# Server driver installation

The virtual-display driver is required on a PC that hosts an extended desktop. A receiving client does not need it. Selecting **Server + Client** in ExtraPane Setup makes driver installation a required dependency, not an optional finish-page task.

## Normal setup

1. Run the ExtraPane installer and choose **Server + Client**.
2. Read and accept the development-certificate explanation.
3. Approve the driver installer's administrator prompt. The app itself remains a per-user installation.
4. After setup completes, open Server and choose Start Driver. A monitor is created only after a client connects.

If you choose **Client only**, setup does not install a driver or trust a certificate. Both app pages remain available. To host later, rerun setup as Server + Client or launch `ExtraPane.DriverSetup.exe` from the installed app folder.

For a portable server, extract the ZIP, run its `ExtraPane.DriverSetup.exe` once, then run `ExtraPane.exe`. A portable client only needs the latter. The driver's installation is machine-wide and requires Windows administrator permission.

Canceling elevation or failing the driver step prevents Server + Client setup from continuing. Retry after resolving the error, or return to Client only. If the driver step succeeds but the app installation is later canceled or fails, the separately registered driver can remain installed; remove it through Windows Apps if it is no longer wanted.

If driver installation fails after its manager has been copied, the manager and its Windows Apps entry remain available for repair or removal. Setup reports failure; it does not mark the PC ready to host.

## What changes on Windows

The driver installer:

- Installs its manager, package and uninstaller under `%ProgramFiles%\SideScreen Driver`.
- Verifies the package's filenames, SHA-256 hashes, certificate identity/validity and catalog signer.
- Adds only the package's public code-signing certificate to the machine Root and TrustedPublisher certificate stores, after consent.
- Checks Windows catalog trust and uses SetupAPI to stage the signed INF/CAT/DLL in the Windows Driver Store. Windows validates catalog membership during staging.
- Records the exact package hashes, published `oemNN.inf`, and which driver/certificate entries it added, under its administrator-controlled `state` directory.
- Registers **ExtraPane Virtual Display Driver** as a separate Windows Apps entry.

It does not create a test monitor, change network addresses, disable Secure Boot or Memory Integrity, enable test-signing mode, or restart Windows automatically. It does not install a display driver on the receiving PC unless that PC is also being configured to host.

The package is signed with a development code-signing certificate, not a Microsoft release signature. Each newly built package has an ephemeral signing key; packaging discards that private key and ships only the public certificate. Its validity dates and hashes are in `package\manifest.json`. Frozen package output is not overwritten by the packaging script.

Trusting a development certificate is a machine-wide change. It is shown separately from choosing a role. Driver-store installation and runtime loading are different checks: Windows security policy can still refuse to load a successfully staged driver. The installer never changes boot policy to work around that refusal.

## Updating and removing

The app installer requests that existing ExtraPane instances exit and waits for their helpers to stop before changing files. The driver manager also refuses to change packages while named ExtraPane session/driver processes remain active.

Reinstalling the same driver package is idempotent. Installing a different package records its own signing identity and driver-store entry; older installer-owned entries are retained for tracked removal. This does not overwrite ownership of packages installed before this manager existed.

Uninstalling **ExtraPane** removes the per-user app and optionally its saved settings. It preserves the machine-wide driver because other users or portable copies may still need it.

To remove the driver, uninstall **ExtraPane Virtual Display Driver** through Windows Apps. It stops ExtraPane, verifies its recorded package identities, removes only installer-owned driver-store packages, and removes only certificate entries it added when the corresponding package is no longer staged. Pre-existing driver packages and trust are preserved. No force-removal flag is used.

If removal fails, the uninstaller keeps its manager and records for a retry. If Windows asks for a restart, restart manually and retry. Do not delete ownership records or guess an OEM INF number. The older development-script installations remain governed by their original records; see the historical notes in [EXTENDED-DISPLAY.md](EXTENDED-DISPLAY.md).

## Logs and troubleshooting

- Driver manager: `%ProgramFiles%\SideScreen Driver\driver-setup.log`.
- Windows driver installation: `%SystemRoot%\INF\setupapi.dev.log`.
- Package identity and ownership: `%ProgramFiles%\SideScreen Driver\state`.

An elevation cancellation does not mean Windows installed the driver. A hash or signer mismatch prevents installation; obtain a fresh copy of the release. An expired development certificate requires a newer packaged driver. A driver-store failure reports an error instead of treating the PC as configured for hosting.

If installation succeeds but Start Driver or a client connection fails, preserve the error and logs. The next check is actual device loading and the machine's policy; repeating certificate imports or changing boot security is not an automatic repair step.

## Silent deployment

Silent app setup defaults to Client only. Explicit server deployment requires both flags:

```powershell
.\ExtraPane-<version>-Setup-win-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SERVER=1 /ACCEPTDEVELOPMENTCERT=1
```

The driver step still requires administrator permission. For unattended deployment, use an appropriately elevated deployment context; `/VERYSILENT` does not bypass UAC. The standalone driver setup likewise requires `/ACCEPTDEVELOPMENTCERT=1` when silent. These flags authorize the documented machine certificate trust change; no security settings are silently disabled.

## Verification scope

Build-time checks verify package contents, signatures and hashes without trusting or installing them. CI tests are restricted to disposable GitHub Windows runners. They exercise consent rejection, required server dependency installation, exact driver-store identity, repeat installation, tampered-payload rejection, separate app/driver uninstall, and preservation of independently installed packages and certificates.

CI staging tests do not establish that an indirect display can load on every Windows 11 configuration, and they do not create a virtual monitor. The new installer still needs the user's prerelease trial on their PCs. Do not run `test_driver_installer.py` on a development or personal machine.

## Name-change compatibility

ExtraPane was previously called SideScreen. The driver manager intentionally retains its original fixed `Program Files\SideScreen Driver` directory and internal uninstall identity so upgrades preserve ownership records and can still remove previously installed packages and certificates. Its Windows Apps display name is ExtraPane Virtual Display Driver. The app installer likewise recognizes the previous installation and retains settings. These upgrade paths require verification before distributing the renamed build.
