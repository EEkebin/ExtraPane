# USB device sharing

USB sharing means transferring exclusive access to an actual USB device. It is
separate from camera and microphone media forwarding, which do not deliberately
detach the source device. The Windows development app includes USB storage sharing; physical compatibility and recovery remain hardware validation gates.

## Windows inventory

See [USB-INVENTORY.md](USB-INVENTORY.md) for the native read-only candidate
inventory and its conservative protection rules. Unknown ownership, incomplete
volume information, protected disks, and composite devices remain unavailable.

## Pinned host backend preparation

The candidate backend is
[usbipd-win](https://github.com/dorssel/usbipd-win), pinned at
`4431eba52a2de9f21e3a4ba1f7dc1e408cd57019`. The reviewed source uses .NET SDK
`10.0.403`. ExtraPane carries a small patch in
`patches/usbipd-loopback.patch`:

- The USB/IP listener binds only IPv4 loopback, never a public/LAN interface.
- A local launcher must specify both `usbipd:AllowedBusId` and
  `usbipd:AllowedInstanceId`. The bus ID must be canonical. Missing or invalid
  configuration admits no devices.
- Device listing and import require that exact bus and instance identity, and
  the device must already be bound. Policy-based auto-bind is removed from this
  connection path. Other devices previously bound by usbipd remain unavailable.
- The private registry namespace is `HKLM\SOFTWARE\ExtraPane\UsbHost`. Startup
  does not clear other devices' attachment state. The local `extrapane-bind`
  command compares the expected physical instance ID inside the bind operation
  before persisting it, so a reused bus address cannot select a replacement.
- The upstream global singleton mutex is unchanged, so an independently running
  usbipd server must not run alongside this process.

These configuration keys are for a trusted local launcher after admission and
exclusive locking. They are not client-selected IDs. The patch does not perform
volume locking, establish that ExtraPane owns an existing binding, authenticate
loopback clients, or prevent another local process from reaching the selected
device. Loopback binding is network exposure reduction, not authentication.
Do not deploy or launch this preparatory backend until those layers exist.

Prepare source without building:

```powershell
./scripts/build-usb-host.ps1
```

Build the process project only, with at most two build workers:

```powershell
./scripts/build-usb-host.ps1 -Build
```

Produce a self-contained x64 native AOT process using the installed Visual Studio
C++ tools and the same pinned SDK:

```powershell
./scripts/build-usb-host.ps1 -Publish
```

This writes `out/usb-host/publish`. Neither command executes the backend. The
script disables .NET's first-run certificate generation and uses a build-local
CLI home. A native AOT build still does not install the required USB drivers.

For an existing upstream clone, `-Repository out/reference/usbipd-win` avoids
another network clone; the exact pinned commit is still checked. The script
refuses unexpected source modifications and does not reset or overwrite them.
It never executes usbipd, binds devices, starts services, installs drivers, or
builds/runs the upstream installer. `-Build` is a framework-dependent development
build; `-Publish` produces the native process. Neither output is a finished USB
feature or installer by itself.

## Source storage preparation and recovery

`StorageOperations` implements the source preparation sequence around injected
local backend and recovery owners. Its native handle adapter opens the selected
disk, compares descriptor/layout/device-number facts against the local snapshot,
subscribes to physical USB instance changes, opens all mapped volumes, and
verifies their extents. It locks every volume before flushing or dismounting any
of them. A busy volume aborts preparation. A partial dismount failure retains
locks and requires recovery. The fake lifecycle check exercises these orders
and failures without opening a disk.
After durable offline quarantine and binding intent are verified, an explicit
handoff revalidates and closes the temporary volume/disk handles before the
USB backend's expected driver restart. Removal supervision then belongs to the
backend's exact selected device. It does not blindly ignore a removal observed
before handoff, and it never clears the persistent offline bit.

The host backend requires a durable recovery guard before binding or starting
USB export. It records binding intent before the exact selected-device bind,
records the resulting owned GUID, and verifies quarantine again before starting
the private daemon and admitted tunnel. Busy destination eject leaves the
session active. Verified destination eject is durably recorded as `ejected`
before any unbind, so a crash between those steps does not lose the receipt.
Cleanup waits up to five seconds for the source driver to return before trying
the exact owned binding removal; otherwise explicit recovery can finish it.
Unexpected transport loss does not authorize unbinding or
bringing the source disk online. After a successful destination eject, it may
remove only the binding GUID created by this session and confirmed by fresh
local state. Normal return then restores the verified source disk automatically.
Recovery controls are reserved for interrupted or incomplete returns.

Holding volume handles alone is insufficient crash protection: usbipd can
restore the source USB driver when the network stream closes, and driver
re-enumeration can invalidate old handles. The recovery design therefore uses a
protected durable journal and persistent disk-offline state. The physical USB
device may reappear locally while its storage remains quarantined. Hardware
validation must establish that offline state survives the actual driver
handoff, detach, unplug/replug, and process failure for supported devices.
Devices that cannot satisfy the offline/identity checks must remain unavailable.

The native `RecoveryGuard` now writes a bounded versioned record below protected
`HKLM\SOFTWARE\ExtraPane\Components\UsbRecovery` before changing a disk. The
record includes its prior attributes, disk number, SHA-256 identity fingerprint,
owner nonce, phase, bus ID and acquired binding GUID. Each state write is flushed
with `RegFlushKey`. Existing incomplete or foreign journals prevent a new export.
Registry links, unprotected ownership/DACLs and non-administrator write access
are rejected. A global per-disk mutex serializes local operations across logins.

Quarantine requires the storage owner's locked-handle verifier, then rechecks the
present SetupAPI disk/USB instances, disk number, descriptor and partition bytes
through a fresh physical handle. It sets `DISK_ATTRIBUTE_OFFLINE` with
`Persist=TRUE` and reads it back before recording the offline phase. Only the
offline bit is changed; read-only and other attributes are preserved. Destruction,
disconnect and an abandoned mutex never authorize bringing storage online.

Explicit recovery requires trusted local destination-eject and exact backend
binding-release verification. It rechecks the recorded identity, restores only
the prior offline bit, commits a restored record, and finally removes that
journal. A failed flush or incomplete transition keeps recovery required. The
disk number is deliberately part of the strict fingerprint: renumbering after
reboot or re-enumeration can make automatic recovery refuse even the same physical
device. The implementation does not silently relax that check.

If that happens, preserve the journal and stop sharing. An administrator must
first confirm the destination has flushed/ejected the storage and the export
binding is gone, then compare the physical device identity and partition layout
against the journal/saved inventory before a manual Windows Disk Management
recovery. Never bring an uncertain disk online or delete recovery records merely
to bypass the error. The app lists recovery records and can restore verified
identities; it deliberately offers no override for this manual-review case.

Native compilation/linking and pure journal/state checks passed without reading
or changing the registry or any disk. The persistent offline behavior is still a
hardware validation gate, particularly for USB storage that re-enumerates under
a different identity. See [Microsoft's durable registry flush behavior](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regflushkey).
Native handle destruction closes its OS handles, so a long-lived protected
owner and the durable quarantine, not a C++ object's lifetime, must govern
recovery. Those end-to-end guarantees have not been established by fake tests.

## Guided recovery helper

`ExtraPane.UsbRecovery.exe --list` reads protected journals and emits JSON with
each journal ID, disk number, state, and reason recovery is blocked. It does not
enumerate disks, run usbipd, write registry values, or change disk state. An
Standard users have read-only access to newly created journal keys; only
Administrators and System can write them. Older restrictive keys may require an
administrator terminal and are never silently repermissioned. A malformed
journal remains untouched and is reported as invalid.

After a successful destination eject and exact owned binding removal, an
administrator can explicitly request:

```powershell
ExtraPane.UsbRecovery.exe --recover <journal-id-from-list>
```

This command accepts an opaque local journal ID, never a disk number, device
path, or remote claim that eject succeeded. It finds a fresh local disk whose
identity and partition fingerprint match the journal, rejects protected or
incomplete inventory, reserves the verified private backend, and confirms that
the device is neither attached nor bound. A `released` journal is the durable
receipt of the session's native destination eject and binding-release sequence.
An `ejected` journal preserves verified destination eject when cleanup stopped
before unbind. After proving the disk remains offline and the binding still
belongs to that exact device and journal, explicit recovery can remove only
that recorded GUID, verify it is absent, then restore source access.
`prepared` and `offline` records can also be recovered after local checks because
they never authorized a USB binding or export. `bound` and `binding-intent`
records remain blocked without the required release evidence. `restored`
records support retry after a final journal-deletion failure.

There is no force override. An unplug/replug that changes the disk number or
identity can deliberately block this helper; do not delete the journal or
manually bring a disk online to bypass missing ownership evidence. The UI may
use the list result to explain the state and offer an explicit recovery action,
but `canAttemptRecovery` is only a journal-state hint: the helper repeats all
local device and ownership checks before changing anything.

## Redistribution and remaining integration

### Managed Windows host setup

When the source helper is bundled, the single Windows installer shows a separate
**Enable USB storage sharing from this PC** checkbox on its role page. This
unchecked-by-default option applies to the source computer, including a PC using
the Client role; it is independent of which receiving role is selected. Setup
invokes the elevated source helper only when this option is selected. Silent
deployment must explicitly supply `/USBHOST=1`; requesting it from a package
without the component fails before other component installation. The Ready page
lists the requested source setup and its machine-wide changes. Merely installing
or updating the app leaves existing USB source components alone when unchecked.

Receiving USB devices uses the shared USB/IP receiving component, also used by
the microphone endpoint. It is distinct from the optional source/export backend.
Installing either component does not select or transfer a storage device.

`ExtraPane.UsbHostSetup.exe` is an internal component helper, not another product
download. Build it with `EXTRAPANE_USB_HOST_SETUP=ON` after the pinned native
publication exists in `out/usb-host/publish`. It embeds the native executable,
`usb.ids`, event manifest, notices and the four signed-package files under
`Drivers`. `--check-package` checks their compiled hashes in memory without
installing, starting, enumerating or binding a device.

The explicit elevated `--install` action creates protected files under
`Program Files\ExtraPane Components\UsbHost` and the private registry tree
`HKLM\SOFTWARE\ExtraPane\UsbHost`, including `APPLICATIONFOLDER`, `Devices` and
`Policy`. This is separate from an independent usbipd-win installation. It stages
`VBoxUSB.inf` with `SetupCopyOEMInf` and `SP_COPY_NOOVERWRITE`, preserving an
existing identical package. It creates the shared `VBoxUSBMon` kernel service as
demand-start only when absent. An existing service is accepted only when its
protected image has the expected hash and trusted signature; incompatible or
ambiguous installations are left unchanged and reported. Setup does not start
that driver, install the upstream public server service, invoke the upstream MSI,
add firewall rules, or bind any USB device.

`--remove` refuses pending recovery journals, persisted bindings/policies or
in-use executable files. It disables the private host registration and removes
owned user-mode files. Shared driver-store packages, the monitor service and its
protected driver files remain installed, even if ExtraPane originally created
them, because another application may now depend on them. Protected ownership
records permit an explicit same-version reinstall and retain partial setup state
for diagnosis/retry. No driver or system-wide service is automatically stopped
or uninstalled. Migration to different payload hashes needs an explicit upgrade
path; this first helper refuses to overwrite mismatched managed files.

The application uninstaller does not invoke USB component removal or uninstall
shared USB drivers automatically. Finish any storage eject/recovery before
uninstalling the app. If explicitly removing source components with the managed
helper, a pending recovery journal prevents removal so the backend needed for
recovery is preserved; driver-store and shared service retention still apply.

Native compilation/linking and embedded-payload verification passed. Setup,
service loading, device binding, uninstall and upgrade have not been executed on
the development PCs. Packaging must include the corresponding source materials
below alongside every redistributed modified backend.

The backend is GPL-3.0-only, with component-specific notices described in
upstream `COPYING.md` and `LICENSES`. The build script places the exact upstream
source archive, patch, build script, license notices, and checksummed manifest
in `out/usb-host/corresponding-source`. Those materials must accompany any
redistributed modified backend binary; a link to upstream alone does not
describe ExtraPane's modifications. Preserve upstream notices and review all
bundled dependencies before release. ExtraPane's own license remains a separate
project decision.

The Windows session now composes the native inventory, locked/offline handoff,
private exporter, encrypted tunnel, imported-device identity and destination
flush/eject. Child processes are job-owned. Verified eject is journaled before
binding removal so explicit recovery can resume after a crash. The app exposes
source selection and live USB checkboxes, receiving eject, and advanced recovery
controls. Setup is handled by the installer.

These are development implementations, not physical verification. Installation,
lock-to-driver handoff, actual imported storage, eject/reconnect and recovery still
require authorized hardware checks. Driver upgrade/rollback and uninstall need
separate installation testing. The conservative identity checks can reject drives
without a unique serial or complete ownership information.

Use raw USB only with trusted computers. Source eligibility is enforced by the
ExtraPane source app. The receiver forwards USB/IP data to the installed driver;
it does not sandbox a malicious peer or validate every possible USB descriptor
and transfer. Automatic LAN mode does not verify peer identity. TLS encryption
and optional access codes do not turn raw USB into an untrusted-device sandbox.

## MicroSD adapter limitation found during testing

On the tested Surface USB microSD reader, taking the disk offline changed the
Windows drive-layout response from the actual partition table to a whole-disk
pseudo partition. Export stopped before binding, leaving a durable offline
journal. Reconnecting the same reader and card restored local exFAT access, but
the saved v1 fingerprint still did not match. The record was preserved and no
forced recovery or partition change was performed.

Unresolved records make the disk unavailable in the USB picker and reject a new
export before network admission or disk mutation. `ExtraPane.UsbRecovery --inspect
<journal-id>` is read-only and exposes the recorded/current fingerprints and disk
metadata for diagnosis. It does not authorize recovery. Do not retry export on
this adapter with older builds. Version 2 journals now fingerprint bounded raw
partition-table metadata, disk geometry and device identity, avoiding Windows'
synthetic offline layout. Basic partitioned MBR and GPT are supported; extended
MBR chains and unpartitioned/superfloppy media are rejected. Filesystem data is
excluded so ordinary file writes do not invalidate the fingerprint. GPT headers,
table checksums, backup consistency and partition bounds are checked. Cloned
cards with identical metadata cannot be distinguished by this fingerprint.
Version 1 restoration retains its original strict comparison.

The tested Surface's never-exported record was retired by the guarded
already-online reconciliation. Its card remained Online/Healthy as D: exFAT.
The new export and automatic-return paths still require physical testing.

## Automatic return and stale pre-export records

Unchecking USB asks the receiver to flush and safely eject its owned device. A
busy-device refusal keeps the tunnel running and rechecks USB in the client.
Disconnect all stops other resources but retains USB and its control connection
until return succeeds; a busy refusal cancels the pending full disconnect.
Only a verified eject receipt and exact owned-binding release authorize automatic
source restoration. Network loss does not supply that proof.

The bounded EPUT/EPUS v2 protocol carries return requests and busy responses
separately from USB/IP payload. Both endpoints require matching builds.

`ExtraPane.UsbRecovery --reconcile-online` retires only protected `prepared` or
`offline` journals with no binding history when the identified local disk slot
is already online and a fresh backend check proves no binding or attachment.
It neither asserts that a replacement card is the original medium nor changes
disk attributes or contents. The client performs the same assessment for source
selection and repeats it under an ownership reservation before a new export.
Uninstall invokes reconciliation before removing components. Missing disks,
malformed journals and any evidence of export remain blocked for recovery.
