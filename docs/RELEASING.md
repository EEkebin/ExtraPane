# Releasing SideScreen

The `Build and prerelease` workflow uses the GitHub-hosted `windows-2025` image. It builds the native engine, runs five CTest suites, builds both WinUI apps, creates portable archives and Inno Setup installers, and verifies payload hashes and embedded icons. GPU/driver/touch acceptance still needs real Windows hardware; hosted CI does not substitute for those checks.

PRs receive build validation without release-write permissions. A push to main publishes only when GitHub associates its exact commit with a merged PR targeting main. Manual dispatch on main is the recovery/bootstrap path. The publish job alone has contents-write permission. Versions are `0.12.0-pre.<run>.<attempt>`; retries get new immutable tags. Releases contain four downloads and `SHA256SUMS.txt`.

Installers use Inno Setup 6, install per-user under `%LOCALAPPDATA%\Programs\SideScreen\Client` or `Server`, and register independent uninstallers. Both formats carry the same payload. Exit from the tray before upgrading or uninstalling: the named app mutex blocks updates while running. The Server executable still requests elevation. Uninstall preserves Credential Manager pairings, HKCU settings and the separately installed driver. No installer modifies driver signing or firewall policy; the Server manages its scoped runtime rule.

App and installer signing is not configured. Do not describe these downloads as a production signed-driver installer. Driver distribution/signing remains separate release work.

Sources: [Windows runner inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md), [Inno per-user installation](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm).
