# Speakers development

Confirmed behavior: the client selects a physical speaker/headset output; the
server exposes a normal playback device that an application can select. This
is independent of display streaming and microphone capture. It does not mirror
all system audio or change the server/client default output automatically.

The client group appears above Microphones, with independent resource rows,
destinations, power actions, session mute and volume. Disconnect removes only
that session's virtual playback endpoint and releases the selected output stream.
The physical output remains available to local applications.

## Implemented development slice

Windows virtual playback endpoint to a Windows or Android client output, using the existing
managed USB/IP audio backend. Each endpoint is playback-only, stereo PCM16 at
48 kHz, with 10 ms packets. No additional kernel driver is introduced. Native
callbacks feed bounded queues; they never block on network writes. Client playback
uses a clock-driven 40 ms jitter reserve that can grow to 80 ms, refill after underrun,
short transitions and immediate mute/stop cleanup.

Speakers append resource kind 4 without changing the existing four wire values.
Existing four-resource grouped admission remains compatible; speakers use dynamic
admission and distinct media framing. Invalid, unsupported or wrong-kind sessions
must fail before allocating a device.

The server packetizer keeps a 20 ms reserve and sends complete PCM blocks. Each
queue is bounded; these limits are not an end-to-end latency guarantee. Windows
uses shared-mode WASAPI; Android uses a selected AudioTrack output and an independent
playback foreground service. Muting invalidates queued and in-flight audio.

Other client output implementations and Linux/macOS virtual playback endpoints
are follow-up platform gates. Unsupported paths stay unavailable with an explicit
reason; they must never silently connect as a microphone. No speaker device
session has been tested yet. See IMPLEMENTATION-CHECKLIST.md for build evidence.

## Using Speakers

1. Install matching development builds. The Windows server needs the managed
   audio receiving component included with Server + Client.
2. On the client, expand **Speakers**, add a row, choose its local output and
   server address, then connect with the green power button.
3. On the server, select **ExtraPane Speakers N** in Windows Sound or in the
   application's output selector. ExtraPane does not change the default device.
4. Adjust the row's volume or mute independently. Its red power button disconnects
   only this output; Disconnect all speakers stops the speaker rows.

Selected Bluetooth/headset routes depend on OS/device support. A removed or
unavailable selected output ends its session rather than silently playing elsewhere.
Live device playback, simultaneous microphone/display load and long-session
clock drift still require validation.
