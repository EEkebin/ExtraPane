# Windows camera component setup

This documents the shared-resource development branch, not the published release.
The current component requires native x64 Windows; x64 emulation on ARM64 does not
make an x64 DLL loadable inside the native ARM64 Windows Frame Server.
The app uses a single setup flow. Internally, `ExtraPane.CameraSetup.exe` carries
the exact `ExtraPane.CameraSource.dll` built with it as an embedded payload.
It does not accept a DLL path, CLSID, registry root or destination from the caller.

The helper installs the x64 COM source under:

```text
%ProgramFiles%\ExtraPane Components\Camera\ExtraPane.CameraSource.<sha256>.dll
```

Directories permit writes only to Administrators and System, with explicit
read/execute access for Users and Local Service. Existing reparse points,
unexpected owners, unprivileged write permissions, modified content-addressed
payloads and foreign registrations are rejected. Registry operations use the
64-bit machine view. The registration identity is:

```text
HKLM\Software\Classes\CLSID\{5A053AD3-02CD-4281-8367-CEF990182004}
    ExtraPaneOwner = ExtraPane.CameraSource/v1
    InprocServer32\(Default) = <protected DLL path>
    InprocServer32\ThreadingModel = Both
```

An upgrade writes a new content-addressed DLL before switching the registration.
Already loaded versions are retained until component removal, avoiding overwriting
a DLL in Frame Server. A failed registry update attempts to restore every previous
value; failures to restore are reported. This is in-process rollback, not a
power-loss transaction. Initial installation interrupted between registry writes
may require inspection before retrying. Setup does not stop Windows Frame Server,
change camera privacy settings, create a virtual camera, alter certificates or
change system security policy.

## Commands

| Command | Behavior |
| --- | --- |
| `--check-package` | Validate embedded x64 PE payload and print SHA-256; no installation changes. |
| `--status` | Inspect owned registration and payload hash; exit 0 installed, 3 absent, 1 invalid/conflicting. |
| `--install` | Administrator-only install/update of the fixed embedded component. |
| `--remove` | Administrator-only removal of the owned machine component; exit 3010 if loaded files are scheduled for deletion after restart. |

No argument performs an installation. The app or installer must explicitly invoke
`--install` with elevation. Runtime virtual cameras use session lifetime and
current-user access; registering the COM class alone does not publish a camera.

When the package includes CameraSetup, the installer offers **Camera host + Client**
without a virtual-display driver. `/CAMERA=1` selects that role for unattended
setup. Existing `/SERVER=1` selects display hosting and also installs the camera
component when it is packaged. Display-driver certificate consent remains separate.

The machine component can serve multiple Windows users. Ordinary app uninstall
therefore keeps it. The unchecked **Remove the machine-wide camera component
(affects all users)** option, or `/REMOVECAMERA=1`, explicitly requests its removal.
The helper validates all owned payloads before unregistering and only removes
matching content-addressed files. Unknown files and registry values are retained.

## Build and validation

`cmake/CameraSetup.cmake` builds `extrapane_camera_setup` and embeds the camera DLL.
The isolated `camera-setup-policy` check exercises rollback for each registration
write using an in-memory store. `camera-setup-payload` validates the real embedded
DLL without registering or loading it. Neither test writes machine state.

After development staging, `scripts/package-shared-installer.ps1 -Stage <stage>`
compiles the shared installer with Inno Setup 6. It does not launch the installer.

Actual elevated installation, upgrade/removal, loaded-DLL cleanup, Local Service
access and Windows Frame Server activation still require explicit device or
disposable-machine validation. Successful compilation and isolated checks do not
prove camera availability in browsers or conferencing applications.
