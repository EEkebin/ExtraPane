# Microphone forwarding implementation

The Windows development slice captures a locally selected microphone and forwards
bounded PCM packets through the existing encrypted admission path. The receiver
starts a managed local USB Audio Class endpoint helper. This code is not evidence
of successful recording in an application: no physical capture, endpoint attach,
or live microphone connection has been run in this implementation phase. Opus
encoding is not implemented yet.

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

The current alternative implementation uses a local USB Audio Class 2
device emulator using the existing signed usbip-win2 controller and Windows'
inbox audio class driver. This can avoid a new ExtraPane kernel driver while
still creating an ordinary recording endpoint. The MIT-licensed
[usbip-audio library](https://github.com/network-audio/usbip-audio) implements that
approach; [Microsoft documents the inbox UAC2 driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers).
Its dependency, installer ownership, actual PCM delivery, disconnect behavior and
device compatibility still require validation before release availability claims.
No driver installation or system policy change is authorized by an isolated build.

## Native microphone session

`ExtraPane.Microphone.exe` exposes `list-microphones`, `serve-microphone` and
`send-microphone`. This is a dedicated microphone-only listener, separate from the
occupied display/camera ports. One microphone endpoint is active at a time in
this slice. Disconnect destroys that endpoint but keeps the server listening.
The sender uses a selected local device ID supplied on stdin, never a peer's path.
Source leases prevent two ExtraPane connections in the same Windows login session
from sharing the same microphone simultaneously. They do not prevent other audio
applications from using it or coordinate separate Windows logins.

TLS and optional one-time access-code admission finish before the receiver starts
an endpoint; source capture starts only after the endpoint grant. Codes are read
on stdin and are not command-line arguments. Automatic LAN mode provides encryption
without peer identity verification, matching the existing product policy.

The fixed `EPMO` version-1 offer declares PCM16 little-endian, 48 kHz mono and
480 samples per frame, plus a fresh nonzero resource/generation binding. Existing
`EPRS` capabilities/request/grant follow. `EPMD` frames have a 40-byte bounded
header and exactly 960 payload bytes, with contiguous sequence numbers starting
at zero, a source sample position and mute/discontinuity flags. Neither this wire
format nor its approximately 768 kbit/s audio payload is described as Opus.
Unexpected kinds, formats, generations, versions or sequences terminate the
resource. The current TCP transport does not implement adaptive jitter buffering.

The receiver's supervised `ExtraPane.MicrophoneEndpoint.exe --run --slot 1`
receives fixed `EPMI` packets through inherited stdin. Launch inherits only its
input/status handles. Endpoint startup has a 30-second deadline; PCM pipe writes
are cancelable and limited to two seconds. Stop sends an explicit command and
closes stdin, allowing twelve seconds for endpoint-owned detach before terminating
the owned process job. Only that endpoint's USB/IP attachment may be removed.
Endpoint/helper startup and driver backend are intentionally absent from
`--check-config` validation.

Sender stdin commands after selection/admission are `M` to mute, `U` to unmute,
and `Q` to stop; EOF stops capture. Server accepts `G` for a new required access
code and `Q` to stop. Muted frames contain zeros and continue at capture cadence,
so silence does not disable the endpoint idle watchdog.

## Isolated validation

`cmake/Microphone.cmake` defines `dm_microphone` and the portable
`dm_microphone_pcm` conversion library. Configure
`EXTRAPANE_MICROPHONE_TESTS=ON` to add `microphone-pcm` checks. These tests cover
downmixing, clipping, nonfinite samples, silence, bounds and unsupported formats.
They never enumerate or open a microphone. Native source compilation and these
pure checks passed with MSVC; live capture, mute/stop and application recording
verification remain pending.
