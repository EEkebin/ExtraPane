# Linux camera and microphone resources

This increment adds native Linux camera and microphone resource adapters beside
Linux display hosting and reception. The listener routes encrypted resource
connections on one port; camera and microphone requests do not create monitors.

## Sources

GStreamer enumerates physical capture sources. The adapter checks the selected
identity again before capture and reports missing plugins, absent sources and
permission errors. Camera output is H.264 with an even negotiated resolution up
to 3840 x 2160 and an integer target of 1-60 FPS. The default comes from a raw
mode advertised by the device; fractional hardware rates may be converted to an
integer stream rate. Compressed-only device modes are not currently supported.
Mirror can change while connected.

Microphone output is PCM16 little-endian, 48 kHz mono in 10 ms packets. The native
packetizer handles arbitrary aligned capture chunks, positions, muting and
discontinuity. Normal provider capture does not detach the source from Linux.

Different physical sources can run concurrently. Connections to several servers
share one native capture pipeline and encoder per physical source within the
app. Cross-process source leases prevent a second ExtraPane process from taking
over that capture. Each recipient has its own bounded queue, transport and mute
state; a slow recipient disconnects independently instead of stalling capture
for other servers. Mirror is shared by all recipients of that camera. A camera
joining an existing capture waits for an IDR containing SPS/PPS configuration;
dependency frames are never silently dropped. The last recipient stops capture.
Camera recipients must use the same source mode. Protected
multi-resource connections use the per-destination control/ticket broker rather
than reusing a one-use access code.

## Receiving camera

A camera receiver requires a prepared, writable v4l2loopback output slot whose
card label begins with `ExtraPane Camera`. The adapter checks character-device,
driver, output/write capabilities and exclusive lease before using the slot.
It never writes to physical cameras or another application's loopback node.
Start server does not create kernel devices or alter device permissions.

Each incoming camera reserves a separate slot. If one unused slot rejects the
negotiated resolution, format, buffer geometry or frame rate, the receiver
releases it and tries the remaining slots. Foreign and already leased nodes are
never configured. Disconnect releases the selected descriptor and lease.

GStreamer decodes H.264 to YUY2. Dimensions, format, allocations and both decoder
and output strides are checked before writing. Applications select the prepared
ExtraPane camera node. Availability depends on prepared slots, kernel/module
compatibility and permissions; an AppImage cannot provide a running-kernel module.

## Receiving microphone

GStreamer's PipeWire sink in `provide` mode exports a virtual `Audio/Source`
with a random session-specific name and an ExtraPane Microphone description.
The node belongs to the process and disappears when its pipeline or process
closes. No persistent audio configuration is changed.

Before granting the resource, the receiver primes the stream and verifies its
exact node name and media class using bounded, read-only `pw-dump` output.
This requires a running user PipeWire session, PipeWire tools and its GStreamer
plugin. A bounded drop-old PCM queue handles periods with no consuming app.
The receiver does not play microphone audio through speakers or change the
default input; applications select the virtual input themselves.

## Remaining gates

Device-list refresh runs in an isolated `ExtraPane --list-resource camera` or
`microphone` helper using QCoreApplication, without opening a window or starting
capture. The UI limits the JSON response to 1 MiB and terminates enumeration
after ten seconds; exit also terminates inventory helpers. A stalled third-party
GStreamer device provider therefore does not hold the UI's shutdown thread.
Live capture still uses its selected provider and needs device validation.

Camera receiving rejects v4l2loopback frame-rate changes instead of granting an
incorrect rate. Parsed H.264 dimensions must match negotiated geometry before
the decoder receives the stream.

Linux USB export/import is not implemented and remains unavailable with a reason.
Kernel USB/IP support alone does not implement ExtraPane's ownership, exclusive
mount handling, encrypted forwarding and reliable return behavior.

The portable packetizer check covers fragmented buffers, several packets per
buffer, sequence and position progression, mute, discontinuity and alignment.
The portable fanout check covers independent recipient queues, packet/byte limits
and late joins requiring IDR plus SPS/PPS. Fake capture checks exercise the same
registry used by native capture: create once, mode rejection, last-subscriber
cleanup, concurrent replacement and unrelated-device teardown isolation.
These checks do not exercise a camera
encoder or prove delivery to several physical receivers.
Native compilation and actual delivery to applications are separate validation
gates. Isolated checks have not exercised physical capture devices, virtual
camera nodes or the user's PipeWire graph.

References: [PipeWire sink](https://github.com/PipeWire/pipewire/blob/master/src/gst/gstpipewiresink.c),
[PipeWire properties](https://docs.pipewire.org/page_man_pipewire-props_7.html),
[V4L2 output](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-output.html).

## Portable diagnostics

Run the AppImage with --check-media-factories to print JSON listing available and missing media factories. This checks plugin loading only: it does not enumerate devices, start capture, open a virtual camera, or contact PipeWire. The pwDumpAvailable field reports whether the host prerequisite is on PATH; it does not prove the PipeWire service is running. The packaged CI startup check requires every bundled media factory and the Linux setup guides, separately from physical device acceptance.
