# Camera-only session preview

The shared-resource preview uses `ExtraPane.Resources.exe` as an internal native
helper. It does not create a display or require the virtual-display driver. This
first live connection path supports one camera per connection and up to four
receiving connections. It has not been exercised on physical devices yet.

The helper binds the selected local IPv4 address and user-configured port. `auto`
uses the existing listener's active IPv4 interfaces. Static Ethernet addresses
work without DHCP, DNS or discovery. The legacy display listener cannot occupy
the same address/port at the same time: combined display/resource dispatch remains
unfinished, so the shared Windows UI chooses the host mode for this preview.

## Admission and wire sequence

1. Establish the existing TLS 1.3 automatic-LAN connection. With access codes
   enabled, complete the existing one-time access-code exchange before any camera
   message. Without codes, TLS encrypts the connection but does not verify peer
   identity; the extra access-code stream is omitted on both sides.
2. Client sends a 32-byte camera offer. All integer fields use network byte order:

   | Offset | Bytes | Meaning |
   | --- | --- | --- |
   | 0 | 4 | ASCII `EPCM` |
   | 4 | 2 | Version 1 |
   | 6 | 1 | Codec 1: H.264 |
   | 7 | 1 | Reserved, zero |
   | 8 | 2 | Even width, 2–3840 |
   | 10 | 2 | Even height, 2–2160 |
   | 12 | 2 | Frame rate, 1–60 |
   | 14 | 2 | Reserved, zero |
   | 16 | 8 | Nonzero resource identifier |
   | 24 | 8 | Nonzero generation identifier |

   The client chooses fresh random identifiers. Their scope is this admitted TLS
   connection; they cannot attach or authorize another connection. The shared
   codec lives in `core/session/camera_protocol.cpp`.
3. Exchange existing `EPRS` capabilities. Client advertises camera source;
   server advertises camera destination.
4. Client sends a camera request with the offered resource identifier and opaque
   device string `camera`. Physical camera identifiers stay local. The server
   validates the request before constructing its decoder and camera endpoint.
5. After the server grants the request, the client opens its explicitly selected
   camera and sends existing `EPMF` H.264 access units, beginning with sequence 0.
   Sequences must be contiguous. Capture timestamps use 100 ns units; the receiving
   resource normalizes them to its first frame. Every frame must match the offer's
   resource and generation identifiers.
6. Closing the connection stops that camera resource. This initial media phase
   does not multiplex resource-control messages or additional resources. The
   listener remains running for subsequent clients.

The first media header permits up to 15 seconds for client camera/encoder startup;
subsequent transfers have five-second deadlines. Graceful helper stop interrupts
native socket waits using `shutdown`, then joins workers before destroying their
TLS contexts. Decoders, virtual-camera endpoints and COM lifetimes remain owned by
their session worker. No cancellation thread closes/reuses a socket handle.

## Helper control and isolation

Host stdin accepts `G` to generate a new code and `Q` to stop. EOF or loss of the
launcher also stops the helper. Client stdin supplies a local device identifier,
then an optional code, and subsequently accepts `Q`. Codes are not command-line
arguments. `list-cameras` enumerates local devices only when explicitly requested.
`--help` and `serve-camera --check-config --bind <IPv4> --port <port>` do not open
devices, listeners or connections. Configuration checks validate syntax only, not
whether an address is currently assigned or reachable.

Machine-readable output includes `LISTENING`, `CAMERA_STARTED`,
`CAMERA_DISCONNECTED`, `ACCESS_CODE`, `ACCESS_STATUS` and `STOPPED`.
`CAMERA_SESSION_ERROR` is a per-client failure and does not mean the listener
stopped. `error:` denotes a fatal helper/configuration failure.

A local named mutex prevents duplicate camera listeners in one Windows session.
A SHA-256-derived local mutex prevents two helpers in that session from sharing
the same physical source. Mutex names/status do not contain its hardware path.
Cross-Windows-session ownership still needs a machine-wide coordinator; this
preview does not claim that broader guarantee.

## First device test prerequisites

The camera component must be installed before hosting. The new helper does not
create or modify Windows Firewall rules. Windows may prompt to allow it on Private
networks; an existing rule for `dm-view.exe` does not necessarily cover
`ExtraPane.Resources.exe`. Check the chosen interface/profile and authorize any
needed firewall change when the physical test begins, not during a build.

Isolated validation currently covers offer serialization, bounds and version
rejection. Existing resource/codec/pipe checks remain separate. Real TLS socket
cancellation, camera capture, GPU encoding/decoding, Frame Server activation and
Android-to-Windows delivery still need authorized device tests.
