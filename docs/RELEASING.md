# Releasing ExtraPane

The `Build and release` workflow builds Windows on `windows-2025` and calls reusable workflows for the Linux x86_64 AppImage and Android ARM64/x86_64 APK. Android builds debug/test/release variants and runs lint with pinned SDK/NDK tools; trusted runs sign the release APK and verify its certificate, metadata and alignment. PR builds receive no signing secrets. Windows builds the engine, tests, unified WinUI app, driver helper, virtual-display driver, portable archive and installer. Linux builds in a digest-pinned Arch container using the 2026-11-03 package archive snapshot, runs synthetic input checks, packages Qt/GStreamer dependencies and notices, and checks AppImage startup/shutdown under Xvfb outside the build directory. GPU/driver/touch and streaming acceptance still needs real hardware; hosted CI does not substitute for those checks.

Every push to any branch runs the full build (Windows, Linux, Android, macOS) with a CI-assigned version and uploads the installer, signed APK and other packages as run artifacts; a newer push to the same feature branch cancels its older run. Those checks appear on pull requests through the commit. Feature branches never publish, even when they point at a merged commit. To test a branch build, push it and run `scripts/deliver-dev-build.ps1`: it waits for that commit's run, verifies checksums, copies the Windows installer to the Desktop (without running it) and stream-installs the APK on the connected ADB device, then confirms the installed version. Prefer this over local packaging: CI builds `usbipd.exe` deterministically with the pinned SDK, while a locally built one differs byte-for-byte and the installer correctly refuses it over an installed release.

A push to main publishes only when GitHub associates its exact commit with a merged PR targeting main. Manual dispatch on main is the recovery/bootstrap path. The publish job alone has contents-write permission and requires all four platform jobs to succeed. Automatic prerelease versions are `YYYY.MM.DD-pre.<run>.<attempt>` with tags and titles such as `v2026.11.02-pre.14.1`; retries get new immutable tags. Both prereleases and stable releases contain the Windows ZIP and installer, Linux AppImage, Linux dependency-notice archive, signed Android APK and combined `SHA256SUMS.txt`. The publisher rejects missing artifacts and checksum mismatches.

Linux uses hash-pinned tools from `packaging/linux/tools.lock.json`; changed upstream downloads fail instead of executing unreviewed replacements. Dependency versions and upstream source references accompany the notices. The current Linux artifact targets recent Arch-based systems, not arbitrary older distributions. To validate Linux without publishing, dispatch `Linux AppImage` with a calendar version. That workflow uploads a build artifact only.

