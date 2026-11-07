# Multiple resource connections

## Requested behavior

Keep one ExtraPane installation and one server listener. Replace the client's
fixed four resource rows with repeatable Display, Camera, Microphone and USB
connection rows. Every row has a stable identity, source selection, independent
enabled state, status, and lifecycle. Disconnect all stops every row, preserving
USB exclusive-ownership and bounded-return rules.

The user requested two destination modes:

- Single server: one IPv4 address, port, interface and access-code entry at the
  top. Per-row destination controls are hidden and every row uses that server.
- Multiple servers: each row selects its server address/port and interface;
  rows may share a server or use different servers. Admission is scoped to each
  destination. Secrets are not persisted.

The same camera or microphone may be sent to several servers where supported.
Native capture must be shared, with independent bounded outbound streams. A slow
or disconnected recipient must not stop other recipients. USB storage remains
exclusive to one recipient, including while returning or awaiting recovery.

Examples: two different microphones sent to one server; each microphone sent to
a different server; one microphone sent to two servers; multiple cameras routed
independently; concurrent display windows receiving desktops from different
servers. Multiple source devices arriving at the same server must appear as
distinct normal OS camera/microphone endpoints.

Display rows select a local monitor as the basis for native resolution and
refresh rate, or specify custom width, height and target FPS. The stream target
is distinct from the physical panel's supported refresh rates. Requested modes
must pass host driver, encoder and receiver capability validation; never silently
claim an unsupported custom rate is delivered. Display data still flows from
server to client, unlike cameras/microphones.

Each display row owns a normal resizable receiver window with its selected
fullscreen destination. The controls must be accessible by touch, including a
keyboard action; automatic keyboard activation needs a remote editable-focus
signal and must not be inferred from every touch.

Closing the main window hides it to the tray while the server or any client
connection is active. With no active work it exits normally. Double-clicking
the tray icon restores and focuses the main window in either role.

## Baseline and implementation status

The previous Windows controller had one admission connection and a fixed four-element
client array indexed by resource kind. The shared resource model similarly
represented one row per kind. These could not represent repeated cameras/microphones
or independent destinations.

The new Windows server allocates four camera slots, four display slots, four
microphone slots and one USB slot. Microphone endpoint launch, readiness and
ownership are scoped to the assigned slot. Camera and microphone source brokers
share one capture per physical source and isolate recipient queues. These paths
have compiled and their isolated queue checks pass; simultaneous delivery to real
applications still needs device testing.

The repeatable connection model and per-destination runtime are being integrated
with the shared UI. Exact custom display modes require the updated display
driver. Packaging and live acceptance testing are separate completion gates.

Existing admission tickets are scoped by resource kind. Concurrent rows of the
same kind must not revoke or consume each other's pending tickets. Admission,
resource identities and cleanup need row-aware ownership before concurrency.

## Implementation sequence

1. Add an instance-based connection model with repeatable rows and validated
   per-row settings. Add Single server/Multiple servers destination resolution.
   Changing destination mode must not reroute a running stream silently: active
   rows retain their effective destination until explicitly reconnected.
2. Replace kind-indexed Windows child/session ownership with per-instance state.
   Scope admission to destination and distinguish repeated same-kind resources.
   A failure/disconnect must affect only its row and owned native resources.
3. Support several distinct microphone receiving endpoints, slot-specific names,
   identifiers and lifecycle. Preserve camera/display capacity negotiation and
   show explicit receiver capacity errors. Do not advertise unlimited resources.
4. Implement native camera/microphone capture sharing for repeated physical
   sources: one capture owner, per-recipient negotiation and bounded queues,
   reference-counted release, permission revocation propagated to all subscribers.
5. Add local monitor selection and per-display custom geometry/FPS, independent
   receiver windows, and per-window input/focus/fullscreen ownership.
6. Package matching endpoints. Verify model, admission and failure isolation with
   isolated checks before authorized hardware tests. Verify actual separate OS
   endpoints and simultaneous deliveries, not just successful connection calls.

Implement Windows first against the current shared-app test build. Keep the
common model suitable for the other platforms, but label native platform
capability gaps honestly until their adapters are implemented and tested.

## Windows shared-app integration (2026-11-07)

The shared Windows client now uses `ConnectionModel` and
`WindowsConnectionRuntime`. Rows have stable UUIDs and retain their effective
server/interface while connecting, active, returning USB, or awaiting recovery.
The Single server form supplies new connections; Multiple servers exposes a
destination form per row. An empty admitted server connection is real runtime
activity, including for close-to-tray behavior.

Each destination owns one `ExtraPane.Session` control helper. Resource ticket
requests are serialized until the recipient reports admission/started or its
attempt ends. A timed-out admission closes that destination rather than assigning
a late ticket to another row. Codes and scoped credentials travel over private
stdin and are not saved in connection settings or process arguments.

