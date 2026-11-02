# Releasing SideScreen

The `Build and release` workflow uses the GitHub-hosted `windows-2025` image. It builds the native engine, runs five CTest suites, builds the unified WinUI app and driver helper, creates a portable archive and an Inno Setup installer, and verifies payload hashes and embedded icons. GPU/driver/touch acceptance still needs real Windows hardware; hosted CI does not substitute for those checks.

PRs receive build validation without release-write permissions. A push to main publishes only when GitHub associates its exact commit with a merged PR targeting main. Manual dispatch on main is the recovery/bootstrap path. The publish job alone has contents-write permission. Automatic prerelease versions are `YYYY.MM.DD-pre.<run>.<attempt>` with tags and titles such as `v2026.11.02-pre.14.1`; retries get new immutable tags. Releases contain two downloads and `SHA256SUMS.txt`.

Installers use Inno Setup 6, install per-user under `%LOCALAPPDATA%\Programs\SideScreen`, and register one uninstaller. Both formats carry the same payload. Exit from the tray before upgrading or uninstalling: the named app mutex blocks updates while running. The main SideScreen executable runs asInvoker. Start Driver uses ShellExecute/runas for SideScreen.Service.exe; the helper owns the firewall rule and driver engine, and stops when the app closes or requests stop. Uninstall offers an unchecked **Remove saved pairings and settings on this PC** checkbox. Selecting it deletes the current user's `DM/peers/dm-lan` Credential Manager entry and `HKCU\Software\DM\Host`, `Receiver`, and `Display` settings. This also resets preferences shared with portable copies. Other credentials, other users and the separately installed driver are untouched. Silent uninstall preserves data by default; pass `/REMOVEUSERDATA=1` to explicitly reset it. Canceling uninstall does not reset data. No installer modifies driver signing or firewall policy; the Server manages its scoped runtime rule.

App and installer signing is not configured. Do not describe these downloads as a production signed-driver installer. Driver distribution/signing remains separate release work.

Sources: [Windows runner inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md), [Inno per-user installation](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).

## Calendar versions and release names

SideScreen follows the date-tag / characterful-title style of [kebinImports releases](https://github.com/EEkebin/kebinImports/releases), with zero-padded month/day. This is calendar versioning (CalVer), not strict SemVer. The UTC workflow creation date supplies the date and stays fixed on retries. `VERSION` supplies the local development default; CI overrides it. The About page, executable product/file version strings, installer, manifest and ZIP filenames all use the same release version. Windows numeric fields use `year.month.day.revision`; prerelease identity remains in the full text version.

- **Automatic prerelease:** `v2026.11.02-pre.14.1`. The release title is exactly the tag, with GitHub's prerelease flag; it does not become Latest.
- **Stable:** `v2026.11.02`, titled **The Elbow Room Update**. The title is the name alone. Example description: ?Elbow room means space to move: another screen gives your desktop a little more of it.?
- **Another stable release on the same UTC day:** `v2026.11.02.2`, then `.3`, and so on. Tags are immutable; never move a published tag or relabel old binaries as a new version.

For a stable release, manually run **Build and release** on `main`, choose `stable`, and provide a short, friendly display-related name and its explanation. Both fields are required. Define any unusual word in that explanation; avoid obscure names that need a paragraph to understand. Use names that fit the actual changes rather than picking a random phrase. Merged PRs always remain prereleases. Existing `v0.13...` history is retained, not rewritten.

The stable dispatch `revision` is `0` for the first release of a day, then `2`, `3`, etc. A stable retry deliberately targets the same tag and cannot overwrite an existing release. Choose the next revision for a new build after publication. Release creation always includes the installer, ZIP, hashes, download instructions, limitations and generated change notes.