`Build and release` also provides a `linux_only` validation switch, which skips Windows and never publishes. Platform builds normally run independently after one metadata job. Validation on 2026-11-03: [Linux build, synthetic input, extracted AppImage lifecycle and artifact upload passed](https://github.com/EEkebin/ExtraPane/actions/runs/41724190850). Windows build/package/installer checks passed in the Windows job of run 41722903190; that run's earlier Linux attempt failed before the container-notice fix. The installer fixture now locks the intentionally retained `SideScreen.DriverSetup` mutex. Neither validation run published a release.

Current installers install under Program Files\ExtraPane and offer **Share only**
or **Full installation (receive and share)**. Both include USB source support;
Full installation also prepares the virtual-display, camera and audio/USB receiving
components. Display certificate consent remains explicit. The main ExtraPane
uninstaller owns the managed components; there is one Windows Apps entry.
Portable copies require the matching installer to prepare receiving components.
See [Windows component installation](DRIVER-INSTALLATION.md).

Updates request application/helper shutdown before replacing files. Uninstall
removes ExtraPane-owned components and current-user settings while preserving
independently installed shared drivers. Silent setup defaults to Share only;
`/SERVER=1 /ACCEPTDEVELOPMENTCERT=1` retains its compatibility meaning of Full
installation with explicit development-certificate consent. No device is exported
or virtual monitor created by installation.

App and installer signing is not configured. Do not describe these downloads as a production signed-driver installer. The driver installer stages a development-signed package; Microsoft release signing remains future work. CI verifies staging/removal on disposable runners, while actual driver loading and input acceptance require Windows 11 hardware checks.

Sources: [Windows runner inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md), [Inno per-user installation](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).

## Calendar versions and release names

ExtraPane follows the date-tag / characterful-title style of [kebinImports releases](https://github.com/EEkebin/kebinImports/releases), with zero-padded month/day. This is calendar versioning (CalVer), not strict SemVer. The UTC workflow creation date supplies the date and stays fixed on retries. `VERSION` supplies the local development default; CI overrides it. The About page, executable product/file version strings, installer, manifest and ZIP filenames all use the same release version. Windows numeric fields use `year.month.day.revision`; prerelease identity remains in the full text version.

- **Automatic prerelease:** `v2026.11.02-pre.14.1`. The release title is exactly the tag, with GitHub's prerelease flag; it does not become Latest.
- **Stable:** `v2026.11.02`, titled **The Elbow Room Update**. The title is the name alone. Example description: "Elbow room means space to move: another screen gives your desktop a little more of it."
- **Another stable release on the same UTC day:** `v2026.11.02.2`, then `.3`, and so on. Tags are immutable; never move a published tag or relabel old binaries as a new version.

For a stable release, manually run **Build and release** on `main`, choose `stable`, and provide a short, friendly display-related name and its explanation. Both fields are required. Define any unusual word in that explanation; avoid obscure names that need a paragraph to understand. Use names that fit the actual changes rather than picking a random phrase. Merged PRs always remain prereleases. Existing `v0.13...` history is retained, not rewritten.

The stable dispatch `revision` is `0` for the first release of a day, then `2`, `3`, etc. A stable retry deliberately targets the same tag and cannot overwrite an existing release. Choose the next revision for a new build after publication. Release creation always includes the installer, ZIP, hashes, download instructions, limitations and generated change notes.


Android-only validation uses `Build and release` with `android_only=true` on the feature branch; this never publishes a release. Release signing requires the two repository secrets described in [ANDROID.md](ANDROID.md). The persistent signing certificate is checked against the tracked fingerprint, so a missing or wrong key fails packaging. Never replace it for a routine update. Keep the parent workflow run-number sequence continuous for Android versionCode upgrade ordering. Local debug APKs use a different key and cannot update release installations.

Local Android validation on 2026-11-03: build/lint, real-device JNI and synthetic stylus/keyboard checks, release signing and package verification passed. Physical active-stylus and external-keyboard delivery to host applications remain unverified. See the Android guide for the distinction between implemented fields and hardware evidence.

[Android-only hosted validation passed](https://github.com/EEkebin/ExtraPane/actions/runs/41726764091): clean ARM64/x86_64 builds, lint, signed release verification and APK/checksum artifact upload. The run did not publish a release or run Windows/Linux jobs.


macOS adds the reusable `macos.yml` workflow with native Apple Silicon and Intel runner checks. The shared release requires this job and publishes a Universal ZIP and DMG with combined checksums. `macos_only=true` skips other platforms and never publishes. The app is ad-hoc signed, not Developer ID signed or notarized. See [MACOS.md](MACOS.md) for deployment target, build commands and physical verification gaps.

[macOS validation passed on both architectures](https://github.com/EEkebin/ExtraPane/actions/runs/41729302343): Universal compile, native H.264 color round-trip, protocol/input checks, AppKit startup/shutdown, ZIP/DMG packaging and artifact upload. No release was published. Physical Mac LAN validation remains pending.

[Post-device-fix macOS packages passed on both architectures](https://github.com/EEkebin/ExtraPane/actions/runs/41732576330). These packages include the verified automatic-LAN TLS fix, native fullscreen shortcut, and non-pausing toolbar reveal. M3 Pro live video, mouse/keyboard, fullscreen and hover behavior were confirmed by the user. No public release was created.

Mac host packaging includes the statically linked TLS dependency notices. CI additionally extracts the Universal ZIP outside the build tree, verifies its signature and both architectures, rejects non-system runtime library dependencies, runs packaged protocol/media and UI startup checks, and verifies the DMG and checksum sidecars. These checks do not create virtual monitors or validate Screen Recording permissions.

[Mac host deployment validation passed on Apple Silicon and Intel](https://github.com/EEkebin/ExtraPane/actions/runs/41738412800): Universal builds with native protocol/media/input and UI checks on both runners, plus ZIP/DMG packaging, dependency notices, extracted-app startup, signature checks, system-library dependency checks and checksums on Apple Silicon. This feature-branch run uploaded deployment artifacts without publishing a release.
