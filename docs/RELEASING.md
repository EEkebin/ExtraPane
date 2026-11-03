# Releasing ExtraPane

The `Build and release` workflow builds Windows on `windows-2025` and calls the reusable `Linux AppImage` workflow for Linux x86_64. Windows builds the engine, tests, unified WinUI app, driver helper, virtual-display driver, portable archive and installer. Linux builds in a digest-pinned Arch container using the 2026-11-03 package archive snapshot, runs synthetic input checks, packages Qt/GStreamer dependencies and notices, and checks AppImage startup/shutdown under Xvfb outside the build directory. GPU/driver/touch and streaming acceptance still needs real hardware; hosted CI does not substitute for those checks.

PRs receive build validation without release-write permissions. A push to main publishes only when GitHub associates its exact commit with a merged PR targeting main. Manual dispatch on main is the recovery/bootstrap path. The publish job alone has contents-write permission and requires both platforms to succeed. Automatic prerelease versions are `YYYY.MM.DD-pre.<run>.<attempt>` with tags and titles such as `v2026.11.02-pre.14.1`; retries get new immutable tags. Both prereleases and stable releases contain the Windows ZIP and installer, Linux AppImage, Linux dependency-notice archive and combined `SHA256SUMS.txt`. The publisher rejects missing artifacts and checksum mismatches.

Linux uses hash-pinned tools from `packaging/linux/tools.lock.json`; changed upstream downloads fail instead of executing unreviewed replacements. Dependency versions and upstream source references accompany the notices. The current Linux artifact targets recent Arch-based systems, not arbitrary older distributions. To validate Linux without publishing, dispatch `Linux AppImage` with a calendar version. That workflow uploads a build artifact only.

App installers remain per-user and offer Client only or Server + Client. Server + Client requires the bundled elevated driver installer and explicit development-certificate consent. Portable downloads also contain `ExtraPane.DriverSetup.exe`. The driver installer is machine-wide and independently registered for uninstall. See [driver installation](DRIVER-INSTALLATION.md) for ownership tracking, failures and trust changes.

App updates/uninstall request tray Exit from running instances and wait for app/helper shutdown. The Finish-page launch choice is checked by default and skipped in silent setup. App uninstall optionally removes saved settings and preserves the driver. Driver uninstall removes only recorded installer-owned packages/trust and preserves pre-existing installations. Silent app setup defaults to Client only; server deployment requires `/SERVER=1 /ACCEPTDEVELOPMENTCERT=1` and elevation for the dependency.

App and installer signing is not configured. Do not describe these downloads as a production signed-driver installer. The driver installer stages a development-signed package; Microsoft release signing remains future work. CI verifies staging/removal on disposable runners, while actual driver loading and input acceptance require Windows 11 hardware checks.

Sources: [Windows runner inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md), [Inno per-user installation](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).

## Calendar versions and release names

ExtraPane follows the date-tag / characterful-title style of [kebinImports releases](https://github.com/EEkebin/kebinImports/releases), with zero-padded month/day. This is calendar versioning (CalVer), not strict SemVer. The UTC workflow creation date supplies the date and stays fixed on retries. `VERSION` supplies the local development default; CI overrides it. The About page, executable product/file version strings, installer, manifest and ZIP filenames all use the same release version. Windows numeric fields use `year.month.day.revision`; prerelease identity remains in the full text version.

- **Automatic prerelease:** `v2026.11.02-pre.14.1`. The release title is exactly the tag, with GitHub's prerelease flag; it does not become Latest.
- **Stable:** `v2026.11.02`, titled **The Elbow Room Update**. The title is the name alone. Example description: ?Elbow room means space to move: another screen gives your desktop a little more of it.?
- **Another stable release on the same UTC day:** `v2026.11.02.2`, then `.3`, and so on. Tags are immutable; never move a published tag or relabel old binaries as a new version.

For a stable release, manually run **Build and release** on `main`, choose `stable`, and provide a short, friendly display-related name and its explanation. Both fields are required. Define any unusual word in that explanation; avoid obscure names that need a paragraph to understand. Use names that fit the actual changes rather than picking a random phrase. Merged PRs always remain prereleases. Existing `v0.13...` history is retained, not rewritten.

The stable dispatch `revision` is `0` for the first release of a day, then `2`, `3`, etc. A stable retry deliberately targets the same tag and cannot overwrite an existing release. Choose the next revision for a new build after publication. Release creation always includes the installer, ZIP, hashes, download instructions, limitations and generated change notes.
