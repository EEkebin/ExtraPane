# Shared Qt interface on mobile: not pursued

Decision (2026-11-10, user): **Android keeps its native Java interface.** A Qt
for Android pilot (shared `PeerWorkspace.qml` + `PeerController` over the C++
core, Qt 6.11.3) built, signed and ran on the Pixel 8a, but the user judged it
clearly less polished and less "normal" than the native app. The pilot app was
uninstalled and its code removed. Do not restart this migration without a new
user decision.

Qt/QML remains the interface for Windows and Linux. macOS keeps its AppKit
interface unless the user decides otherwise.

## Keeping native interfaces consistent instead

The original problem was drift: fixes and features landed on one platform only
(custom-PIN selector, renderer picker, Nearby header/refresh layout, text with
lost "·"/"—" characters). Address it without a shared UI toolkit:

- When a user-visible behavior changes, change it on desktop (QML), Android
  (Java) and macOS (AppKit) in the same increment, or record the gap in
  `IMPLEMENTATION-CHECKLIST.md`.
- Keep the same section order, wording, control placement and states across
  platforms (for example: refresh in the section header; "Searching nearby"
  only while searching; status only when idle).
- Platform conventions win where they differ (Android back gesture, system
  insets, Material components; macOS menus and controls).
- Encode UI text as UTF-8 and prefer escapes for non-ASCII separators; the
  desktop UI check fails on " ? " or U+FFFD.

## Leftovers from the pilot

- `scripts/fetch_qt.py` and `tools/qt-6.11.3.lock.json` restore Qt 6.11.3 from
  the per-target repository layout that aqtinstall 3.3.0 cannot read. They are
  kept for a possible future desktop Qt upgrade (Qt 6.8.3 is current there).
- `out/qt-sdk-6.11` (~2.8 GB) and `out/qt-archives` are local downloads only and
  can be deleted.
