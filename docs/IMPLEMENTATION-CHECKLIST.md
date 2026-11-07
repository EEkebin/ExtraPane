# Resource-sharing implementation checklist

This is the working checklist for the shared ExtraPane app and device-sharing
features. Update it after every completed increment, in the same change as the
implementation. A checked implementation item does not imply physical-device or
release verification. Keep this file consistent with RESOURCE-SHARING.md.

## Current target

Complete camera (including Windows sender UI), microphone and USB implementation
before requesting any device tests, per the user's updated direction. The first
Android-to-Windows camera slice has passed isolated checks; this does not mean the
combined product is ready. Continue isolated builds/checks only. Ask before device
connections, camera/GPU tests, installation or system changes. iOS, renaming and
speaker forwarding remain deferred.
## Shared interface and connections

- [x] Shared Qt/QML shell foundation and resource checklist.
- [x] Windows adapter for the existing native display engine.
- [x] Development package Qt startup repair and offscreen checks.
- [x] Resource lifecycle, ownership, capability negotiation and bounded framing.
- [x] Isolated encrypted negotiation and tamper-rejection checks.
- [x] Camera-only live protocol integrated into Windows and Android app sessions.
- [x] Camera format negotiation and connection/status/error UI.
- [ ] Shared UI migration and display regression checks across all four platforms.

## Camera

- [x] Windows enumeration, native capture and H.264 source adapter implemented.
- [x] Bounded encoded-frame queues and resource/generation validation.
- [x] Native receiver connects framed messages to H.264 decoding and NV12 output.
- [x] Windows camera-source DLL, COM factory and activation implemented.
- [x] Restricted local pipe and receiving-side MF sample delivery.
- [x] Session-lifetime virtual-camera registration backend implemented.
- [x] Resource cleanup for transport/decoder errors, stop and restart.
- [x] Isolated synthetic delivery, COM lifetime and failure-cleanup checks.
- [x] Managed Windows camera setup, upgrade, rollback and removal implemented (physical verification pending).
- [x] Android camera selection/permission/status and Windows camera host/setup UI.
- [x] Windows Client camera picker, explicit asynchronous refresh, share/stop and status UI; isolated Windows/Linux QML checks passed.
- [x] Camera-only connection implemented without allocating a monitor.
- [x] Android Camera2/H.264 forwarding and foreground lifecycle implemented (device verification pending).
- [ ] Linux camera sources/endpoints.
- [ ] macOS camera sources/endpoints.
- [ ] Physical Windows capture and GPU encode/decode verification.
- [ ] Actual Windows Frame Server registration/service-account verification.
- [ ] Android-to-Windows picture, color, latency and reconnect verification.
- [ ] Camera selectable and usable in browser/Discord.

## Microphone

- [x] Windows WASAPI shared-mode source and PCM conversion implemented; isolated conversion checks passed.
- [x] Local UAC2 microphone endpoint helper implemented with pinned dependency; 4 unit checks and safe startup check passed.
- [x] Windows microphone session, bounded PCM wire and picker/mute/stop controls; isolated UI/protocol/configuration checks passed.
- [x] Managed signed USB/IP setup helper compiled with payload and policy checks; no installation performed.
- [ ] Verify component setup/recording behavior after the full implementation milestone.


- [x] Windows/Android native capture and bounded PCM16 transport.
- [ ] Opus compression and source sample-rate conversion.
- [x] Windows recording-device endpoint implementation through managed USB/IP UAC2 helper.
- [ ] Verify endpoint enumeration and actual application recording; Linux/macOS endpoints remain unimplemented.
- [x] Windows/Android source mute, permission and interruption handling implemented; physical behavior pending.
- [x] Android AudioRecord source, microphone foreground service and mute/stop UI implemented; APK and lint pass.
- [ ] Physical Android foreground/background, lock and permission-withdrawal behavior.
- [x] Pinned signed USB/IP backend setup implemented for the Windows microphone endpoint.
- [ ] Verify installed driver compatibility and resolve production USB identity/distribution prerequisites.
- [ ] Application-level recording and reconnect tests.

## USB

