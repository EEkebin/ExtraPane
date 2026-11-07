# Managed microphone backend setup (development)

`ExtraPane.UsbSetup.exe` is an internal helper, not a second user-facing
application. The main installer/UI must invoke it explicitly with administrator
approval. Building it does not install, execute or enumerate USB devices.

CMake `UsbSetup.cmake` embeds only USBip 0.9.8.1 x64 with SHA256
`38cad6d4432b52d5bb9409d9ad03b72fdffc4ada4cd3a48fbeca1a2752a8518a`.
Set `EXTRAPANE_USBIP_INSTALLER` to that downloaded artifact. The build checks its
hash, and `--check-package` rechecks the embedded bytes entirely in memory.

`--install` requires elevation and native x64 Windows. It checks for USBip's
uninstall registration in HKLM/HKCU, both registry views, its standard Program
Files directory, and its driver service registrations. Any existing footprint
prevents automatic installer execution. Only an existing exact 0.9.8.1 client
with trusted signed binaries and an installed UDE service is reused; other or
partial installations fail with an error and are left unchanged. The upstream
installer's upgrade path runs the old uninstaller, so it must never be used to
silently replace another app's backend.

When absent, setup writes the hash-pinned installer to the protected component
folder, validates Authenticode trust, and executes only fixed silent/no-restart
arguments for main/client components. It waits for completion; code 3010 reports
that a reboot is needed. It never invokes USBip's uninstaller, global detach,
certificate configuration or test-signing commands itself. The pinned upstream
installer owns its driver/service installation and associated system changes.

After verification, setup copies exactly `usbip.exe`, `libusbip.dll`, and
`resources.dll` into `%ProgramFiles%/ExtraPane Components/Microphone`. Each file
requires a trusted Authenticode signature. Source and destination paths reject
reparse points and unprivileged write ACLs. A protected ownership manifest records
the fixed filenames' hashes. Arbitrary executable paths, DLL names, environment
search paths and command arguments are not accepted. No new versions are guessed
compatible. Unknown or modified component files fail closed.

`--status` reads ownership, hashes and backend compatibility. `--remove` verifies
ownership and removes only managed copies and their marker. Files must not be in
use. The shared USBip installation, drivers, devices and uninstall registration
are preserved even if ExtraPane originally installed them. Other files are not
recursively deleted.

This implementation has not been executed with elevation. The exact upstream
CLI/DLL signature coverage, VC runtime dependencies, successful driver deployment,
reboot behavior, real microphone registration, and install/remove rollback must
be checked before distribution. A signed installer/driver does not prove its
individual CLI and DLL files are signed; if they are not, this helper deliberately
refuses them until an equally strict package-hash trust strategy is implemented.
The backend's own BSD 2-Clause license and redistributable notices must accompany shipping
packages in addition to the microphone adapter's MIT dependency notice.