Camera and microphone rows share one native source broker per physical source.
The runtime uses `broker-camera` / `broker-microphone`, sends row-scoped `ADD` and
`REMOVE` commands, and consumes `RESOURCE_EVENT` messages. Source-wide camera
mirroring is synchronized across its rows; microphone mute is per recipient.
Disconnecting one destination leaves recipients on other destinations running.
Display and USB rows retain independent native helpers. USB first requests
receiver eject, then ends the exporter and reclaims the exact source on refusal
or timeout when local ownership checks pass. Interrupted return keeps a visible
incomplete-write warning. Unknown ownership is not treated as freely reusable.

Each display row opens an independent resizable receiver window. It defaults to
a custom virtual display at 1920x1080 and 60 FPS without a local monitor selection.
Match local monitor is an optional alternative. Checking the resource establishes
the connection; the separate Connect button has been removed. Custom requests use even dimensions from 2 to 4096,
at most 8,847,360 total pixels, and 1–360 target FPS. The native host still validates
the actual requested mode. These controls do not change physical monitor refresh.
Current Windows receiving limits are four displays, four cameras, four microphone
endpoints, and one USB storage connection per server. A camera source broker
supports eight recipients; a microphone source broker supports sixteen across
destinations. These are limits, not promises that a particular device can sustain
every connection at its requested quality.

Isolated verification covers model identity, frozen destinations, settings
validation and USB exclusivity; offscreen QML loading of both legacy and repeated
connection layouts; and fake-child sessions with repeated same-source camera
rows, microphone coexistence, recipient failure isolation, mirror propagation,
USB return veto/retry, and empty admission lifecycle. This is not physical-device
delivery, throughput, microphone endpoint, custom-display driver, or USB-storage
verification. Matching native helpers and the updated display driver are required
for the staged build.

## Separate USB incident

The Surface absent-reader ownership record was retired by the separately
authorized pre.13 repair without opening or modifying the missing disk. See
IMPLEMENTATION-CHECKLIST.md for the audit and limitations. This display/UI
iteration does not perform new USB sharing or recovery operations.

## Windows custom virtual modes

The host can construct `VirtualMonitor(slot, desiredMode)` with an explicit
client-requested width, height and refresh rate. The launcher passes a versioned
binary property to that software-device instance. The driver validates even
dimensions from 2 through 4096, at most 8,847,360 pixels, and 1–360 Hz. Malformed or
out-of-range properties fail device initialization instead of silently falling
back to a nearby resolution.

Each adapter and monitor owns its own mode list: the requested mode first,
followed by the existing defaults without duplicates. No process-global mutable
mode list is used. Because the IddCx descriptor parser has no monitor context,
the driver encodes the request in a bounded private EDID text descriptor and
checks its format, bounds and checksum when parsing it. The existing manufacturer
and product identity stays unchanged. A default request preserves the original
EDID, four default modes and preferred index; the launcher explicitly clears a
prior slot's custom request when switching back to defaults.

The unsigned 0.9.0.0 driver and catalog build passed with no warnings/errors.
Its three-digit refresh descriptor supports up to 360 Hz and still decodes the
legacy two-digit descriptor.
Isolated property tests passed ABI bounds, descriptor roundtrips, corruption
rejection and independent device descriptions. Driver installation and live
custom-resolution/FPS tests have not been run for this change. Windows, capture,
encoding and the receiver must still accept the mode; requesting 360 Hz does not
establish that 360 frames per second are delivered.

The mode callbacks retain the documented IddCx count-only behavior when output
buffers are null. See Microsoft's [descriptor callback input contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/ns-iddcx-idarg_in_parsemonitordescription)
and [mode-origin definitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/ne-iddcx-iddcx_monitor_mode_origin).

## Resource groups (2026-11-07)

The client groups rows into Displays, Cameras, Microphones and USBs. Each group can be expanded or collapsed without changing its connections. Add creates another row of that kind. Disconnect all within a group stops only that group; the page-wide action stops all groups. USB continues through its normal safe-return path. The Server page omits routine component-present messages and retains actionable component errors.

## Automatic Windows keyboard (2026-11-07)

The Windows receiver requests host-side automatic keyboard mode. After an
admitted touch is released, the host confirms writable focused UI Automation
metadata at the touched point on the streamed monitor. ValuePattern and writable
TextPattern editors qualify; field contents are not read. The host requests the
Windows accessibility On-Screen Keyboard and positions only its own verified
process window on that monitor. Existing keyboards are preserved, and OS launch
or positioning failures are logged. Manual receiver Keyboard remains available.
This does not implement the modern Windows touch keyboard, TSF or IME. Matching
Windows builds are required for the new request; Linux/macOS hosts accept the
optional request but do not implement this Windows keyboard behavior. Physical
touch typing and keyboard placement remain acceptance tests.

## Desktop navigation and group controls

The app opens on Server without starting the server. Desktop navigation can
collapse to an icon rail; its menu button expands it again. Resource group titles
are centered across their headers, independently of the add/disconnect actions
on the right. Icon actions have tooltips and accessible names. Navigation and
collapse are presentation changes only, leaving resource sessions untouched.