- [x] Platform-independent storage eligibility, exclusive ownership and recovery lifecycle with synthetic checks.
- [x] Windows read-only disk inventory implementation and synthetic descriptor/layout checks.
- [x] Native storage lock/dismount adapter and bounded single-worker USB tunnel implemented; synthetic lifecycle/framing checks pass.
- [x] Protected local USB backend, durable offline journal, explicit recovery helper and managed source setup compiled; synthetic checks pass.
- [x] Windows USB source picker, destination eject and guided recovery controls implemented; offscreen UI checks pass.
- [x] Native Windows USB session integration built and failure paths reviewed; 15 isolated USB checks pass.

- [x] Windows USB forwarding through encrypted admitted sessions with supervised private backend processes.
- [ ] Linux USB forwarding and cross-platform USB verification.
- [x] Windows per-device selection, exclusions, unique identity and exclusive ownership implemented.
- [x] Windows source lock/offline/handoff and destination flush/eject implemented; physical behavior remains unverified.
- [x] Disconnect quarantine and durable verified-eject recovery implemented; no automatic source disk-online operation.
- [x] Managed Windows source/receiver setup and conservative shared-component retention implemented.
- [ ] Validate setup/removal on devices and implement upgrades between differing component payloads.
- [ ] Physical removable-storage validation.

## Packaging and release

- [x] Shared Windows development staging and isolated startup check.
- [x] CI definitions for isolated resource and camera checks.
- [x] Windows camera installer integration source and graceful app shutdown protocol.
- [ ] Compile and verify the new installer; compiler staging was blocked by automatic approval review.
- [ ] Complete single-installer workflow for all new components.
- [ ] Cross-platform package, upgrade and uninstall verification.
- [ ] Resolve macOS extension signing and other distribution prerequisites.
- [ ] User-authorized device tests.
- [ ] Publish a testable prerelease after required checks.

## Increment log

| Increment | Result | Verification / remaining limit |
| --- | --- | --- |
| `4a4aae2` | Resource foundation and shared Qt display shell | Isolated UI/model checks; platform migration incomplete. |
| `c1179ff` | Bounded resource negotiation | In-memory encrypted admission checks; no live session integration. |
| `0c06ab2` | Windows camera source and encode adapter | Compiled; synthetic NV12 checks, no physical camera. |
| `b478091` | Camera framing and bounded queues | Isolated bounds, encryption and cancellation checks. |
| `1cae15f` | Receiving MF camera source | Synthetic native sample/lifecycle checks. |
| `0b11798` | COM DLL, local transport, registration backend | Unregistered DLL delivery and lifetime checks; Windows registration not run. |
| `2631915` | Decoder-to-camera receiver integration | Build and synthetic-decoder delivery checks; real GPU path not run. |
| 2026-11-07 camera preview | Windows setup and live receiver, Android sender, shared host UI and installer shutdown | 11 Windows checks and 4 core checks passed; APK/Android lint and packet checks passed. No device installation or camera/GPU tests. |
| 2026-11-07 Windows sender | Windows camera picker/share/stop UI and USB ownership foundation | Offscreen camera/fallback UI and 5 core suites pass; no physical device access. |
| 2026-11-07 native sources | WASAPI source, local microphone endpoint helper and native USB inventory | Native builds/PCM and inventory checks; 4 Rust unit checks. No capture, attachment or disk enumeration executed. |
| 2026-11-07 microphone connection | Windows microphone session/UI and managed component setup | 6 core suites, UI checks, native configuration/payload checks pass. No live sockets or driver installation. |
| `dc802ce` | Microphone backend ownership hardening | Five isolated Rust checks and release build pass; protected payload hashes/ACLs and exact port identity checked before cleanup. No driver/device execution. |
| 2026-11-07 Windows USB session | Native source/receiver, scoped import, safe eject, durable recovery, shared controls and package integration | 15 isolated USB checks, offscreen UI and native builds pass. Driver handoff and actual device delivery still need physical verification. |
| 2026-11-07 USB ownership and recovery | Protected backend, offline journal, source setup and guided Windows controls | Native recovery/setup payload checks and offscreen UI pass. No storage enumeration, driver setup or real USB session run. |
| 2026-11-07 Android microphone / USB transport | Android microphone sender and native storage/tunnel foundations | Android APK/instrumentation compilation/lint; 7 core suites; synthetic native storage lifecycle checks. No live USB or audio access. |

Concurrent implementation is authorized. Device testing is deferred until camera, microphone and USB implementation is complete. CAMERA-TEST.md is a future test guide, not a request to test now.
