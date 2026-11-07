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
- [x] One Windows server accepts display, camera, microphone and USB connections on the same port; no server resource dropdown. Native build and isolated routing/UI checks pass; live verification pending.
- [x] Windows client multi-select checkboxes, independent device/status/stop/mute state, and grouped one-code admission implemented; isolated checks pass, physical combined-resource verification pending.
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

- [x] Combined Windows camera/microphone/USB portable development package staged at `out/windows-multiselect-20261107`; packaged offscreen startup, USB configuration, microphone endpoint and setup-payload checks pass.
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
| 2026-11-07 combined Windows package | Camera/microphone/USB app and internal setup helpers staged together; test guide and source materials included | Packaged offscreen startup and helper checks pass. No device tests or driver installations. Inno installer compilation remains blocked. |
| 2026-11-07 Windows USB session | Native source/receiver, scoped import, safe eject, durable recovery, shared controls and package integration | 15 isolated USB checks, offscreen UI and native builds pass. Driver handoff and actual device delivery still need physical verification. |
| 2026-11-07 USB ownership and recovery | Protected backend, offline journal, source setup and guided Windows controls | Native recovery/setup payload checks and offscreen UI pass. No storage enumeration, driver setup or real USB session run. |
| 2026-11-07 Android microphone / USB transport | Android microphone sender and native storage/tunnel foundations | Android APK/instrumentation compilation/lint; 7 core suites; synthetic native storage lifecycle checks. No live USB or audio access. |

Concurrent implementation is authorized. Device testing is deferred until camera, microphone and USB implementation is complete. CAMERA-TEST.md is a future test guide, not a request to test now.

2026-11-07 camera setup correction: status now uses read-only registry APIs; setup can initialize an entirely empty class key while preserving nonempty registrations. Setup policy/payload checks and offscreen UI pass. Actual installation retry remains pending.

2026-11-07 unified-server routing: added bounded eight-byte lookahead above
TLS/access admission and classification of existing display/camera/microphone/USB
prefixes. Existing clients need no wire change. Eight isolated core checks pass,
including prefix preservation across split reads and failed-stream rejection;
Windows TLS library compiles. Unified listener integration and physical
simultaneous-resource verification remain pending.

Unified routing verification also passes an in-memory authenticated-record test: eight-byte lookahead preserves the complete camera offer and rejects tampered encrypted records before routing. No network socket, capture device or driver was exercised.

2026-11-07 unified server: native dispatcher dc1fbbf and UI now start all supported receivers together. Five packaged native targets rebuild; 12 configuration checks, eight core checks, authenticated-record routing and offscreen UI pass. Optional components are prepared only for requested resources. No live listener or device tests were run.

2026-11-07 unified server deployment: packaged offscreen startup passed; all 1,322 files copied and verified on the Surface Desktop as `ExtraPane-unified-server-20261107`. Old Surface camera-fix build removed. Automatic approval review blocked local old camera-fix folder/archive cleanup (reason: blocked by policy), so those copies remain. No application, listener or device was started on the Surface.

2026-11-07 client multi-select: model/UI checks cover combined display/camera/microphone selections, independent source devices, stop/mute, unavailable devices and no automatic capture. Group admission uses one-use per-resource tickets, with real in-memory PAKE/AEAD expiry/replay/wrong-kind checks; all native helpers rebuilt. Physical simultaneous delivery remains unverified.

2026-11-07 multi-select deployment: packaged offscreen startup and Session helper configuration checks pass. All 1,323 package files copied and verified on the Surface Desktop in `ExtraPane-multiselect-20261107`; its superseded unified-server folder was removed. No live resource session, device capture or driver installation was run. Local old-build cleanup remains subject to the previously reported approval-review block.

2026-11-07 native camera mode negotiation: Windows clients now enumerate native camera modes, prefer normal-motion modes then image size/frame rate, and verify NV12 conversion before offering dimensions and FPS to the server. The selected mode propagates into encoding and emits CAMERA_FORMAT status; explicit CLI diagnostics require all three format arguments. Resource helper build, automatic/explicit/partial configuration checks and synthetic camera-format/inert-lifecycle checks pass without device access. Current camera wire and endpoint limits remain even dimensions up to 3840x2160 with whole-number FPS up to 60; fractional-only modes are reported unsupported, not silently mislabelled. Physical mode negotiation remains to be verified.

2026-11-07 cleanup retry: user renewed deletion request. Automatic approval review again rejected removal of the explicitly named superseded local camera-fix and unified-server packages with ?blocked by policy?; they remain. Surface Desktop inventory contains only the current multi-select package and no running ExtraPane process; preserve it until replacement delivery. Live desktop camera probe reproduces MF_E_SHUTDOWN at IMFVirtualCamera::Start; repair is in progress.

