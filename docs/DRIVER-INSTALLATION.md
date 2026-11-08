# Server driver installation

The virtual-display driver is required on a PC that hosts an extended desktop. A receiving client does not need it. Selecting **Server + Client** in ExtraPane Setup makes driver installation a required dependency, not an optional finish-page task.

## Normal setup

1. Run the ExtraPane installer and choose **Server + Client**.
2. Read and accept the development-certificate explanation.
3. Approve the ExtraPane installer administrator prompt. The app and its managed components install under Program Files\ExtraPane.
4. After setup completes, open Server and choose Start server. A monitor is created only after a client connects.

If you choose **Client only**, setup does not install a driver or trust a certificate. Both app pages remain available. To host later, rerun setup as Server + Client to install the required components.

For the current shared-UI portable server, use the matching installer with **Server + Client** to prepare the managed components, then extract the ZIP and run `ExtraPane.exe`. The older `ExtraPane.DriverSetup.exe` is part of historical display-only packages. Display-driver installation is machine-wide and requires Windows administrator permission.

Canceling elevation or certificate consent prevents server setup. Driver installation runs after the main app and its uninstaller are available. If it fails, setup reports a nonzero result, suppresses application launch, and keeps ExtraPane installed for repair or removal. Resolve the reported error and rerun the installer; the PC is not ready to host until driver setup succeeds.

## What changes on Windows

The main ExtraPane installer:

- Installs the driver manager and package under `%ProgramFiles%\ExtraPane\Components\Display`, owned by the main ExtraPane uninstaller.
- Verifies the package's filenames, SHA-256 hashes, certificate identity/validity and catalog signer.
- Adds only the package's public code-signing certificate to the machine Root and TrustedPublisher certificate stores, after consent.
- Checks Windows catalog trust and uses SetupAPI to stage the signed INF/CAT/DLL in the Windows Driver Store. Windows validates catalog membership during staging.
- Records the exact package hashes, published `oemNN.inf`, and which driver/certificate entries it added, under its administrator-controlled `state` directory.
- Registers only **ExtraPane** in Windows Apps. The installed app does not use a nested display-driver installer or uninstaller.

It does not create a test monitor, change network addresses, disable Secure Boot or Memory Integrity, enable test-signing mode, or restart Windows automatically. It does not install a display driver on the receiving PC unless that PC is also being configured to host.

The package is signed with a development code-signing certificate, not a Microsoft release signature. Each newly built package has an ephemeral signing key; packaging discards that private key and ships only the public certificate. Its validity dates and hashes are in `package\manifest.json`. Frozen package output is not overwritten by the packaging script.

Trusting a development certificate is a machine-wide change. It is shown separately from choosing a role. Driver-store installation and runtime loading are different checks: Windows security policy can still refuse to load a successfully staged driver. The installer never changes boot policy to work around that refusal.

## Updating and removing

The app installer requests that existing ExtraPane instances exit and waits for their helpers to stop before changing files. The driver manager also refuses to change packages while named ExtraPane session/driver processes remain active.

Reinstalling the same driver package is idempotent. Installing a different package records its own signing identity and driver-store entry; older installer-owned entries are retained for tracked removal. This does not overwrite ownership of packages installed before this manager existed.

Uninstall **ExtraPane** through Windows Apps to remove the app, its managed components and the executing account's saved settings. The main uninstaller calls the display manager directly. It verifies recorded package identities, removes only installer-owned driver-store packages, and removes only certificate entries it added when the corresponding package is no longer staged. Pre-existing packages and trust are preserved. No force-removal flag is used.

The separately distributed portable driver setup remains available for portable users. A standalone installation has its own uninstall entry until a verified main-app upgrade takes ownership.
If removal fails, the uninstaller keeps its manager and records for a retry. If Windows asks for a restart, restart manually and retry. Do not delete ownership records or guess an OEM INF number. The older development-script installations remain governed by their original records; see the historical notes in [EXTENDED-DISPLAY.md](EXTENDED-DISPLAY.md).

## Logs and troubleshooting

- Driver manager: `%ProgramFiles%\ExtraPane\Components\Display\driver-setup.log`.
- Windows driver installation: `%SystemRoot%\INF\setupapi.dev.log`.
- Package identity and ownership: `%ProgramFiles%\ExtraPane\Components\Display\state`.

An elevation cancellation does not mean Windows installed the driver. A hash or signer mismatch prevents installation; obtain a fresh copy of the release. An expired development certificate requires a newer packaged driver. A driver-store failure reports an error instead of treating the PC as configured for hosting.

If installation succeeds but Start server or a client connection fails, preserve the error and logs. The next check is actual device loading and the machine's policy; repeating certificate imports or changing boot security is not an automatic repair step.

## Silent deployment

Silent app setup defaults to Client only. Explicit server deployment requires both flags:

```powershell
.\ExtraPane-<version>-Setup-win-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SERVER=1 /ACCEPTDEVELOPMENTCERT=1
```

The driver step still requires administrator permission. For unattended deployment, use an appropriately elevated deployment context; `/VERYSILENT` does not bypass UAC. The standalone driver setup likewise requires `/ACCEPTDEVELOPMENTCERT=1` when silent. These flags authorize the documented machine certificate trust change; no security settings are silently disabled.

## Verification scope

Build-time checks verify package contents, signatures and hashes without trusting or installing them. CI tests are restricted to disposable GitHub Windows runners. The test definitions cover consent rejection, driver failure with retained repair/uninstall, exact driver-store identity, repeat installation, tampered-payload rejection, unified removal and preservation of independently installed packages and certificates. The updated main-installer integration has compiled but still requires its disposable-runner and user installation trials.

CI staging tests do not establish that an indirect display can load on every Windows 11 configuration, and they do not create a virtual monitor. The new installer still needs the user's prerelease trial on their PCs. Do not run `test_driver_installer.py` on a development or personal machine.

## Name-change compatibility

ExtraPane was previously called SideScreen. The current manager imports verified ownership records from the older directory before taking responsibility for those packages. After successful installation, the main installer retires the exact recognized driver uninstall entry. It does not run the old uninstaller during migration. The old directory and redundant uninstaller files may remain as rollback remnants; their cleanup is still incomplete. Do not run a stale driver uninstaller after migration. Existing settings remain until ExtraPane is uninstalled.
