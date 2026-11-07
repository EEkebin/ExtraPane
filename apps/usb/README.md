# Windows USB resource session

`ExtraPane.Usb.exe` is the native helper behind the Windows USB pages. It runs
only after an explicit user action. Builds and `--check-config` do not enumerate,
connect, eject, offline, bind or install devices.

The source is a locally selected, dedicated removable storage device. It must
have a unique physical USB identity and storage serial, complete disk/volume
inventory, and no boot/system/paging volume or other owner. Composite devices,
keyboards, cameras, license devices, ambiguous identities and devices already
bound by another USB application are unavailable.

The receiver gets an actual imported USB device, not a shared network folder.
The device cannot remain mounted for normal use on the source during export.

## Session and cleanup

The source connects to the receiver's selected IPv4 interface and TCP port. TLS
and the optional six-digit access code finish before the source locks volumes or
changes disk state. Static-IP Ethernet works without DHCP or discovery. Automatic
LAN mode provides encryption without verifying peer identity.

After admission, the source rechecks identity, claims the device, locks and
flushes every volume, dismounts it, and writes a durable recovery journal. It sets
persistent disk-offline state before releasing volume handles for the explicit
USB-driver handoff. The patched exporter allows only the chosen physical instance
and bus identity and binds only to IPv4 loopback. USB/IP bytes cross the network
inside the admitted TLS stream; no public raw USB/IP listener is opened.

The receiver attaches through its private loopback tunnel using the managed
usbip-win2 client. Its synthetic USB serial is 15 random uppercase alphanumeric
characters, the maximum supported by the pinned client. This changes the virtual
USB serial presented to Windows, not file contents or filesystem volume serials.
The separate session receipt nonce has 128 bits of randomness.

Receiver **E** requests safe eject and then returns to listening. Receiver **Q**
requests safe eject and stops the receiver after success. Eject validates the
owned USB/IP port, loopback address/port, bus identity, synthetic serial, VID/PID
and USBIP controller ancestry. It maps the destination disk's volumes again,
checks each opened volume's disk extent, flushes them, and asks Windows to eject
that exact device. A veto or busy device leaves the session active.

Only successful native eject permits a nonce-bound receipt on the same TLS
session. The source records that evidence durably before releasing its exact
binding. The source remains offline until the user explicitly chooses **Restore
source disk** and recovery checks pass. Disconnect, timeout, parent exit, helper
crash and source **Q** provide no clean-eject evidence: the source stays offline
with its journal and binding available for recovery. The private export process
is in a kill-on-close job so it cannot outlive a crashed helper.

These checks do not make remote USB descriptors safe against a malicious
admitted peer. USB sharing should connect computers the user trusts. The native
handoff, successful application writes, destination eject, reconnect and recovery
still require physical-device validation; none were exercised by isolated builds.

## Internal protocol and launcher

`list-storage` is an explicit read-only inventory action. It emits bounded lines:
`USB_DEVICE_V1<TAB>hex UTF-8 opaque ID<TAB>label<TAB>available or reason`, ending
with `USB_LIST_DONE`. IDs are local inventory identities, never paths selected
by a network peer.

`send-usb --bind ADDRESS --peer ADDRESS --port N --device-stdin [--access-stdin]`
reads the decoded UTF-8 ID, then an optional access code, as bounded input lines.
`serve-usb --bind ADDRESS --port N [--require-code]` listens for one session at a
time and survives rejected or disconnected clients. `G` regenerates a one-use
code. Both commands accept `--check-config` for validation without side effects.

The elevated UI launches the helper with `--pipe NAME --parent-pid PID`. The
helper requires a local ExtraPane pipe name, matching pipe-server PID and user
SID, and opens it with identification-only impersonation permissions. The parent
is watched throughout the session. Device IDs and codes never appear in argv.

The EPUS v1 negotiation is fixed binary: 8-byte request/grant/ready controls,
64-byte source offer (nonce, VID/PID, selected bus), and 32-byte clean-eject
receipt. The EPUT v1 tunnel uses 24-byte headers, 64 KiB maximum chunks and 256 KiB
bounded credit queues. A single worker owns all TLS reads and writes. The exporter
side gates the initial raw USB/IP import to the exact selected bus; device-list
requests and imports of other devices are rejected before local forwarding.
Turn-based exchange limits throughput by network round-trip time; no latency or
throughput claim has been physically verified.