2026-11-07 automatic Windows server preparation: removed Camera setup and Receiving component setup buttons. Start server now checks staged display-package presence, verifies the bundled camera payload against the installed registration, and prepares missing/stale camera and microphone/USB receiver components with normal UAC before opening the listener. Failure, cancellation, missing helpers or a required restart leaves the server stopped. Development display-driver certificate consent remains an explicit installer step; no monitor is allocated during preparation. Shared Windows UI build and offscreen checks pass, including preparation-state controls and absence of setup buttons. Client camera format output is shown with its active resource status. Live installation/readiness and camera delivery verification remain pending.

2026-11-07 camera activation repair: corrected COM activation ownership so releasing IMFActivate does not shut down the returned live media source. Activation-release regression and receiver diagnostic cleanup checks pass. Installed the updated camera component on the desktop for the authorized diagnosis; native decoder/virtual-camera probe now reports prepare OK, start OK and cleanup complete (previously MF_E_SHUTDOWN). Physical Surface camera delivery remains unverified in this increment.

2026-11-07 automatic-server deployment: commit 63392aa packaged as out/windows-auto-server-20261107. Packaged offscreen startup passed locally and on Surface. Archive SHA256, 1,323 files and 120,749,671 extracted bytes verified on Surface Desktop/ExtraPane-auto-server-20261107. Removed its superseded multi-select folder and transfer ZIP after confirming no processes used them. Local old folders remain because automatic approval review rejected their deletion. Native desktop camera start/cleanup now passes; full Surface camera video delivery and combined resources still require verification.

2026-11-07 complete Windows installer: built 2026.11.07-pre.1 with embedded display-driver installer, camera, microphone/USB receiving and optional USB source components. Unified Server + Client replaces camera-only/microphone-only roles; Client only remains. Driver package manifest hashes and versioned packaged startup pass. Inno Setup 6.7.3 compiled both installers. Identical 53,421,288-byte installer copied to desktop and Surface Desktops (SHA256 52fc6e885cdce32da5e64c23de86a9cdc9ec4e3177c478384cab0e2fbb72d9af). Installer execution and end-to-end installation are reserved for the user test; neither installer was run.

2026-11-07 installer pre.2 repair: pre.1 failed certificate import because legacy SideScreen-Driver.cer passed package validation but import hardcoded ExtraPane-Driver.cer. Validation and import now share manifest-based certificate selection. PowerShell parser, both naming variants and actual package-path checks pass; both installers rebuilt. pre.2 copied and SHA256 verified on both Desktops. User installation retry pending; no driver or trust changes executed during repair.

2026-11-07 installer pre.3: microphone setup rejected standard Program Files TrustedInstaller/inherit-only ACLs, then unsigned upstream DLLs. Corrected object-specific ACL checks and pinned all three payload hashes extracted from the hash-verified USBip installer; retain executable Authenticode validation. Policy regression checks reject swapped/unknown/mismatched files. Live backend check, elevated managed component installation and status pass on desktop, preserving existing USB/IP backend and ACLs. Both compiled pre.3 copies verified on Desktops; full installer retry and microphone recording delivery still pending.

2026-11-07 shared USB monitor compatibility: source setup reuses the exact bundled driver or reviewed Oracle-signed VBoxUSBMon7.2.2.170484 (SHA256 ffcad560ac7714460248631aa17238f4d2ad9fd5aed87531b93dab410d23fd87), retaining protected-path and Authenticode checks. Running services must return an exact8-byte ABI5.x version reply; no filter/binding/start/stop calls are issued. Root read-only probe observed ABI5.0 on this desktop. Helper build and isolated unknown-hash/wrong-ABI/malformed-length regression checks pass. Installation/coexistence and actual USB transfer remain unverified.

2026-11-07 managed component consolidation: camera, microphone, USB-source helpers and runtime/UI paths now use Program Files/ExtraPane/Components. Install migration validates legacy ownership, hashes, ACLs and reparse boundaries before publishing new paths; camera COM registration and private USB registry move only after validated new files, old loaded camera/driver cleanup can request restart. Microphone cleanup holds all exact owned files before deletion; independent USB/IP backend remains in its own location. Native helpers/USB runner and shared UI compile; microphone endpoint release build and five isolated tests pass. No installation or migration was executed; actual legacy migration and restart cleanup remain unverified.

2026-11-07 consolidated installer pre.4: source09a7c68 packaged with all managed components under Program Files/ExtraPane. Both installer layers compile; staged offscreen startup, camera/USB payload validation and read-only USB monitor compatibility pass. Identical installer copied to both Desktops with SHA256 3f2efcd597a987b3ffdd9688322e694391d7fd8ced1b844c3c4fbb5d02f4bfa4. Installation/migration/uninstall and actual USB sharing remain unverified. Automatic approval review rejected legacy driver retirement and additional managed-uninstall implementation (generic blocked by policy), so rollback folders and some component-removal work remain; no rejection bypass attempted.
