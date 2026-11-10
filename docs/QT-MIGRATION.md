# One Qt interface on every platform

Status: in progress (started 2026-11-10). Decision: [DECISIONS.md](DECISIONS.md)
"Shared interface and resources (2026-11-07)" chose Qt Quick/QML over the C++
engines; Windows and Linux use it, Android (Java views) and macOS (AppKit) still
have separate hand-built interfaces. That split is why fixes and features landed
on one platform only (custom-PIN selector, renderer picker, refresh/header
layout, mangled UI text). This plan finishes the migration.

## Principles

- **One UI, native engines.** `apps/shared/ui/PeerWorkspace.qml` and
  `PeerController` drive every platform. Media, input, permissions and OS
  services stay native: Android Java (Camera2, MediaCodec, AudioRecord/Track,
  stylus, foreground services, multicast lock, runtime permissions), macOS
  Objective-C++, Windows/Linux C++.
- **Platform differences are data, not forks.** QML reads capability flags
  (for example `rendererSettings`, input families, receive support) instead of
  platform-specific copies of a screen.
- **Ship nothing worse.** The current Android and macOS apps stay the shipping
  apps until the Qt build passes the parity checklist on real devices. The pilot
  uses a separate package/bundle id so both can be installed side by side.
- **One Qt version.** Android needs Qt >= 6.10 for 16 KB page alignment and
  NDK 27; the pilot uses Qt 6.11.3. Desktop moves from 6.8.3 to the same version
  in a separate, fully tested step (license pins in
  `scripts/package_shared_windows.py`, aqt pins in CI).

## Milestones

1. **Android toolchain and shell** - Qt 6.11.3 for Android (arm64-v8a, x86_64)
   builds `PeerWorkspace.qml` + `PeerController` against the C++ core with the
   pinned NDK 27 and signs with the existing key under a pilot package id.
   Checks: 16 KB alignment, both ABIs, offscreen QML test on desktop unchanged.
2. **Android bridge** - C++ `Service::Hooks` for Android (port of
   `apps/android/native/peer_service_jni.cpp` hooks) plus a Java `MediaBridge`
   extracted from `PeerActivity`: inventory (offers), `native-ready`/`native-stop`
   media start/stop, foreground service, permissions, multicast lock for nearby
   search. Display-destination receive stays a native Activity launched from QML.
3. **Android parity** - pairing (rotating/custom), nearby/manual connect,
   paired devices, resource sharing toggles, active streams, disconnect, forget,
   back navigation, insets/IME, touch-sized controls. UI tests run the same QML
   for phone widths.
4. **Android switch** - the Qt app takes the production package id; remove the
   Java views (`PeerActivity`, `MainActivity`, `CaptureRows`, `SpeakerRows`, ...).
5. **macOS** - Qt app over the existing Objective-C++ engines (`peer_runtime.mm`
   et al.), then remove `peer_workspace.mm`.
6. **Desktop Qt upgrade** to the shared version.

## Parity checklist (per platform, on a real device)

- [ ] Pairing in both directions, wrong-PIN message, rotating and custom secret
- [ ] Nearby search and manual address connect
- [ ] Share/stop each resource kind; streams survive UI navigation
- [ ] Disconnect, forget, reconnect after network change
- [ ] Permissions prompts and denial handling
- [ ] Background/foreground behavior (Android foreground service)
- [ ] Accessibility labels and keyboard/back navigation

## Progress

- 2026-11-10: plan written; Qt 6.11.3 desktop + Android SDKs restored into
  `out/qt-sdk-6.11` for the pilot.
