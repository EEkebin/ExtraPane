# Microphone forwarding implementation

The current Windows slice captures a locally selected microphone. It does not
yet transmit audio, encode Opus, or create a Windows recording device. An app
must not advertise microphone forwarding from this source code alone.

## Windows source

`dm::microphone::enumerate()` lists active recording endpoints and their friendly
names without activating capture. `Capture` construction is inert. `start()`
creates one worker that rechecks the local selection and owns all WASAPI and COM
objects through activation, capture, stop and release. Windows privacy policy and
device-in-use errors are reported through `read()`; no settings are changed.

Capture uses shared mode, so ExtraPane does not intentionally detach or reserve
the hardware exclusively. Availability alongside other programs still depends
on the driver, Windows privacy policy and other programs' exclusive use.

The first format supports the device's **48 kHz**, mono or stereo mix format in
signed PCM16 or float32. Stereo is averaged to mono. Float samples are clipped;
nonfinite values become zero. Other rates, layouts and packed formats fail with
an unsupported-format message. There is no resampler yet. Output is signed PCM16
mono and is not labeled Opus.

Native event capture carries sequence numbers, device positions and
discontinuity flags. Packets are limited to 100 ms and the producer queue to eight
packets. A slow consumer fails the source instead of growing memory indefinitely.
Mute clears queued audio and zeros subsequent samples without changing the system
microphone volume. Stop signals the worker, clears queued audio, stops WASAPI and
releases the endpoint on its owning thread. A stalled or removed device reports
failure; creating a new capture object is required to start again.

No permission prompt is bypassed. Windows desktop microphone privacy settings
must permit access. The capture API has not been exercised on a physical device
in this implementation phase.

## Ordinary recording endpoint

A source plus network stream is not an ordinary recording device. The planned
receiver must supply real PCM to a managed endpoint that recording applications
can select. No loopback playback device or generated test tone substitutes for it.

The original Windows design uses a SysVAD-derived WaveRT audio driver with a
bounded PCM ingress. Microsoft's sample generates a tone until that ingress is
implemented. Such a new kernel driver requires a distribution signing path;
building it or trusting a local development certificate does not satisfy stock
Windows production loading policy. See [Microsoft's SysVAD sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/)
and [driver signing policy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-policy--windows-vista-and-later-).

An alternative now under implementation review is a local USB Audio Class 2
device emulator using the existing signed usbip-win2 controller and Windows'
inbox audio class driver. This can avoid a new ExtraPane kernel driver while
still creating an ordinary recording endpoint. The MIT-licensed
[usbip-audio library](https://github.com/network-audio/usbip-audio) implements that
approach; [Microsoft documents the inbox UAC2 driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers).
Its dependency, installer ownership, actual PCM delivery, disconnect behavior and
device compatibility still require validation before changing availability claims.
No driver installation or system policy change is authorized by an isolated build.

## Isolated validation

`cmake/Microphone.cmake` defines `dm_microphone` and the portable
`dm_microphone_pcm` conversion library. Configure
`EXTRAPANE_MICROPHONE_TESTS=ON` to add `microphone-pcm` checks. These tests cover
downmixing, clipping, nonfinite samples, silence, bounds and unsupported formats.
They never enumerate or open a microphone. Native source compilation and these
pure checks passed with MSVC; live capture, mute/stop and application recording
verification remain pending.
