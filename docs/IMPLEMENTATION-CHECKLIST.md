# Resource-sharing implementation checklist

## Active increment: distinguishable Windows USB device names

- [x] Label generic USB resources with the device-reported product string
  (`DEVPKEY_Device_BusReportedDeviceDesc`) instead of Windows' driver description
  ("USB Composite Device"/"USB Input Device"), falling back to the old name.
- [x] Append meaningful child functions (Keyboard, Mouse, Audio, Camera, Game
  controller, Serial port, Smart card reader...) from present child device nodes;
  vendor-defined/consumer-control HID collections are omitted as noise.
- [x] Prefix a device-supplied manufacturer only when it is not an INF
  placeholder such as "(Standard USB Host Controller)"; append VID:PID only when
  the name is still generic; append the USB port when labels would otherwise be
  identical (for example several "Watchman Dongle" receivers).
- [x] Naming is best effort and never changes availability or the device ID.
  Labels stay within 120 bytes without splitting UTF-8 sequences.
- [ ] Build/deliver matching Windows/Android packages and confirm in the app.

Verification: `extrapane-usb-generic-check` covers function mapping, label
composition, trimming, VID:PID fallback and duplicate disambiguation; storage,
host-backend, recovery and inventory checks still pass. A read-only property
preview on this PC showed, for example, "Razer DeathAdder V3", "Samson C01U",
"Dell Smart Card Reader Keyboard", "Index HMD" and five "Watchman Dongle" devices
where Windows reported only "USB Composite Device"/"USB Input Device". No device
was bound, attached or reconfigured. Storage-disk labels are unchanged.

## Stable release and delivery: 2026.11.09

- [x] Remove verified suffixes and gateway-route description lines from device and
  resource cards. Mutual pairing remains authenticated in both directions.
- [x] Shorten generated PIN rotation/countdowns to 20 seconds on every UI.
  Keep the independent five-attempt-per-minute limit; changing PIN does not reset it.
- [x] Update README, security/acceptance docs and generated release instructions
  for Devices/Resources instead of the legacy Server/Client workflow.
- [x] Fix wildcard-listener regression expectations and cross-platform crypto
  dependency declarations found by the final CI audit.
- [x] Fix POSIX listener restart after active TLS closure; retain exact-binding
  exclusivity and account for BSD wildcard/specific binding semantics in tests.
- [x] Add hash-verified public release-asset download fallbacks for MbedTLS,
  USB/IP and Gradle; explicitly pin the AppImage runtime. Actual API downloads,
  checksums, fresh CMake configuration and fallback/corruption checks passed.
- [x] Final core CI passed all eight jobs on Windows, Linux and macOS
  (`42567602811`); merged PRs 16, 15 and 17 into main.
- [x] Stable Android and Universal macOS packaging passed. Linux compiled but
  encountered another GitHub HTTP504 downloading linuxdeploy; added immutable
  API URLs for the exact existing linuxdeploy/Qt-plugin hashes and verified both.
- [x] Published [The Swap Meet Update, v2026.11.09](https://github.com/EEkebin/ExtraPane/releases/tag/v2026.11.09)
  from `692669d75672cffe46dd2e76c603e21c712ceed1`. Release run
  [42568843977](https://github.com/EEkebin/ExtraPane/actions/runs/42568843977)
  passed Windows, Android, Linux and both macOS architectures, then publication.
  All seven platform assets and SHA256SUMS.txt are present; asset digests agree.
- [x] Copied the matching Windows installer to this Desktop and Surface Desktop
  (`10.0.20.143`), verifying the saved SSH host key and both file checksums.
  Installer SHA256: `a2dfa481581c8699dc104b5c0f7fb0e82806b54ed4d8ca8a959eb3e5e53f94ac`.
- [x] Stream-installed Android and confirmed version `2026.11.09`, code `12201`.
  APK SHA256: `9f920f37afb8a52d3e3f7d67cbeddea8661b0540a6faef85a40ace1bfb782ac3`.
- [x] Deleted the three merged feature branches locally and remotely after
  verifying every branch was an ancestor of main.
- [ ] Physical acceptance remains pending. Neither Windows installer was run;
  no drivers, display layouts or security settings were changed during delivery.

Targeted Qt layout/routing, native auth/rotation/rate-limit, listener and private
store checks pass. Android UI compilation/lint and host checks pass. Full final
core CI passed all jobs. Stable packaging and publication passed. The
preceding pre.116.1 platform build passed all four platforms; its separate core
CI failures were addressed and verified in this increment.

## Active increment: pairing credentials and mutual resource availability

- [x] Rotate generated codes every minute without interrupting paired devices.
- [x] Add protected fixed custom PIN/password settings and neutral masked prompts.
- [x] Display circular countdowns on desktop, Android and macOS.
- [x] Distinguish incorrect credential confirmation from transport errors.
- [x] Make one pairing provide resource availability in both directions, including
  when a reverse direct/discovery route is absent.
- [x] Verify authentication boundaries and UI transitions.
- [x] Build and deliver matching Windows/Android packages.
- [ ] Complete all-platform CI and physical acceptance for this revision.

The user clarified that the pairing field should hide entered characters, not
that typing freezes. Custom secrets remain fixed; generated credentials rotate.
No further physical connections or driver changes are part of isolated checks.

Verification: auth checks cover generated-code rotation at 59.999/60 seconds,
wrong/custom/Unicode passwords, expiry during confirmation, strict UTF-8 and size
limits, rate-limit reset and settings-change non-bypass. Store checks cover
private persistence, corruption rejection and Windows DPAPI plaintext absence.
Service checks retain active grants across credential changes. Mutual-resource
and existing gateway regressions pass without reverse discovery/address hints.
Qt app, offscreen prompt/countdown/masking and controller persistence/reset checks
pass. Android's 16 host suites, both native ABIs, Java compilation and lint pass.
macOS controls are implemented; full platform compilation remains for CI.

Delivery: **2026.11.09-pre.116.1**, clean source
`f06fbfdacc3e812ea05cba3f9a7064c1b44cc27c`. Windows installer/portable extraction,
isolated UI and embedded component checks pass. Matching installer copied and
SHA-256 verified on this Desktop and Surface Desktop (`10.0.20.143`), without
running either installer. Android release build/lint, host checks, both ABIs,
pinned signer and alignment checks pass; stream installation succeeded.

- Windows installer SHA-256:
  `ce892c335558265c6a76b2d790c54be5242fbc9f9d8b5a6f7efa402d049b17af`
- Android APK SHA-256:
  `5f6d37d5da79f0bdfbdf17ae3b8f078a865d88dcc5495185eda907ea4c787c0c`
- [All-platform CI run](https://github.com/EEkebin/ExtraPane/actions/runs/42564669305)
  remains in progress at delivery. User acceptance of new pairing and mutual
  resources awaits both Windows installations being updated.

## Active increment: USB routing through the installed peer workspace

- [x] Update unified receiver routing to recognize the current kind-bound USB
  protocol for both storage and generic devices.
- [x] Surface native failure details instead of silently returning the power
  button to green after a premature connection toast.
- [x] Verify actual protocol greetings through the shared routing check.
- [x] Build and deliver matching Windows/Android packages.

User acceptance of pre.114.1 found that USB connects briefly in the peer workspace
then ends before the mouse leaves the Surface. The shared receiver still matched
the version-3 USB prefix, while the native helper sends version 4 with a device
kind. The previous direct-helper physical test bypassed that router. Read-only
inspection confirms both installed services are pre.114.1 and the Surface mouse
and HID interfaces remain healthy. This is an integration defect, not successful
installed-app USB acceptance.

Regression evidence: actual storage/generic greeting definitions pass through
session-prefix routing; malformed kinds, old/future versions, reserved bytes and
reply frames are rejected. The updated standalone routing check passes.
The previous all-platform run also found an Objective-C++ ivar initializer syntax
error on macOS; the declaration is corrected and an isolated syntax check passes.
Full macOS verification is queued for the next source build.
Native runtime, authenticated loopback failure propagation/redaction, Qt startup
and UI checks pass. Provider errors reach the requesting peer through the existing
rejection message; native credentials are withheld from diagnostics. The initial
toast says "Resource connection requested" rather than claiming device readiness.

Delivery: **2026.11.09-pre.115.1**, clean source
`d135fb4749960197786407be25d54ea0e2e369a9`. Windows installer/portable extraction,
UI startup and embedded component checks pass. Installer copied and SHA-256
verified on both this Desktop and Surface Desktop (`10.0.20.143`); neither was
executed. Android's 16 host checks, release build/lint, both ABIs, pinned signer
and alignment checks pass. APK stream-installed; version/code `11501` confirmed.

- Windows installer SHA-256:
  `07ea3decbd92fbe303e38fa1e6fdc499def9dbd5d055fc0fa778c999c9d97157`
- Android APK SHA-256:
  `665565dbee20be54fec18a1eb34765f0d04dcfe7f8703f093dbbb2eab8121b11`
- [All-platform CI](https://github.com/EEkebin/ExtraPane/actions/runs/42563269836)
  completed successfully on Windows, Linux, Android and both macOS architectures.
  The prior macOS syntax failure is resolved. Installed-app USB
  acceptance awaits the user's retry on both updated Windows installations.

## Active increment: automatic hardware updates and composite USB

- [x] Refresh local resources on hardware changes and notify paired devices.
- [x] Preserve unrelated and actively owned sessions across inventory refreshes.
- [x] Remove manual resource refresh controls.
- [x] Validate complete USB configuration/interface descriptors for composite
  HID/audio devices; report Bluetooth and unsupported functions clearly.
- [x] Add all-address listener and preset/custom address/port controls, preserving
  active resources during rebind and reporting actual state after failed rollback.
- [x] Verify isolated transitions and the authorized Surface USB mouse return.
- [x] Build and deliver matching Windows/Android packages.
- [ ] Complete all-platform CI for this source revision.

Windows uses debounced device notifications and periodic reconciliation; Linux
and macOS reconcile periodically, and Android uses hardware callbacks plus a
fallback timer. Catalog changes notify paired peers; older peers retain their
polling fallback. Busy active sources keep their advertised identity until native
session termination or explicit sharing/permission withdrawal.

Isolated evidence: native USB descriptor/ownership/return checks, Qt startup and
offscreen UI checks, and TLS catalog-change/rebind tests pass. Rebind tests retain
live resource bytes through address changes and verify occupied-port rollback.
Android's 16 host suites, Java compilation, lint and native builds for both ABIs
pass. Changing a listener can briefly pause Android controls (up to about four
seconds while an existing handshake ends); active streams continue. Physical media hotplug
and other operating systems' hardware behavior remain pending.

The first authorized Surface mouse test exposed premature exporter shutdown:
destination eject succeeded but source restart was interrupted, leaving the exact
VBox stub at Problem 21. Restarting that verified stub restored the mouse, and the
user confirmed local operation. The fix waits for normal source return and keeps
an exact, durable ownership record for interrupted cleanup.

The repeat test with VID 3151/PID 3020 passed import, destination eject and automatic
source return. The original composite device and HID interfaces were present and
healthy afterward, with no remaining owned binding, stub or recovery lease. No
manual repair was needed. This verifies one mouse's transfer/return lifecycle;
headset compatibility, application input behavior and physical crash/unplug
recovery are not broadly validated. Bluetooth forwarding is deferred at the
user's request. No Windows installer or new driver was run during this test.

Delivery: **2026.11.09-pre.114.1**, built from clean source
`dd9f0e20c1af0f6129a5af82bab8be75f5b3229b`. Windows installer and portable
extraction checks passed, including embedded components and isolated Qt startup.
The installer was copied to this Desktop and the Surface Desktop at
`10.0.20.143`; both copies were verified. Neither installer was executed.
Android release build/lint, signer, both native ABIs and alignment checks passed.
The APK was stream-installed on the authorized Pixel, and installed version
`2026.11.09-pre.114.1` / code `11401` was confirmed.

- Windows installer SHA-256:
  `e9e2ca20712768660d3f4b04fd7e3a2a76e1288cd54f2a4d2ce5fea3b0812247`
- Android APK SHA-256:
  `298aa43fe80141cd36a2db1890ae3c0adbd11a7d7d3d33ad213dce4c98117337`
- [All-platform CI run](https://github.com/EEkebin/ExtraPane/actions/runs/42562223287)
  was still running at delivery. Full installed-UI hardware acceptance remains
  pending; the successful mouse lifecycle test used the updated native helper.

## Active increment: generic USB device forwarding

- [x] Audit pinned USB/IP transfer support and separate non-storage ownership from
  storage quarantine; preserve prior work on `feat/device-discovery` and develop
  this increment on `feat/usb-device-forwarding`.
- [x] Add conservative physical HID/audio inventory and explicit unavailable reasons.
- [x] Implement exact owned non-storage export, import, return and abandoned-binding
  cleanup without disk recovery requirements or forced driver replacement.
- [x] Integrate USB resource selection and versioned kind negotiation; reject unsafe
  storage/generic fallback and incompatible native helpers.
- [x] Verify synthetic lifecycle, protocol, ownership and isolated builds; update docs.
- [x] Build and deliver matching Windows/Android packages after verification.
- [ ] With user permission, verify actual USB mouse/keyboard and headset transfer,
  release, disconnect, unplug and crash recovery on physical devices.

Initial pre.113 implementation evidence: pinned backends implement interrupt and
isochronous transfers, but that alone was not physical compatibility evidence.
No USB device was bound, detached or imported before that delivery; the follow-up
mouse result is recorded above. Raw USB on other platforms remains
unsupported. Existing storage safety rules remain separate and unchanged.

Initial isolated evidence: native USB helper and Qt app compile; Qt offscreen
checks pass. Offer tests cover legacy-version rejection, malformed kind/reserved
fields and receipt identity. Generic policy/lifecycle checks exercise selection
changes, binding/start/release failures and exact import-port parsing. Tunnel
checks cover bidirectional flow and idle pacing. Patched pinned exporter AOT
compilation succeeded without running it. Guarded installer migration compiled
and policy checks passed: only the exact previous payload can upgrade, unchanged
drivers and inactive ownership records are preserved, active exporters block
replacement, and a verified interrupted replacement can resume. Android's 14 host
checks, Java compilation and lint passed. Final package verification is pending.
Generic receiver uses exact virtual-port to
PnP driver-key mapping rather than the pinned driver's broken serial override;
the physical mapping remains an acceptance gate. Composite devices are excluded.

Delivered **2026.11.09-pre.113.1** from clean source
`a84ba5b357ee1b7d77d439519942ae8e91c43332` on `feat/usb-device-forwarding`
(draft PR 16 into `feat/device-discovery`; neither branch merged to main).
Windows installer copied to this Desktop and the Surface Desktop with matching
SHA256 `db830ecd478c10a816c7e626bc9509c0210bbfa530f30a35601b77389a795798`;
saved Surface SSH identity verified. Extracted UI, component checks, exact versions,
clean metadata and updated USB backend/corresponding-source hashes passed.
Android streamed install succeeded and versionCode 11301/versionName confirmed;
APK SHA256 `399e0ffd9798fc3b387d854364fbf14e27c16021d6e3cf0a87639b847dc017ce`.
Signing identity, release flags, both ABIs and 16-KB alignment were verified.
Hosted all-platform run `42553036819` is still in progress. No Windows installer
was executed, and no physical USB forwarding test was performed. Android raw USB
support remains unavailable; its package is the matching-version companion build.

## Active increment: pairing dialog labels

- [x] Show the selected device name in Pair with NAME; smaller address/port below.
- [x] Add Pairing Code label and XXXXXX placeholder across desktop and Android.
- [x] Preserve frozen-target validation, cancellation, remembered-peer and gateway
  authentication behavior; render discovered names as plain text.
- [x] Verify layout and build/deliver matching Windows and Android packages.

Qt offscreen checks pass, including named/unknown devices, plain-text rendering,
frozen endpoints, validation and cancellation. Focused and invalid-code screenshots
reviewed; XXXXXX stays inside the field without a floating label/border overlap.
Android's 14 host checks and integrated compile/lint passed. Native macOS source
updated; native compilation remains for CI. No physical pairing tests were run.

Delivered **2026.11.09-pre.112.1** from clean
`b42f57851b0c737bb0c6b7827f900b111238a743`. Verified Windows app/native versions,
clean metadata, extracted UI, component payloads and checksums. Installer copied
to both Desktops; saved Surface SSH identity and destination SHA256 verified:
`f095f560749ebf89d0a64b8d06f7db1c23e0eb7d167fa61e68ab142a4b83cdd4`.
Android streamed install succeeded; version 11201 confirmed, with pinned signer,
release settings, both ABIs and 16-KB alignment checked. APK SHA256:
`29a1504eab8fe34083fa7ef256fd4af771b63d606c1633de586779a5827b08da`.
Hosted run `42549268217` is still building all platforms. Windows installers were
not run; no driver or system-setting changes were made.

## Active increment: local device card and resource restrictions

- [x] Put This device first with a collapsed hostname/pairing-code card.
- [x] Start Resources device cards and type groups collapsed; preserve expansion
  across inventory/catalog refreshes.
- [x] Add persistent per-resource Share permission switches, independent of
  active-use power controls; withdrawal stops only affected sessions.
- [x] Verify shared desktop and Android with isolated checks; native macOS source
  reviewed, with compilation/physical validation pending.
- [x] Build and deliver matching Windows/Android packages; do not run Windows
  installers or physical media tests as an incidental verification step.

Verification: Qt offscreen layout and resource routing checks passed, including
default collapse and expansion preservation across model refresh/reconnection.
Default Devices screenshot reviewed. Controller checks cover opt-outs across
restart, inventory disappearance/reappearance, offline/unavailable preferences,
sibling isolation and explicit re-enable. Core policy withdrawal check and Windows
app compilation passed. Android's 14 host suites and final integrated compile/lint
passed. Android audio restrictions use type/address/product identity rather than
transient route numbers; indistinguishable routes share a conservative opt-out.
Windows named monitor presets retain their existing DISPLAYn IDs, which can change
after display-topology changes; a newly identified resource defaults to allowed.
No physical media, driver installation or system-setting changes were performed.

Delivery: **2026.11.09-pre.111.1** from clean
`c0e459cef16a7ed00ec5d43245fb589bdedcbef8`. Windows installer copied to both
Desktops with matching SHA256 and saved Surface SSH identity verified. Installer
SHA256: `b21b582ae982673d7c94c629bf4b83a98ed83822e82d04b53eb4dedc6c248fdd`.
Android streamed install succeeded; version 11101 confirmed. APK SHA256:
`033c2cade51b62ede3f37e560d15e1d71b8d98bb61dd4db62d9995bc868d460a`.
Exact Windows app/native versions, clean metadata, extracted portable UI and
component payload checks passed. Android signer, release flags, both ABIs and
16-KB alignment passed. Hosted run `42547801425` remains in progress; native macOS
compilation and physical acceptance are pending. Neither Windows installer was run.

This is the working checklist for the shared ExtraPane app and device-sharing
features. Update it after every completed increment, in the same change as the
implementation. A checked implementation item does not imply physical-device or
release verification. Keep this file consistent with RESOURCE-SHARING.md.

## Current status ? 2026-11-08

This snapshot supersedes the historical checklist and dated development entries
below. Implemented means code exists and its stated checks passed; it does not
mean every device combination has been validated.

### Implemented and checked

- [x] Windows/Linux shared Qt interface; native Android and macOS frontends.
- [x] Windows server accepts concurrent display, camera, microphone and USB resources on one listener.
- [x] Repeatable resource rows and single-server/per-resource destinations on the shared desktop interface.
- [x] Green/red power actions; idle Disconnect all hidden; last-resource admission cleanup.
- [x] Collapsible resource groups and hamburger navigation; Android uses a sliding drawer.
- [x] ExtraPane display-window/monitor branding; stable legacy upgrade identities preserved.
- [x] Windows camera and microphone source/virtual receiving components.
- [x] Android camera/microphone sources; Linux sources, fanout and receiving integrations; macOS sources.
- [x] Windows microphone jitter reserve, bounded refill/overflow and diagnostics; 14 isolated Rust checks passed.
- [x] Windows USB forwarding and bounded ownership return implementation.
- [x] Surface orphaned-driver uninstall helper repaired; removal precheck passes.
- [x] Unified Windows installer and portable package; Linux AppImage, Android APK, macOS Universal ZIP/DMG builds.
- [x] Matching 2026.11.09-pre.109.1 Windows installers delivered to both desktops; Android APK installed and version verified.

### Current validation and remaining work

- [x] pre.106.1 hosted build passed Windows, Linux AppImage, Android and both macOS architectures; release publication was skipped.
- [x] Complete the monitor-row/bilateral-Forget increment and deliver matching pre.107.1 Windows/Android packages.
- [ ] Finish hosted pre.107.1 cross-platform validation; physical input and upgrade acceptance remain pending.
- [ ] Retest phone microphone while three Surface display streams are active; confirm application recording quality.
- [ ] Verify successful Surface uninstall with the repaired helper (precheck alone is not a completed uninstall).
- [ ] Make the microSD USB reader enumerate and transfer reliably on the receiving PC; verify eject, return, reconnect and uninstall.
- [ ] Validate camera/microphone delivery and concurrent multi-client behavior in target applications across platforms.
- [ ] Implement clock-paced Linux microphone jitter buffering with safe mute invalidation.
- [ ] Complete Linux/Android USB support and remaining native/shared UI parity.
- [ ] Complete macOS virtual camera/microphone receiving, USB, same-source fanout and multiple client display windows.
- [ ] Physical Linux touch/pen, long-session clock drift, sustained load and broader hardware coverage.
- [ ] Production signing/distribution and full upgrade/uninstall acceptance across platforms.

Speakers now have a Windows server endpoint and Windows/Android client implementation;
see [Speakers development](SPEAKERS.md). Physical playback verification is pending.

Deferred: iOS, a future product rename, automatic system-audio mirroring and
Miracast/Windows+K. See the platform guides for their exact supported features.

## Historical baseline and implementation journal

The older milestone checkboxes below record their original checkpoint. Use the
current snapshot above and dated evidence for today's status.

### Original target

2026-11-07 Linux fanout lifecycle review: extracted the native capture registry into a portable helper with weak entry pruning and per-device teardown locks. Fake capture checks passed for create-once, rejected mode preserving existing capture, final subscriber stop, concurrent final release/new acquire and unrelated-device progress during blocked teardown. Camera mirror writes now occur only when the subscriber's desired state changes. No live capture validation performed.

2026-11-07 Linux same-source fanout increment: one native capture/encoder per physical camera or microphone feeds independent bounded recipient queues. Slow recipients fail independently; camera joins wait for IDR with SPS/PPS, source-wide live mirror and recipient-specific microphone mute are preserved. Last-subscriber teardown and new acquisition are serialized per physical device to retain exact source ownership. Portable fanout checks passed with MSVC C++20 (keyframe/config, packet/byte limits and recipient isolation); native Linux compilation and live multi-server delivery remain pending. No device tests or installation performed.

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

2026-11-07 Surface setup pre.5: diagnosed cold Authenticode chain cache on Surface. Normal Windows signature verification succeeds, while cache-only setup initially failed. Setup helpers now allow certificate-chain retrieval without relaxing trust or pinned payload hashes. Surface managed microphone install succeeded with3010 (restart required); USB source install succeeded0 with no device binding. Both status checks report installed. pre.5 compiled and copied checksum-verified to both Desktops; packaged startup passes. Temporary diagnostic executables removed from Surface Desktop. Full app installation retry and sharing tests still pending.

2026-11-07 virtual-camera Frame Server contract repair: sample and start-event timestamps now use receiver-local MFGetSystemTime/QPC units rather than sender-relative stream timestamps. Separate activation objects copy their attributes into the source as required by the Frame Server contract. Native synthetic source and unregistered DLL activation/pipe/lifetime checks pass, covering local timestamp ranges, newest-frame bytes and inherited device attributes. Fresh CameraSetup payload compiled. These checks do not establish Windows Settings/real consumer success; live validation remains pending. Reference: https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/frame-server-custom-media-source (Sample Allocation and IMFActivate).

The live consumer probe subsequently reported MF_E_INVALID_STATE_TRANSITION. Source Start now tolerates a valid repeated start without recreating the pipe subscriber, and IMFMediaStream2 supports Running/Paused/Running with bounded pending delivery. Source-level Pause remains unsupported as documented. Synthetic state and unregistered DLL pipe regressions pass; updated CameraSetup rebuilt for the pending live retry.

2026-11-07 camera delivery pre.6: live SourceReader reproduced MF_E_INVALID_STATE_TRANSITION at first ReadSample while camera1920x1440@30 was connected. Fixed stream pause/resume and repeated source Start, source attributes and local QPC timestamps; synthetic source and DLL pipe tests pass. Updated desktop camera component and bundled helper after user disconnected. pre.6 compiled and copied SHA256-verified to both Desktops. Live delivery retest remains pending reconnect; no claim of preview success.

2026-11-07 live camera delivery verified: after pre.6 registration, Windows FrameServer still loaded old91f7e84 DLL while host loaded new0c3cc5d DLL, reproducing old state error. Restarted FrameServer during authorized camera diagnosis. SourceReader then successfully read two real4,147,200-byte NV12 frames (1920x1440) from connected Surface camera; first read was expected stream tick. Windows Settings visual confirmation remains pending. Camera component upgrade must account for FrameServer retaining old DLL until service restart or Windows restart.

2026-11-07 live camera mirror implementation: Windows source helper accepts MIRROR 1/0 through existing private stdin; camera worker samples atomic state once per frame and horizontally mirrors NV12 before H264 encoding without restarting capture/transport. Asymmetric luma/chroma tests cover even widths with odd/even UV-pair counts, invalid geometry and double-flip restoration. Native resource helper/host builds and camera-format checks pass. User-visible toggle and automatic camera/microphone/USB dropdown inventory are being integrated; live mirroring still needs user verification.

2026-11-07 mirror/device UI increment complete: Mirror Camera checkbox sends initial/live control without reconnect or microphone changes. Device dropdown open loads camera, microphone and USB inventories, including initially empty lists; scanning guards avoid duplicate requests and valid selection is retained. Separate refresh and USB setup buttons/API removed; USB recovery remains. Shared UI build and offscreen popup/mirror tests pass without devices.

2026-11-07 default unified uninstall: main uninstaller invokes USB source, microphone, display and camera cleanup before app-file removal, then resets current-account settings/known pairing. UI toggles removed; silent uninstall also resets. Failures retain helpers and restart codes propagate. Mic removal validates known files and removes its managed folder. Installer compilation and syntax checks pass; no live uninstall run. Renewed explicit user folder-cleanup request still received automatic approval rejection for legacy-driver/outer-folder cleanup edits (blocked by policy); those changes were not applied.

2026-11-07 USB source uninstall increment: removes hash-verified private files/folders and registrations, plus only an owned monitor service and unused owned INF package. Independent USB/IP/VirtualBox consumers, pending recovery and changed ownership block destructive removal. Native helper build, synthetic ownership policy and embedded payload checks pass. No live uninstall run; legacy display and outer-folder cleanup restrictions remain as recorded above.

2026-11-07 pre.7 packaged and delivered: fresh native helper payloads and shared UI staged, driver/main installers compiled, packaged offscreen startup passes. Installer SHA256 dd5fc76c04679b4ce0143590e4e05fa92fe4deafb84f44f3cf1deed343e0a461 verified on desktop and Surface Desktops. Includes live mirror, popup inventory loading and default managed-component/settings uninstall. Neither installer nor uninstaller was run; live mirror and full removal remain user validation. Blocked legacy/outer-folder cleanup remains incomplete.

2026-11-07 persistent resource-control protocol: Windows session helper now supports an acknowledged idle connection, heartbeat and on-demand ADD commands without allocating a display/media resource. Authenticated control leases issue fresh per-kind, one-use tickets and survive original code expiry/rotation; pending tickets expire, are replaced per kind and are revoked when the control socket closes. Automatic LAN replies remain explicitly unauthenticated. Four isolated admission/config checks pass, including replay, wrong-kind, expiry, rotation and close/revoke cases; native host/session/resource helpers compile. No live device or socket interoperability test was run for this increment. UI must stop independent child streams when the control helper exits.

2026-11-07 microphone rejection reproduced and corrected: added read-only --check-backend; unchanged runtime rejected installed Components ACL as writable by untrusted user. Runtime now matches installer servicing policy (exact TrustedInstaller SID; inherit-only entries ignored on current object; individual payload checks retained). Same read-only check now returns MICROPHONE_BACKEND_OK. Five Rust checks and native builds pass. Server retains actual local startup error, while client receives an actionable generic rejection. No microphone attach or audio delivery test performed.

2026-11-07 USB uninstall shared-driver regression: pre.7 rejected private USB cleanup whenever an independent USB consumer existed and ExtraPane had staged an INF, even though the external monitor was correctly preserved. Removal now preserves that shared DriverStore package while permitting verified private payload/registration removal. Owned monitor binaries still needed by external consumers remain a blocking condition. Added mutation-free --check-removal; on this desktop it reports monitor=preserve, inf=preserve-shared, private-payload=remove-if-not-busy. Native helper rebuilt; policy and embedded-payload checks pass. No actual uninstall, driver/service mutation or USB attachment was performed.

2026-11-07 Windows live resource selection: Connect accepts no selections and waits for native control acknowledgement. Check/uncheck starts/stops independent resources; idle source selection remains usable. Disconnect all clears every selection. Failures uncheck only the failed resource without retry loops; loss of control stops owned resources. Removed per-resource Stop buttons. Pre.8 UI build, offscreen model/popup checks and fake-child controller lifecycle checks pass without capture, sockets, drivers or settings writes. Live interoperability remains pending.

2026-11-07 direct display-driver ownership: main installer stages and logs raw display manager/package files, installs the driver after its own repair/uninstall is available, and removes it directly on uninstall. It excludes the standalone DriverSetup executable from the installed app. Verified former display uninstall registration is retired after ownership validation. Driver failure returns nonzero, reports captured diagnostics and suppresses launch. Former child managed state/log cleanup rules now belong to main uninstall. Legacy old uninstaller files/folders remain untouched. Inno compilation and PowerShell/Python syntax checks pass; no installation/uninstall executed. Disposable-runner integration definitions updated but not run on either personal PC.

2026-11-07 pre.8 delivery: compiled main installer from fresh shared/native/Rust staging; no nested display installer in installed payload. Packaged offscreen startup, microphone --check-backend and USB --check-removal pass on desktop without device attachment or removal. SHA256 37dd78c7573b0a49b76148a7ffd3f49238d121552bf628029024294f4f325cb2 verified after copy to both desktop and Surface Desktops. User should upgrade over pre.7 before retrying uninstall; both endpoints need matching pre.8 for persistent control. Live audio, resource toggle interoperability and real upgrade/uninstall remain user validation.

2026-11-07 Surface microSD recovery incident: sharing left an offline journal before binding/export. Installed pre.8 recovery helper was stale and still used the legacy component path; rebuilding its target fixed that path error. Guarded recovery then refused a fingerprint mismatch without changing disk state. Same reader/card unplug/replug restored local D: exFAT access; read-only Windows status reports Online/Healthy. Inspection shows offline layout192 bytes (whole-disk pseudo partition), reconnected layout624 bytes (real MBR partition offset1048576). Existing v1 journal hash still differs; no identity bypass, record deletion, partition changes or new export performed. Added pending-journal inventory/pre-connect gates, stage-specific errors and read-only --inspect. USB journal/backend regression checks pass. Stable versioned offline media identity and this reader's export remain unfinished; do not repeat live export yet.

2026-11-07 packaging freshness: development package script now builds each included native helper target, explicitly including UsbRecovery, and the shared UI before copying files. It validates source/build roots and uses the discovered VS toolchain with two workers. This addresses omitted-target stale binaries; Rust endpoint remains explicitly prebuilt separately.

2026-11-07 pre.9 packaged: revised staging rebuilt every native/UI target, including UsbRecovery; packaged UI startup passes and recovery helper hash matches fresh build. Fixed redirected Qt-warning handling to use native exit status. Inno build passes. Installer SHA256 da901559d5fd1a0cd780f4528a3dc6d1757d566225aea77dcbca520006b9a895 verified on both Desktops. No installer run, no USB export retried. Card confirmed Online/Healthy D: exFAT after user reconnected it; old recovery journal preserved because identity mismatch persists. This release guards retries and repairs stale tooling; it does not complete microSD-reader export support. Automatic approval blocked deletion of this turn's incomplete out/windows-usb-recovery-20261107 staging folder (blocked by policy); left it and used a new final stage.

2026-11-07 graceful USB return increment: framed v2 return/busy controls and shared client pending-disconnect handling implemented. Isolated core usb-tunnel/resource-session-route checks pass (2/2), including bounded direction-checked controls separate from payload. Shared UI build and fake-child UI/controller checks pass (2/2), including busy veto and retry. Added guarded already-online pre-export reconciliation before uninstall. Stable offline identity and native automatic restoration integration are still in progress; no new USB export, install, disk mutation or live return test performed.

2026-11-07 automatic USB return completed for pre.10 test build: v2 raw MBR/GPT identity avoids offline synthetic layouts, with strict legacy v1 restoration retained. Backend persists verified eject before source reappearance, releases only its binding, retries re-enumeration/restore within five seconds and reports actual restoration separately. Native build and journal/storage/backend checks pass (3/3); core framing/route checks pass (2/2); shared UI/controller fake-child checks pass (2/2), including forced control loss and post-return inventory refresh. Added missing storage-to-recovery CMake link. Packaged offscreen UI startup and Inno compile pass. No live export or installation performed.

2026-11-07 Surface stale record resolved: rebuilt --reconcile-online initially crashed before retirement; process-lifetime COM initialization and inventory snapshot eliminated the observed failure. Hash-verified helper then returned retired=1, journals=[], exit0. Disk1 stayed Online and D: stayed exFAT/Healthy. Only the never-exported already-online journal was removed; no source disk attributes, partitions or files were modified. Actual microSD export/return, busy-file behavior and pre.10 upgrade/uninstall remain physical validation gates.

2026-11-07 pre.10 delivery: complete installer rebuilt with native/UI updates and current USB recovery documentation. SHA256 6a9155df4945c364b6d02a2197797426328402018c6845ea4a20606332bb41a9 verified on both desktop and Surface Desktops. Source implementation commit 1fd60c4. Installer was copied only, not executed. Upgrade both endpoints together before testing USB; normal uncheck requests safe return, busy veto keeps USB shared, and uncertain transport loss still retains recovery safeguards.

2026-11-07 pre.10 physical failure: user reported the imported card never appeared; uncheck did not complete, then server restart and reader replug left v2 bound recovery. Read-only inspection: no source USB/exporter process, no destination imported ports/disks, source disk1 Offline, exact private binding 959ddb29-e20a-49ef-ac2c-98398b3ad657 unoccupied. Version-aware raw metadata matches saved fingerprint 7cea514037834ef2a5d2505ca9914b5f1e51d60539f34be2fe04732eab2b8720. Existing v2 bound does not prove whether export started, so no journal deletion, unbind or online action performed. Found GUID normalization mismatch in normal unbind and misleading readiness/busy messages; fixes in progress. Actual initial import failure remains unidentified because previous subprocess diagnostics were discarded.

2026-11-07 USB diagnostics/recovery repair: normalized persisted binding GUIDs before ownership comparisons (bare vs braced IDs could prevent unbind). Added exact adoption of a verified ejected binding for recovery after process exit. Added operator-only interrupted return for exact v2 bound records, with original-record/acknowledgment audit; never invoked automatically or represented as a clean eject. Native build and recovery/help/backend checks pass (3/3). EPUT/EPUS v3 reports exact-disk readiness and categorized return failure; core framing/route checks pass (2/2). Shared UI/controller tests pass (2/2), including retained bounded/redacted USB logs and hiding redundant Disconnected text only for unchecked idle resources. User approval for current interrupted recovery is pending; no live sharing or recovery mutation has been run in this increment.

2026-11-07 pre.11 delivery: installer and packaged offscreen UI checks pass. SHA256 3280e5f3dee8569006ca134e36c63be9e33d45a80b79f5846760530ba7cadeee verified on both Desktops. Source commit dc3c414. Neither installer nor live USB sharing was run. Current Surface v2 bound record is unchanged; explicit interrupted-return approval remains pending. Initial physical import failure remains unconfirmed; this build repairs GUID cleanup and supplies retained readiness/failure diagnostics for the next authorized test.

2026-11-07 requested multi-destination resource rows: saved design in MULTIPLE-RESOURCE-CONNECTIONS.md. User selected optional Single server top-level destination controls and per-row destinations in Multiple servers mode, plus same-source camera/microphone delivery to several servers where supported. Existing fixed-kind client ownership and singleton microphone endpoint are implementation blockers, not completed features. Display window behavior remains pending. No runtime changes or live tests for this request yet.

2026-11-07 multi-connection increment: implemented shared-window tray double-click restore/focus for both roles, and tray retention checks for server or aggregate client activity. Tray Exit requests normal client disconnect/server stop and keeps the window visible while USB return is pending. Native camera/microphone source brokers and four independent microphone receiver slots compile; isolated fan-out/media and microphone checks pass. Repeatable connection UI/runtime, exact custom display modes and automatic editable-focus keyboard support remain in progress. No live devices, installations or display changes were exercised.

2026-11-07 multi-connection implementation: repeatable Windows client rows now support Single server/per-row destinations, per-destination serialized admission, empty admitted sessions, independent receiver windows, and shared native camera/microphone sources. Server microphones now use four independent endpoint slots. Camera source fanout is bounded to eight recipients, microphone to sixteen; USB receiver remains one and each source remains exclusive. Shared model/UI/lifecycle checks pass (3/3), including failure isolation and USB busy-return retry. Native media/microphone checks pass; Rust endpoint checks pass (8/8). Driver 0.8 builds and validates without warnings; custom mode descriptor/property and video/keyboard policy tests pass (2/2). Exact custom mode requests preserve arrangement and reject unsupported modes rather than substitute silently. Touch-accessible controls and manual/automatic accessibility On-Screen Keyboard are implemented; full TSF/IME is not claimed. Tray X retains server/client work; double-click restores/focuses; Exit requests graceful return. No live media, driver installation, display change or USB recovery performed. Installer packaging and physical acceptance remain pending.

2026-11-07 pre.12 delivery: complete Windows installer includes multi-connection runtime, capture fanout, tray restore/lifetime, touch-accessible preview controls/OSK focus support and driver0.8 custom modes. Packaged offscreen UI and Inno compile pass. Installer SHA256 4c36cf9044a6f0bce6bf8ac9cc847a160d9d8dec577a1665fb9277a3a8583430 verified on desktop and Surface Desktops. Implementation commit57f2935. Copied only; no installation or device/media/display tests run. Surface interrupted USB record remains unchanged and operator recovery approval is still pending. New features are ready for physical acceptance after prerequisite setup/recovery is resolved; cross-platform repeatable-row migration and full modern touch keyboard/IME remain outside this Windows increment.

2026-11-07 installer lifecycle repair: exact complete USB source backend reuse now verifies payload/manifest, protected ownership/version, exact registration and monitor compatibility without requiring empty bindings/journals or the missing card. Main uninstall invokes guarded --prepare-uninstall and source --check-removal, then removes managed components; failures use a logged normal dialog plus Inno silent Abort, retaining helpers. Installed Apps display name is ExtraPane; version remains separate metadata. UsbHostSetup build and synthetic verification-dispatch/monitor policy plus embedded payload checks pass (2/2, 0.03s). No live install/uninstall, driver changes or storage operations performed by this increment; native cleanup helper and full installer packaging are coordinated separately.

2026-11-07 Surface uninstall blocker cleared: user explicitly requested releasing absent USB ownership without reconnecting media. Hash-verified helper --release-absent retired journal e99f7b0e408b849d89ba9e5218509d152f1c3c1d7eab4b0f27dfd74c580ad5e7 after protected audit and exact inactive binding 959ddb29-e20a-49ef-ac2c-98398b3ad657 release. Result diskTouched=false, cleanEjectClaimed=false, auditRetained=true; active journals empty and private USB state no longer lists reader binding. Installed old --reconcile-online now exits0. No card connected, no disk attributes/filesystems changed and no uninstall executed. Surface existing display name was normalized; permanent installer AppName/UninstallDisplayName now share ProductName=ExtraPane, while DisplayVersion remains separate.

2026-11-07 USB lifecycle revision: compatible installer upgrades reuse only exactly verified managed backend bytes/ownership/registration and preserve pending state. Uninstall prepares guarded absent or exact present-source cleanup and uses readable error dialogs instead of raw Pascal exception locations. Source stop/disconnect tries receiver eject, bounds the wait to10s, then ends its own exporter, releases exact ownership and restores verified prior local state with an interrupted-write warning. Unplugged source ownership uses absent archive without claiming the card is online. Receiver cleanup targets its owned import tuple only. Shared UI keeps cleanup helpers alive, preserves interrupted-return warnings and leaves checkboxes unchecked. Three isolated model/UI/controller tests pass. Actual transfers remain untested; killing the USB helper itself or power loss still lacks a separate crash guardian. Archived absent records do not automatically restore an offline bit on a later replug.

2026-11-07 pre.13 delivery: final native USB/helper/service builds, source backend fake lifecycle and recovery policy tests passed, including guarded unplugged ownership release without an online claim. Shared model/UI/controller checks3/3 passed; installer backend reuse/payload checks2/2 passed. Packaged offscreen app startup and Inno compile passed. Installer SHA256 001a9ff6478841d8ed7d24323e234c37a952dd1f5cbe4ef939b10d34a09e255d verified on both desktop and Surface Desktops. Implementation commit5f9c880. Copied only; no installer executed and no live transfer exercised. The earlier Surface absent-state repair is separately documented above. Full hard-kill/power-loss recovery remains a limitation, not claimed as tested or implemented.

2026-11-07 display connection regression fixed: unified host constructed an IP-slot-N layout name that its numeric/dot/hyphen validator rejected before monitor creation; client consequently saw TLS peer close. Shared select_client_layout_profile now constructs IP-N keys, tested through the same validator for all four slots without touching displays/registry. Display defaults to monitor-free custom1920x1080@60; Match local monitor is optional and custom launches never pass stale monitor IDs. Removed Connect to server button; checking a resource connects it, Disconnect all remains. Native display property/profile test passes, shared model/UI/controller tests3/3 pass including actual helper argument assertions and unexpected exit reporting. Bounded/redacted display.log records native stages/errors. No live connection or display changes run.

2026-11-07 pre.14 delivery: native layout regression test and three shared tests passed; packaged offscreen startup and Inno compile passed. Installer SHA256 6af5b0fa9b826972cee143f5e6b99593922c8307d6949df5531231ebc1876cb8 verified on both Desktops. Implementation commit03106a3. No installation or live display connection performed. Update server for layout fix and client for optional monitor/custom mode UI before manual retest.

2026-11-07 custom display 360 FPS increment: raised shared UI/model, H.264 display negotiation, Windows encoder/preview/capture limits and driver custom refresh to360, retaining default60 and separate camera/JPEG limits. Driver0.9 descriptor supports refresh above255 while accepting legacy descriptors. Native protocol/property tests2/2 and shared model/UI/controller tests3/3 passed; unsigned driver build passed. Boundary coverage includes120/240/255/256/360 and rejects361. No live display tests or driver installation performed. Actual delivered360 FPS remains unverified and hardware-dependent.

2026-11-07 grouped client and keyboard increment: client rows are grouped into collapsible Displays/Cameras/Microphones/USBs with per-kind Add/Disconnect all; collapse preserves row objects and connections. Available server component messages hidden, actionable errors retained. Shared model/UI/controller checks3/3 pass including group-isolation assertions. Automatic Windows keyboard now requests host-side accessibility OSK after touch release and writable focused UIA ValuePattern/TextPattern metadata; owned keyboard only, existing/delegated instances preserved, checked positioning and retained diagnostics. Persisted next-connection preference uses existing uninstall-cleaned settings key. Native view/service build and video policy test pass; Linux/macOS optional request parsing updated but not built here. Actual touch typing/OSK placement remain untested.

2026-11-07 Surface uninstall monitor fix: its own staged VBoxUSB service was incorrectly counted as an independent monitor consumer. Exclude it only after validating CreatedInf ownership, published/store INF/catalog and exact signed/hash-matched system driver path (including Windows TrustedInstaller ownership). Independent usbipd/VirtualBox and unknown drivers remain protected. Native policy/package checks pass. Corrected installed helper SHA256 bf65d1cffe0faaede92c6c7477c01bf0e299da6c1a8b726dfe9f517a6fa01a89 replaced only after no active app/exporter/uninstaller was observed; prior helper backed up in Surface user Temp. Installed --check-removal now exits0, with owned monitor/INF removal eligible. No uninstall executed, no driver/disk mutation, no card reconnection required.

2026-11-07 pre.15 delivery: combined 360Hz driver0.9, grouped resources, host keyboard request and USB self-consumer uninstall fix. Packaged offscreen startup and Inno compile passed. Installer SHA256 999f6b8f587d94652a1255463c769feba97641ec035a1e886aa9b5ce36d3a88e verified on both Desktops. Implementation through1db29e2. No installer executed or live media/display/keyboard/USB test run; only the separately documented Surface setup-helper replacement and read-only removal check. Update both PCs for matching request support and host driver.

2026-11-07 desktop UI polish: Material dark controls, vector action/navigation icons with accessible names/tooltips, collapsible sidebar icon rail and geometrically centered resource headings. Compact add/disconnect actions replace repeated text buttons; responsive destination/resolution controls avoid horizontal clipping. Server is default without automatic startup. General limits are expandable, actionable errors retained. Shared model/UI/controller checks3/3 pass; offscreen readable-font renders inspected at1120 and720px with expanded/collapsed navigation and empty/populated groups. Default-page and sidebar/group-state invariants checked. Linux shared resources/style wired but Linux build not run here; no live media/device tests.

2026-11-07 pre.16 delivery: UI polish installer packaged with verified Material style dependencies and offscreen startup. SHA256 f0eaeef617c6ce12214eeaf5eaa03fdfabfa3452e14b30e7971abe49bd4720a6 verified on both Desktops. Source9a4e149. Copied only; no installation or live media tests. Surface uninstall is a separately user-authorized ongoing operation, recorded independently.

2026-11-07 USB uninstall phantom-reference fix: Surface pre15 had no sessions/journals and its real reader mounted healthy via usbstor.inf, but Windows refused owned oem66.inf removal because an absent VID80EE/PIDCAFE VBoxUSB stub still referenced it. Uninstaller now removes only known-absent USBIP stub nodes matching the exact verified owned INF and service, rechecking immediately before standard PnP removal. Real/present/unknown/foreign device references remain protected. Native helper build, isolated policy and embedded package checks passed. Exact absent Surface stub removal was separately authorized and allowed standard USB source/receiver cleanup; final full app removal verification pending.

2026-11-07 pre.17 delivery: includes pre16 UI polish/defaultServer and exact-owned absent USB stub cleanup. Native policy/package checks and packaged offscreen UI/Inno compile pass. Installer SHA256 7bf2ad2e8b1659fc0f0b56d27f6e514fdcf249f6d487bf30cce8fe7fa45d46b1 verified on both Desktops. Source34d8875. No new installation; Surface authorized standard uninstall continues separately through historical display-driver cleanup.

2026-11-07 uninstall retry repair: a repeated USB --remove recreated an empty ownership key then returned not-installed, causing the following preflight to report a version mismatch. Empty protected records are now accepted only with absent/empty current and legacy payload directories; removal retires its empty key. Unknown metadata/payload still blocks safely. Isolated empty-record and phantom-node policy checks plus embedded payload verification pass. Interactive Surface retry passed USB stages; full standard uninstall verification ongoing.

2026-11-07 Surface removal completed: exact absent owned USBIP stub retired, standard uninstaller succeeded in interactive user session (exit0, all removed, no restart required). SSH-only attempt lacked Credential Manager logon context; interactive retry completed saved settings/credential cleanup. All three historical display records removed; owned oem66.inf absent, no ExtraPane/SideScreen uninstall entry or app/session/uninstaller processes, temporary tasks removed. Remaining exact empty non-reparse Components/app directories removed nonrecursively; Program Files/ExtraPane absent. Physical disk1 remains Online and D:exFAT Healthy. No formatting, partition changes or manual disk-state changes.

2026-11-07 uninstall folder completion: standard installer now retires empty managed Display/Components/app directories after successful cleanup, addressing the empty parent directories observed after Surface removal. Uses dirifempty, preserving unrelated files; Inno packaging validation required for finalpre18.

2026-11-07 final pre.18 delivery: consolidated UI/defaultServer, absent USB stub and retry fixes, plus empty-directory uninstall rules through dc6395c. Packaged offscreen startup and final Inno compile passed. SHA256 e027c59e6419af57a3e2f2a19c99f3be433886ea845d1b8d77da9f0762a2f625 verified on desktop and Surface Desktops. No new install performed; Surface remains successfully uninstalled with its mounted card healthy. Use pre18 rather than intermediate pre16/pre17 for next manual installation.

2026-11-07 scrollbar spacing: shared server and resource scroll areas reserve scrollbar width plus12 logical pixels so controls/text do not overlap the scrollbar. Gutter remains stable while scrollbar fades. Linux resource loader now sizes to padded availableWidth. Existing offscreen UI check passed and server render inspected; no live device actions.

2026-11-07 group header hit area: empty space and title on each resource group header now toggle expansion, with header background pointer handling beneath the separate chevron/add/disconnect controls. Child actions remain independent. Included in finalpre19 UI check/package.

2026-11-07 About branding: standard packaged ExtraPane icon added at upper-right of About heading with aspect ratio preserved. Included alongside scrollbar gutters and full-header collapse behavior in pre19.

2026-11-07 collapsed sidebar alignment: menu, navigation and About icons now share the exact rail center, with explicit button padding and geometry instead of text-layout margins. Expanded labels retain consistent icon spacing. Final pre19 includes this adjustment.

2026-11-07 pre.19 delivery: scrollbar gutters, clickable group header backgrounds with independent icon actions, About logo and centered collapsed navigation. Offscreen UI check and visual server/collapsed-client review passed; packaged startup and Inno compile passed. Source6feb38b. Installer verified on local Desktop SHA256 cf17d30b9aef2fbca1a8ef406dc65b9c5ddd9fef9eadc316a6ffced8d7e2f38a. Surface SSH10.0.20.130 connection timed out before transfer; laptop delivery remains pending. No installation or live device test performed.

2026-11-07 pre.19 Surface delivery completed after user started SSH: installer copied to KEBINBOOK Desktop and SHA256 cf17d30b9aef2fbca1a8ef406dc65b9c5ddd9fef9eadc316a6ffced8d7e2f38a verified against local build. No installation performed.

2026-11-07 installer simplification: role page offers Client only/Server + Client; USB source component is mandatory in payload and prepares for either role, no source checkbox/USBHOST opt-in. Certificate disclosure remains on server-only driver page; explicit I agree - Next button records interactive consent, replaces checkbox, restores navigation sizing/captions on other pages. Silent servers still require /ACCEPTDEVELOPMENTCERT=1. Native helpers retain ownership checks; no device automatically exported. Inno compile passed; no interactive installer, driver installation or device tests run.

2026-11-07 pre.20 installer flow delivered: final Inno compile passed; SHA256 c89ba5418b05f34f193e18e9085feecd1b87673f5bd0472c4b779425ff15e9cf verified on both Desktops. Explicit driver consent button replaces checkbox and USB source support installs for both roles. No installer run. Dynamic-capacity work requested afterward remains a separate increment.

2026-11-07 dynamic Windows resource capacity: fixed4display/camera/microphone and1USB receiving slots replaced by dynamic allocator, dynamic reaped workers and per-session USB eject flags. Camera/microphone fanout counts, sharedUI128row and authenticated16control caps removed; protocol framing, pending handshake32/ticket128 bounds and perrecipientqueues remain. Positiveu32 microphone endpoint IDs now supported by rebuilt Rust helper. Connection limits section removed; docs distinguish actual OS/GPU/driver allocations and remaining one-exporter-process-per-source-PC USB backend restriction. Native builds and5targetedCTests pass(access,allocator,ejectisolation,camera fakeendpoint,PCM,display covered across5suites); purefanout>40 passes, Rust8tests pass, shared3checks pass. No live devices, endpointinstallation or high-count throughput verified. Rust helper SHA256 b97b18f76b56a3c21a43ef88ecc0c6804fb9774aa15379d8245343219c72080e.

2026-11-07 pre.21 delivery: dynamic Windows receiving allocations and camera/mic fanout, removed Connection limits section, simplified installer consent/defaultUSB included. Packaged offscreenstartup, updated microphone helper hash, and Inno compile verified. SHA256 d75a3a3c31b1697c20ebc6c85cd44aac8bc52c75a26e174809860b3a2b8903b0 verified on both Desktops. Sourcebbd7756. No installation or live device tests; sourceUSB remains one exporter process per PC, perphysicalUSB remains exclusive, OS/hardware availability and bounded pendingprotocol work still apply.

2026-11-07 compact connection controls: busy rows collapse configuration into frozen effective destination and source/display-mode summaries; display FPS explicitly labeled target (native default capped at60, matching receiver). Shared destination fields hide during active work and return when it stops. Per-row disconnect icons and group/global disconnect, Stop server and native preview Close use red; mirror/mute and USB return/recovery remain available. Isolated connection-model, shared-ui-model and controller-lifecycle checks pass; rendered connected/disconnected and red-stop layouts reviewed. Final packaged offscreen startup and Inno compilation pass. No live device connections or installation performed.

2026-11-07 pre.22 delivered: ExtraPane-2026.11.07-pre.22-Setup-win-x64.exe copied to both desktop and Surface Desktops, SHA256 fa21d0fad4009f4cd487b13d62aeb7b8d36bf075c57fb31ead087064929c436b verified on both. Source9743886; no installation performed.

2026-11-07 connection validation: attempting to enable an incomplete resource highlights invalid address, port, route, optional code format, source/monitor and custom display fields for five seconds, with one nonmodal toast: Check the highlighted fields to connect. Validation collects multiple failures and prevents runtime launch. Automatic route and blank optional access code remain valid; explicit authentication/access-code failures highlight the code and reopen shared editing. Native mode still requires a chosen monitor; custom mode does not. Regression covers missing address+monitor, malformed code, corrected custom-mode attempt, plus existing model/lifecycle checks; offscreen rendered errors inspected and packaged startup passed. No live device or installation tests; server policy and actual code validity are determined remotely, not guessed locally.

2026-11-07 pre.23 delivery: installer copied to both Desktops and SHA256 8ff1b1f8d99143b8769542003eaa3d05771ce81436a54d82e8dda4e7a2741158 verified. Source1542c7c. Inno compile and packaged startup passed; no installation or device connections. License discussion remains a recommendation only; no license applied.

2026-11-07 success feedback: active resource transitions generate green nonmodal connection toasts with resource kind and frozen destination. Concurrent arrivals are grouped (up to five recent messages); validation toasts take precedence. Isolated shared UI checks cover display/microphone success text. pre.24 packaged startup and installer compilation passed. Linux/Android parity implementation underway per PLATFORM-PARITY.md; not yet validated or released.

2026-11-07 pre.24 delivered to both Desktops with SHA256 6ac977acb41627c36d396704c15ed04f3f101de3d7883833e83c9d51b8710ed0 verified. Contains Windows validation and success toasts. No installation. Subsequent Linux/Android work is separate and is not included in this installer.

2026-11-07 Linux shared-runtime increment: grouped ConnectionModel adapter, per-display native controller/window/input/media ownership, native/custom monitor requests, asynchronous camera/microphone enumeration and ResourceSource lifecycle, protected per-destination admission/tickets, stale-generation and cancellation guards, bounded64 pending admission queue, tray persistence and double-click restore. Missing display portal no longer prevents listener startup. Added tests/linux_ui receiverwindow and admission-lifecycle isolated checks (WindowsQt6.8.3, fakeTLS+real credentialparser):2/2 pass. Workflow includes these and PCM packetizer check. Native Linux compile and device interoperability still pending; same-source fanout and USB ownership backend are not implemented. No device connection or installation performed.

2026-11-07 Linux pen overlay exclusion: InputSurface accepts window-coordinate exclusion rectangles from ReceiverWindow toolbar/Controls handle via Video alias. Local overlay pen gestures pass to Qt and remain local until lift; crossing an active remote pen into an overlay emits only pen cancel, including overlays appearing beneath stationary contacts. Removed duplicate window+application event-filter registration. Synthetic native input test verifies no overlay forwarding, no midway remote gesture, preserved unrelated queued keys, stationary cancellation and exactly-once local window dispatch. Local isolated Qt suite3/3 passes; no physical pen claim.

2026-11-07 Linux/Android review checkpoint: first Linux GitHub run 42398750809 at 5fd8738 passed native compilation, isolated checks, AppImage packaging and portable startup. Review fixes enforce initial custom display mode agreement, verify virtual camera FPS/geometry, isolate Linux inventory in bounded helper processes, clear stale success feedback and reset microphone mute on reconnect. Android configuration/admission host checks and isolated APK/lint build passed; restored activity destinations and stopping-resource state corrected. Final integrated GitHub build remains pending. User selected GitHub jobs instead of installing WSL development dependencies; no local dependency installation, live media/device connection or driver installation performed.

- Linux receiver pen controls: toolbar/handle bounds are excluded from streamed native pen input. Overlay-origin pen gestures use scoped local UI mouse synthesis (not remote mouse emulation); remote pen drags crossing controls cancel their contact without clicking the toolbar. Local grabs cancel on reset, focus loss and proximity loss. Verified an actual QtQuick Button click with global tablet/touch mouse synthesis disabled and zero remote packets; cancellation and crossing tests also pass. Isolated Qt 6.8.3 offscreen suite: 3/3 passed (receiver window, admission lifecycle, native pen overlay). Physical pen validation remains pending.

2026-11-07 integrated Linux/Android CI passed: run https://github.com/EEkebin/ExtraPane/actions/runs/42399726079 at 2042fc1 produced matching 2026.11.07-pre.67.1 packages. Linux native compile, synthetic input/pen-overlay, PCM, window and admission checks, AppImage packaging and extracted portable startup/shutdown passed. Android host configuration/admission checks, both-ABI build, lint, instrumentation compilation, release signing and package verification passed. Windows/macOS and publication were skipped by the explicit validation-only workflow option. Downloaded artifacts verified locally: AppImage c9ffc305e7622703aa6f713a232cbaa2d65942449d2d37fb94e6157b53172031; APK 0ed3b5557e8a932b8c00bbf7bc8d30f8ddb0afe49d8aaeac2717970f0a5923e0. Shared UI regression suite also rebuilt and passed3/3. No device installation or live resource test. Remaining: combined device interoperability, Linux endpoint delivery, Linux same-source fanout/USB, Android repeatable same-kind resources/fanout/USB/hosting/sharedQt consolidation; macOS parity deferred until Linux/Android work is complete.

2026-11-07 paused Linux pen controls: remote-input pause no longer disables local pen toolbar/menu interaction. Local UI mouse synthesis remains isolated from native remote input; isolated actual-button test verifies a paused pen tap clicks locally with zero remote packets. Linux Qt window/admission/pen checks3/3 passed. Added portable media-factory JSON diagnostic and package documentation checks; native GitHub validation pending, no live devices accessed.

- Linux camera slot selection now tries remaining prepared slots after geometry/format/FPS rejection, with descriptor/lease cleanup on each failed attempt and disconnect. Portable fake-handle tests cover foreign/busy exclusion, format and FPS failover, concurrent reservation, release/reuse and no-slot errors. Isolated Qt/portable suite: 5/5 passed, including resource fanout checks. AppImage installs Linux resource and platform parity guides; native input check links its QML/Controls dependencies explicitly. No physical loopback/PipeWire or device tests performed; native Linux build remains a separate CI gate.


2026-11-07 Android repeatable capture rows: camera/microphone groups now support add/remove rows, per-row or shared server address/port/code, frozen active destinations, individual/group/global disconnect and per-microphone mute. Two foreground services own per-row sessions; source leases persist through worker teardown, and per-device microphone cleanup guards allow other devices without racing delayed cleanup. Attempt IDs reject stale permission callbacks/retained snapshots. Isolated configuration, admission and capture lifecycle checks pass; ARM64/x86_64 APK, instrumentation compilation and lint pass. No device installation or capture tests. Remaining: actual multi-device hardware support/interoperability, same-source fanout, multiple display windows, USB/hosting and Qt Android UI consolidation. Source selection and Android route/privacy failures remain explicit; new capture rows use the selected common local interface.

2026-11-07 fanout/multi-row build evidence: Linux run https://github.com/EEkebin/ExtraPane/actions/runs/42406415774 at 1e6ac90 passed native compilation, input/PCM/window/admission/slot/fanout checks, package media-factory loading and documentation checks, extracted AppImage startup/shutdown. AppImage 2026.11.07-pre.68 SHA256 c3e1f35776d5a6b65b276b815e5c25bbffc51b1af863545c9bc0780ebba31fcf verified locally. Android run https://github.com/EEkebin/ExtraPane/actions/runs/42406679569 at fc39140 passed host ownership/generation/admission/configuration checks, both-ABI APK and instrumentation compilation, lint, signing and artifact verification. APK 2026.11.07-pre.68.1 SHA256 2f9f8fd64b73357f232145d8a70dccfc2604d7c91e4d7586259d5c0afcf34227 verified locally. Independent review blockers (global microphone capture guard and stale reconnect snapshots) fixed before CI. Artifacts downloaded only; no installation, device connection or live capture. No release published. Remaining: Android same-source fanout/multiple displays/sharedQt UI, Linux/Android USB where supported, physical resource-delivery validation, then macOS parity.


2026-11-07 Android same-source fanout: compatible camera/microphone rows now share one native producer per device with independent destination transport/admission and bounded queues (camera3frames, microphone8packets). Camera subscribers wait for SPS/PPS+IDR; slow subscribers fail independently. Microphone mute uses a destination-owned copy and clears that subscriber's backlog. Last detach owns actual cleanup; pending/failed cleanup prevents reuse. Pure fanout/lifecycle/admission/configuration checks and both-ABI APK/instrumentation/lint builds pass. No device or capture tests. Remaining Android gates: physical fanout/interoperability, vendor cleanup/background behavior, multiple display windows, USB/hosting and Qt consolidation.

- macOS native resource UI source added: repeatable camera/microphone groups, checkbox connect/stop, shared or per-row destination, current-device refresh on selector opening, permission-generation guards, per-row mute/status, compact active summaries and connection notices. Display now joins the same per-target admission broker. Any active host/display/capture keeps the app in the menu bar on close; explicit Quit cancels and awaits callbacks. Camera/microphone privacy descriptions added. Static diff review only at this checkpoint; native Universal compile/UI smoke and physical permission/capture tests remain pending. AppKit adapter is native and does not claim Qt/QML UI parity or virtual camera/microphone receiving/USB support.

Android fanout review follow-up: final detach now waits for bounded producer cleanup before ending its foreground session; microphone recipients rebase sample positions and signal discontinuity for late join, dropped backlog and mute transitions. A subscriber retains each queued packet's muted state across unmute, preventing its muted backlog from being transmitted as live audio. Pure fanout/timeline checks pass, including source failure/restart isolation; native JNI discontinuity assertions compile in the instrumentation APK. Both-ABI build and lint pass; no device execution. Existing camera vendor-cleanup global guard remains documented.

- macOS host control connections no longer consume virtual-display slots: dynamic connection workers have a 32-pending-admission bound, and validated display requests reserve a separate RAII identity only when creating a monitor. Existing native helper limit of four display identities remains explicit. Isolated eight-worker allocator check passed with four exclusive leases, rejection while full, and reuse after release. Native host compilation and connections remain CI/device gates.

2026-11-07 initial Mac source CI at871e4e9: Universal compilation and Apple Silicon native admission/protocol-media/UI checks3/3 passed in run42426424826. Packaging failed only at hdiutil verification with Resource temporarily unavailable after creating/signing the image; added bounded retry for that exact transient condition, retaining failure for persistent busy/corrupt images (three isolated retry tests pass). Added packaged privacy-description/setup-guide checks. Final CI must include display/control worker separation, pre-capture cancellation check and packaging fixes; no live capture/device tests.

2026-11-07 Mac capture review: camera capture retains its macOS device configuration lock until capture stops and verifies negotiated active dimensions/frame durations after configuration. The previous InputPriority approach was corrected because that preset is unavailable on macOS. Constructor failure and normal teardown release the owned configuration lock. This preserves the exact mode but also prevents other apps changing that device's capture settings during sharing; it does not detach the camera. Microphone samples carry capture-time mute state and are zeroed before enqueue; unmuting cannot expose buffered muted samples. Sender also applies current mute during packetization. Final Universal CI required. No physical capture tests. See Apple's [capture formats](https://developer.apple.com/documentation/avfoundation/capture-device-formats) and [device configuration ownership](https://developer.apple.com/documentation/avfoundation/avcapturedevice/) documentation.

2026-11-07 Android fanout final CI: https://github.com/EEkebin/ExtraPane/actions/runs/42426681089 passed host lifecycle/fanout checks, both-ABI compilation, lint, release signing and artifact verification. Downloaded APK 2026.11.07-pre.71.1 SHA256 6c0f8221055970e368cc3b234e9eb2008eeb107ca0ecf3f42c68d5a2842cc264 matches its manifest. No installation or physical capture tests.

2026-11-07 Mac package verification at 1dc6006: run https://github.com/EEkebin/ExtraPane/actions/runs/42427436065 passed Universal compilation, all four native CTests and packaging retry checks on Apple Silicon. Extracted-package architecture, ad-hoc signature, privacy descriptions, setup guide, runtime dependencies, codec self-test, compact UI startup/shutdown and DMG integrity passed. Downloaded 2026.11.07-pre.74.1 ZIP SHA256 03a3838e4b28a763ae122190e57284339b00b9c7b1c7f21b1b42d45eaf8454ab and DMG SHA256 4ff19fd9666fec3db3b5465a68a8709d8a72d3c06fac89a04a49e33477d729f1 match manifests. Intel Universal compilation, all four native CTests and retry checks also passed; the full validation run succeeded. No installation, live device capture, notarization or release publication.

2026-11-08 review fix: Android per-destination mute now tracks capture freshness through native and fanout queues, preventing queued muted-period samples from becoming audible after unmute. Conservative AudioRecord buffer accounting delays resumption rather than exposing backlog. Deterministic delayed-producer/native-buffer/epoch regressions and all four host suites pass; both-ABI APK/instrumentation compilation and lint pass locally. Physical vendor-buffer latency remains unverified.

2026-11-08 review fixes: Mac duplicate-name camera/microphone selectors now resolve stable IDs; UI smoke covers duplicate names, reorder, removal and unavailable sources. Control-only sessions retain bounded admission leases instead of allowing unlimited worker threads; isolated budget/lease regression passes. Microphone delivery rechecks mute per outgoing frame, and capture timestamps retain silence across delayed callbacks after unmute; isolated blocked-send and delayed-callback regressions pass. README corrected obsolete macOS/Android/Linux claims and separates development capabilities from published releases. Native Mac compilation and package checks pending for this increment.

2026-11-08 review validation completed: Android https://github.com/EEkebin/ExtraPane/actions/runs/42429525996 passed host checks, both-ABI compilation, lint, signing and artifact verification. APK 2026.11.08-pre.75.1 SHA256 00d4b615c9c48b7887d9f4951963194173a1ea9595cc5c9572d2a46a1f305629 verified after download. Mac https://github.com/EEkebin/ExtraPane/actions/runs/42429568221 passed Universal builds and all six native CTests on Intel and Apple Silicon, plus retry tests and extracted-package verification. Downloaded 2026.11.08-pre.76.1 ZIP SHA256 16e2667fa376402c017d1509d994b521d3fccf85d3696669642faa9c0c8ae7be and DMG SHA256 05d797cf1019d928e95ee13ae49babd22bdaff81864d4b5214c6ad2d6768fa03 match manifests. All five confirmed review findings fixed. No device actions, installation or release publication. Physical microphone/privacy timing and actual endpoint delivery remain unverified.

2026-11-08 clean four-platform build: https://github.com/EEkebin/ExtraPane/actions/runs/42432514346 at 0f9ab84 passed Windows shared Qt/native/driver compilation, 35 selected native checks, UI checks, unified installer and extracted portable verification; Linux AppImage checks; Android both-ABI build/lint/signing; and Mac Intel/Apple Silicon builds, native checks and ZIP/DMG verification. All packages use 2026.11.08-pre.78.1; downloaded checksums verified. Corrected a stale Windows camera test to check local Frame Server time separately from rebased decode time; production behavior unchanged. Windows installer SHA256 15a9317b248ff0da2036bed89647e856b977614f2640a669bc309790b9cf188d. User-authorized delivery completed: verified installer on desktop PC and Surface Desktop, executable AppImage on Arch Desktop, streamed APK install on Pixel 8a with installed version confirmed. No sharing sessions or Windows/Linux installation performed; combined multi-client device testing remains pending. No release published.

2026-11-08 disconnect UI and Android crash fix: global/group Disconnect all buttons are hidden when no active/pending resources exist across shared Qt, Android and Mac frontends. Shared offscreen UI suite rebuilt and passed. Pixel crash logs identified NetworkOnMainThreadException from Conscrypt TLS close in camera and display cancellation; camera, microphone, display and admission cancellation now dispatch socket shutdown off the UI/service thread, independently per owner. New isolated cancellation regression verifies off-caller execution, idempotence, blocked-close isolation and unrelated socket preservation; all five Android host suites pass. Native/package CI and physical post-fix disconnect validation pending.

2026-11-08 disconnect fixes validated: https://github.com/EEkebin/ExtraPane/actions/runs/42435132940 passed Windows native/shared UI/installer/portable checks, Linux checks and AppImage packaging, Android five host suites/both-ABI build/lint/signing, and Mac native checks on Intel/Apple Silicon plus package verification. All downloaded 2026.11.08-pre.79.1 package checksums verified. Updated APK streamed to Pixel 8a and installed version confirmed; Windows installer copied and hash-verified on desktop PC and Surface Desktops. Arch transfer pending because SSH at its supplied address timed out; new AppImage available locally. No live post-fix camera/microphone/display session test performed; user retest remains required.

2026-11-08 resource power actions: shared Qt, Android and Mac per-resource connection checkboxes replaced by green Connect/red Disconnect power actions. Unrelated settings checkboxes remain. Android/Mac now use one primary per-row action instead of duplicate connection controls; pending connections remain cancellable and teardown stays disabled where required. Accessible action text/tooltips accompany color. Shared offscreen UI suite rebuilt/passed; Android Java compile and lint passed. Native Mac/package CI pending; no device sessions started.

2026-11-08 USB failure/uninstall repair: Surface diagnostics showed no active export/journal and no removable disk, but an exact-owned USB capture stub remained in Windows Code21 removal-pending state. After identity/service/child/ownership checks, a non-force retry removed only that stub; uninstall precheck now succeeds without reboot or storage writes. Durable cleanup now accepts only exact-owned removal-pending stubs with stopped drivers and no children; policy regression and isolated helper compilation pass. Receiver now ends a never-enumerated import after 30 seconds and preserves its primary failure, triggering source return; actual reader enumeration cause remains unverified. Receiver/camera helper rebuild passed. Uninstaller camera cleanup uses quiet errors and logs component restart notices instead of per-component success dialogs; one normal final restart/error flow remains. Full package CI pending.

2026-11-08 GitHub Actions runtime maintenance: updated all eight workflows to the latest stable official action releases, including Pages' nested artifact uploader. Verified each action manifest declares Node24 (or the Pages composite wrapping a Node24 action); all workflow YAML parsed and configured action inputs match their published schemas. ExtraPane has no Node application/build-script dependency. Node26 is the latest LTS, but upstream GitHub action runtimes are currently Node24 and cannot be changed by setup-node. Hosted validation pending; no device sessions or system settings changed.

2026-11-08 pre.81.1 ready for Surface retest: https://github.com/EEkebin/ExtraPane/actions/runs/42437434295 passed all Windows, Linux, Android and macOS Intel/Apple Silicon build/check/package jobs using the updated official actions. Downloaded all platform packages and verified published checksums. Windows installer copied and hash-verified on desktop PC and Surface Desktops (SHA256 832a5a1d9ba8faf947f8ee76f62803140baa8772f41f2242cd3bc2d95cca25de). No installer launched, live resource session started or phone updated. Arch VM deliberately off; Linux/macOS/Android artifacts retained locally. Physical USB enumeration and post-fix resource behavior remain user validation gates.

2026-11-08 microphone jitter increment: Windows virtual microphone now prefills 40 ms, re-buffers on starvation with a bounded 80 ms adaptive target, caps storage at 120 ms and smooths 2 ms transitions. Mute/stop discard queued audio; explicit source discontinuities reset/refill through local endpoint framing. PCM conversion moved outside callback mutex; diagnostic output reports underruns, overflow drops and callback contention every five seconds. All 14 isolated Rust tests passed, including 10 seconds of bursty simulated delivery, adaptive ceiling, reset/mute/stop and independent endpoints; native C++ microphone session rebuilt. All source platforms benefit when sending to Windows. Linux endpoint requires a separate clock-paced/mute-safe implementation; documented why a queue-property-only change is insufficient. Physical phone plus three displays retest and packaged build remain pending.

2026-11-08 last-resource disconnect: Windows multi-resource runtime now retires the destination admission broker automatically after the last resource stops, fails or is cancelled; empty connectShared no longer creates a server session. Shared Qt global Disconnect all follows active/pending resource rows rather than leftover helper processes. Android last-lease release, macOS weak broker lifetime and Linux final-row admission cleanup were inspected and already implement idle shutdown. Rebuilt isolated Windows fake-child lifecycle suite covers final individual camera/microphone/display stop, immediate admission cancellation, sole-resource failure and sibling isolation; passed. Shared Qt offscreen suite verifies an idle helper cannot expose Disconnect all; passed. No live device sessions or installations.

2026-11-08 visible branding cleanup: Windows display receiver normal/view-only/F8-pause titles now use shared ExtraPane branding constants. Virtual-display description, endpoint diagnostic labels, driver installation display labels, visible diagnostics/help and legacy Windows startup/error titles use ExtraPane. Android/Linux/macOS visible-name audit found no DM/SideScreen product labels. Persisted settings, hardware/package/provider identities, migration AppIds/mutexes and protocol tokens are preserved; docs/BRANDING.md records the distinction. Isolated dm_video, dm_display, dm-display, dm-net and dm-session compilation/link passed; no windows or devices opened. Full signed driver/package build remains CI validation.
Driver branding audit also updated the EDID monitor-name descriptor to ExtraPane while preserving manufacturer/product/serial identity; descriptor length remains 128 bytes and checksum validates to zero. Driver compilation/signing remains part of the next CI package build.

2026-11-08 Surface pre81 uninstall orphan-driver repair: read-only diagnostics found no active export/recovery journal/capture devnode, but a stopped, disabled VBoxUSB service still referenced a missing system driver image. Its recorded oem66.inf and DriverStore INF/CAT/SYS matched the exact owned payload and no independent usbipd/VirtualBox installation was present. Cleanup now recognizes only an exact-owned, expected-path, absent-image, stopped, disabled, unreferenced orphan; all 64 policy combinations and isolated helper/package builds passed. Rebuilt an isolated repair helper with the Surface's exact pre81 payload (local source-build usbipd hash differs from CI), verified all payload hashes, preserved the original helper outside the app, and replaced only the installed setup helper while no app/setup process was running. Installed helper SHA256 f181a3e23c59d0ef67a5e7294edb60f1c3bc961bf0867aa9e1a225c8dcfb9f93; --check-removal now succeeds. No uninstall, service/driver removal, storage access or device session was performed; actual user uninstall remains to verify.

2026-11-08 shared desktop navigation animation: Windows/Linux sidebar now eases between its 72 px icon rail and 224 px expanded width over 220 ms, with synchronized label fade/position and a chevron rotating between right (expand) and left (collapse). Animation reverses from its current progress when clicked again. Accessible action names and keyboard focus remain on the same button; active resource sessions are unchanged. Rebuilt shared Qt offscreen shell checks passed; no live app/device windows opened.

2026-11-08 documentation reconciliation: added current implementation/validation snapshot above the historical milestone journal; updated README for shared Qt packages, resource power actions, last-resource disconnect, tray lifetime, animated sidebar, custom display targets, current packaging, microphone jitter limits and guarded USB uninstall. Corrected portable-server guidance to use the matching managed installer. Local Markdown links and diff whitespace checked; no capability marked device-verified based only on isolated tests.

2026-11-08 Speakers authorized: client-selected output appears as a normal playback device on the server; independent group above Microphones, mute/volume, native stereo delivery and client jitter buffering. First target is Windows server/client with managed audio-driver reuse; other platforms require explicit output/virtual-endpoint capability gates. Implementation in progress, no device verification claimed.

## 2026-11-08 Speakers implementation

- [x] Confirmed normal server playback-device behavior; no default-device changes
  or automatic system-audio mirroring.
- [x] Windows stereo 48 kHz PCM16 virtual playback endpoint using the existing
  managed audio backend; unique per-session ownership and endpoint removal.
- [x] Windows selected shared-mode WASAPI output; Android selected AudioTrack output
  and independent media-playback foreground service.
- [x] Speakers above Microphones, repeatable rows, power actions, mute and volume;
  last-resource admission teardown remains independent of other resources.
- [x] Distinct speaker offer/framing, dynamic admission kind 4 and opt-in five-resource
  capabilities; existing four-resource framing stays unchanged.
- [x] Server 20 ms packetizer reserve; client 40 ms jitter reserve growing to 80 ms after
  starvation, bounded queues, mute invalidation and no stale catch-up bursts.
- [x] Native Windows speaker/helper/server linking; 10 isolated core lifecycle,
  protocol and jitter checks; 5 targeted admission/security/configuration/slot checks.
- [x] 24 Rust endpoint tests including burst delivery, partial refill and USB bus-ID
  bounds; 6 Android host suites including malformed speaker framing and jitter timing.
- [x] Offscreen Qt checks and fake-child controller lifecycle checks, including
  speaker mute/volume selected before admission completes; bounded speaker startup deadline.
- [x] Windows package includes the native Speakers helper; CI includes speaker checks.
- [x] README, platform guide, decisions and Speakers setup/limitations updated.
- [ ] Final package CI and new installer/APK delivery.
- [ ] Physical normal-device enumeration and playback in an application; quick mute,
  disconnect/reconnect, selected-output removal and concurrent microphone/display load.
- [ ] Linux/macOS output adapters and virtual playback endpoints. Shared UI keeps
  unsupported speaker connections disabled with a reason.

No new device session, driver installation or audio playback was performed for
these isolated checks. Queue bounds are per-stage, not an end-to-end latency claim.

2026-11-08 Speakers package delivery: build run 42443749668 produced the matching
2026.11.08-pre.84.1 Android APK. Its SHA256 sidecar matched, streamed ADB upgrade
on the Pixel 8a succeeded, and installed versionName/versionCode were verified as
2026.11.08-pre.84.1 / 8401. No speaker session or playback test was started. Windows
installer package also completed: the matching setup EXE was copied to the local
desktop and Surface desktop, with both copies verified against SHA256
8c4be1d1894d49bb1d61a39981670ba86cb773a6c16fdb1d8539486a652d5b6b.
Windows installation is left to the user; no setup or media session was launched.

2026-11-08 shared UI validation/navigation correction: invalid fields now recolor
Material's existing outline, preserving its floating-label notch; destination
fields have a 12 px vertical gap below validation text. The sidebar retains its
logo when collapsed, fades the ExtraPane wordmark, and rotates one fixed-position
chevron button through 180 degrees while the sidebar slides. Removed the changing
horizontal toggle position that made it bounce. Shared UI build and offscreen
checks passed; expanded/collapsed invalid-address screenshots were visually reviewed.
Changes are source/build verified; the delivered pre.84.1 package predates them.

2026-11-08 installer maintenance increment: existing machine-wide installs now
show Update, Restore and Uninstall. Update is disabled unless the complete release
label is newer, preserving previous role/settings. Restore runs guarded full
removal/reset before reinstalling; Uninstall skips install actions and ends at a
removal-specific Finish page. Silent mode refuses same/older Update and requires
an explicit maintenance action for clean removal. New uninstallers report component
restart state; Restore pauses for legacy unknown state or pending removal/restart.
The production installer compiled with all component branches using inert payloads;
19 production Pascal version-comparison cases passed in an abort-before-install
fixture. CI runs those checks. No real installation, removal or driver change was
performed. README/driver guide updated; full maintenance UI and device acceptance
remain pending. The delivered pre.84.1 package predates this change.

## Baseline checkpoint before device discovery redesign

At the user's request, preserve the complete working source state on
`feat/shared-resources` before branching. Previously uncommitted legacy updater /
About source, licensing notes and historical driver-removal helper/document changes
are included as a preservation checkpoint. They were not newly integrated or
verified by this checkpoint; no driver removal or system operation was performed.
The new discovery/pairing design will proceed on a child branch. Neither branch is
ready to merge into main; merging requires the user's later readiness decision.

## Device discovery child branch

- [x] Preserved and pushed complete baseline source on `feat/shared-resources` at
  `ecf5633`; older uncommitted files are explicitly marked unverified checkpoint work.
- [x] Created `feat/device-discovery` directly from that baseline.
- [x] Recorded approved direction and pending questions in DECISIONS.md: optional
  default-on pairing, remembered trust, manual sharing, virtual vs existing screens,
  native/custom virtual monitor configuration and bounded reconnect excluding USB.
- [x] Resolve code rotation: fresh app/service startup only; existing-screen sharing defaults to view-only.
- [ ] Implement and verify discovery, trust, UI migration and additional display mode.
- [ ] User acceptance before child-to-parent and eventual main integration.

This increment changes planning documents and Git history only. No build, device
connection, installation or system configuration change was performed.

## Active implementation: devices, sharing and gateways

- [x] Localhost/IPv4 loopback validation and authenticated self-connection transport parity (isolated endpoints; physical self-display acceptance remains below).
- [x] Portable bounded device/catalog/discovery framing and authorization policy (isolated checks; runtime integration tracked below).
- [ ] LAN discovery on selected connected interfaces, expiry and direct-address fallback.
- [x] Default-on optional pairing, startup-lifetime codes, remembered cryptographic trust and revocation (isolated authentication and service checks).
- [ ] Resource offers backed by actual platform inventory; no capture/export merely from advertising.
- [x] Outbound consumer-to-provider control and authorized per-resource media tunnels (synthetic native bytes delivered through real loopback TLS; hardware delivery remains a separate gate).
- [ ] Devices / Share / Connections UI integration and cross-platform adapters.
- [ ] Existing-screen capture separate from virtual extension; view-only default.
- [x] One-hop delegated gateway catalog and traffic relay, off by default; no UPnP (three-service forward/reverse relay and revocation checks).
- [x] Bounded non-USB reconnect with permission/identity rechecks; explicit USB reconnect (actual transport-interruption checks with fake native endpoints).
- [x] Isolated protocol/lifecycle/security checks and four-platform package builds (pre.92.1; physical acceptance remains separate).
- [ ] User-authorized physical acceptance tests and review before any main merge.

These gates are not marked implemented merely because their model or UI exists.

### Device discovery implementation increment

Implemented the portable catalog/permission core, private remembered-device store,
exporter-bound pairing over TLS 1.3, bounded RPC framing and credit-based media
multiplexing. Isolated Windows checks cover malformed/partial packets, discovery
expiry and replay, trust-store persistence, pairing-code limits, identity changes,
revocation, stream credits, multi-socket grants, half-close and teardown. A 300 KB
synthetic transfer in both directions crosses several flow-control windows. These
checks use temporary files, in-memory channels or loopback sockets; they do not
connect physical devices or install components.

Localhost validation is implemented in Windows, Linux, macOS and Android source.
Existing-screen protocol requests remain distinct from virtual-display requests;
Windows has a scoped capture-host adapter, Linux uses an explicit screen chooser,
and macOS pins a display identity. Android decodes the new request mode. Windows
native builds, fake helper lifecycle checks and Android debug compilation passed.
Linux/macOS compilation and actual screen capture remain pending.

The Qt discovery adapter, peer service orchestration, new workspace UI and Windows
resource adapters are being integrated. Their presence is not evidence of a usable
end-to-end device-sharing build. Gateway traffic relay, reconnect wiring and the
other platform runtime/UI adapters remain open. No new package has been installed
or sent to the test devices during this increment.

Second integration increment: the Windows development workspace is available via
`ExtraPane.exe --devices`; the regular launch still uses the preserved interface.
It wires the shared peer service to local native helpers and keeps tickets out of
command lines. Per-ticket input permission allows controlled and view-only display
sessions to coexist without granting input to the latter. Offscreen UI checks cover
resource-to-device routing, remembered identity selection, default view-only screen
requests, sidebar animation geometry and scroll gutters. The Windows app compiles.

The peer service passes a synthetic loopback end-to-end test with TLS pairing,
remembered reconnect, catalog selection, native byte transfer and revocation. A
three-service test also verifies off-by-default gateway visibility, authorized
forwarding and revocation of both relay legs. Stop sharing preserves independent
outgoing resource sessions. Automatic gateway dialing, reverse-flow relay coverage,
transient reconnect, and Linux/Android/macOS workspace integration remain open.
Native media startup is exercised with fake processes, not real devices. The new
workspace remains opt-in until those integration and acceptance gates are complete.

Cross-platform verification: draft PR #15 targets `feat/shared-resources`, not
main. CI run 42457496119 compiled and passed the peer authentication, framing,
store, transport, service and helper-plan checks on Windows, Ubuntu and macOS.
The new Windows workspace remains opt-in; no physical acceptance is implied.

Linux workspace source now wires selected display destinations, portal-selected
existing screens, camera/microphone sources and the existing receiving endpoints
through scoped loopback adapters. Its full AppImage compilation is the next gate.
Android's opt-in foreground workspace publishes enabled display/camera/microphone/
speaker offers using the same authenticated service; its debug APK compiles for
arm64-v8a and x86_64 and the six host-side Java suites pass. Android backgrounding
stops offers and capture. Android virtual-device consumption, USB and existing-screen
capture are unavailable; Linux USB/speaker integration remains unavailable as in
the previous platform baseline. Neither platform was connected to physical hardware.

macOS workspace source now uses the same peer service with an AppKit Devices /
Share / Connections interface, custom multicast discovery, explicit permission
buttons and scoped native adapters. Existing-screen offers are view-only; camera /
microphone capture requires permission before an offer is available. Virtual camera /
microphone receiving, speaker endpoints and USB remain unavailable on macOS. Full
Universal compilation is pending CI; there has been no Mac installation or hardware
session. Reverse existing-screen relay now passes a synthetic three-service test,
including response bytes and revocation. The gateway can reopen remembered direct
provider routes; this does not reconnect USB sessions.


### Device workspace packaging and validation follow-up

- Completed installer wording: **Share only** and **Full installation (receive and
  share)** describe the existing component split. Defaults, previous-selection
  migration, `/SERVER=1`, ownership rules and explicit development-certificate
  consent are unchanged. No installer was run.
- README and Windows driver guide explain equal-device roles, opt-in `--devices`,
  display destinations versus existing-screen capture, default pairing, direct
  addressing, manual forwarding and one-hop gateway limits. Release documentation
  now describes the single managed uninstaller rather than historical separate
  driver ownership.
- Qt Devices validation highlights malformed address/port/code and incomplete
  display dimensions temporarily, with one general toast. Sharing also validates
  bind address and port. Connections resolve human-readable offered-resource names
  instead of displaying internal opaque IDs.
- Verification: isolated production Pascal maintenance fixture passed 19 version
  cases; Qt controller private-startup and offscreen layout/routing checks passed.
  These do not establish installer execution, native media or hardware behavior.
- Default-workspace transition remains pending: after platform CI and device
  acceptance, desktop startup can make Devices the default and retain the old
  workflow via `--legacy`; retain `--devices` as a compatibility alias. No default
  changed in this increment. macOS validation and native device acceptance remain
  tracked separately; Android and Linux package CI succeeded in the current review.

- Qt reconnect lifecycle follow-up: pending resources appear with Connecting and
  scoped cancellation; interrupted non-USB sessions show Reconnecting and retain
  their row until exact-grant replacement, cancellation or terminal failure.
  Duplicate uses of one resource are replaced independently. Isolated controller
  event tests and offscreen UI checks passed; no native device test was run.

### Peer transport continuity and local connections

- [x] Native endpoint setup, teardown and polling run on one serialized executor,
  separate from TLS and media I/O. Slow setup does not pause existing streams.
  Incoming media waits in bounded per-grant queues until the authorized endpoint
  is ready. Cancellation invalidates pending setup and discards late grants and
  streams without starting a replacement capture session.
- [x] Remembered, authenticated outgoing non-USB resources can reconnect after
  transport loss, with at most five attempts within 30 seconds. Each attempt
  authenticates again, fetches a fresh catalog and checks the same owner, resource,
  kind and input permission before preparing new native admission credentials.
  Protocol, identity, authorization and resource rejection do not trigger retries.
  Explicit disconnect, per-resource cancellation and forgetting a device cancel
  restoration. USB is never automatically reattached.
- [x] Reconnect events retain the connection token and map each restored grant to
  its exact prior grant; terminal failure and cancellation identify that prior
  row. Pending requests have scoped cancellation that preserves sibling uses.
- [x] Isolated Windows checks passed for actual TLS interruption through a local
  TCP proxy, successful restoration, changed permission, cancellation and USB
  exclusion. Async tests delivered synthetic bytes while native setup was blocked,
  then verified deferred delivery and no resurrection after cancellation. Service
  orchestration, reverse gateway and authenticated localhost/self-connection tests
  passed, including self-catalog handling with gateway enabled. Linux syntax checks
  passed for the service and media bridge.
- [ ] Verify native media continuity, physical self-display creation and reconnection
  on actual devices. These checks used ephemeral loopback transports and fake
  endpoints; they did not install components, capture devices or alter displays.

- Devices outgoing connections now expose Automatic route or a connected IPv4
  interface, independent of the sharing listener. Manual and remembered-device
  connections both pass this choice to the transport. Offscreen routing checks
  cover the selected-interface path; no network settings were changed.

- macOS Devices now offers Automatic route plus every connected non-loopback IPv4
  interface, refreshed when its route menu opens. Both manual and remembered-device
  connections pass the chosen local address to the authenticated transport; a stale
  selection is rejected before connecting. A unique remembered address/port hint
  can select a saved identity without re-entering its code; it does not authenticate
  by address, and ambiguous hints require explicit remembered-device selection.
  Static review completed; the updated macOS package CI and physical interface
  selection checks remain pending.

- Peer review follow-up: manual connection to a unique remembered address/port
  pins that saved identity, including localhost/127.0.0.1 equivalence. Ambiguous
  saved hints fail rather than choosing a device, and an explicit expected identity
  takes precedence. Actual TLS tests passed no-code remembered reconnection,
  explicit wrong-identity rejection and ambiguous-hint rejection. Interrupted
  pending requests now emit scoped stopped events even when established sibling
  resources are being restored.

- Explicit Share-page resource refresh now re-enumerates Windows and Linux
  inventories off the UI thread and ignores overlapping requests. Unchanged
  resources keep their enabled state; cosmetic label/mode metadata refresh does
  not terminate an active Windows helper. Removed resources and withdrawn
  availability/input permissions still revoke their affected uses. Windows app
  compilation, private-startup/offscreen UI checks and the fake-process metadata
  refresh regression passed. Linux wiring uses its existing guarded inventory
  worker; full Linux compilation remains a CI check.

- Preview validation fields now recolor the native Material outline rather than
  drawing over the floating label. The offscreen regression verifies that the
  label-aware outline remains in use and normal styling returns after validation
  clears. This fixes the earlier border/label overlap in the new workspace too.

### Device preview package checkpoint: 2026.11.08-pre.92.1

All jobs passed in [the four-platform package run](https://github.com/EEkebin/ExtraPane/actions/runs/42461523431)
for code commit `7a9b03f`: Windows native/shared UI checks and installer/portable
verification, Linux AppImage build/startup, Android build/lint/signing, and macOS
Universal packaging plus native checks on Apple Silicon and Intel. The preceding
[shared-core run](https://github.com/EEkebin/ExtraPane/actions/runs/42459714309)
passed lifecycle, encrypted-control and endpoint checks; the only subsequent code
change was the isolated-tested QML validation-outline correction.

Packages are staged under `out/device-preview-2026.11.08-pre.92.1`. They are test
artifacts, not a published release. The child branch and draft PR remain unmerged.
Desktop preview launch still requires `--devices`; Android has its Devices preview
entry. Use [the physical acceptance guide](DEVICE-WORKSPACE-TESTING.md) next.
No device session, application installation, driver installation, network/security
change or physical acceptance test was performed during this build checkpoint.

### Authorized preview delivery

The user subsequently approved Surface transfer and Android installation.
`pre.92.1` installer was copied to the Surface Desktop over SSH with its existing
trusted host key and a matching remote SHA256. The Pixel 8a received the matching
APK through streamed ADB update; Android reports version `2026.11.08-pre.92.1`
and versionCode `9201`. Windows installation remains for the user to run on both
computers. No streaming or physical resource tests were started during delivery.

### Correct default workspace after user feedback

Normal desktop startup now selects Devices / Share / Connections; `--legacy`
retains the previous interface and `--devices` is an alias. Android's launcher
opens device sharing, with explicit legacy navigation. The Windows packaging
smoke check now instantiates the real default workspace using an isolated private
store and no native inventory/listeners. This exposed an incorrectly ordered QML
resource alias in pre.92.1: the app embedded the page under a different path from
the loader. The alias is now declared before the app resource collection.

Rebuilt Windows executable passes default and legacy offscreen startup checks;
Android both-ABI debug build, test APK compilation and lint pass. New four-platform
package verification and replacement delivery completed below. No physical media
session or driver installation was performed for this correction.

### Default-workspace replacement delivered

All four platform package jobs passed for `2026.11.08-pre.93.1` at code commit
`73e31ed` in [run 42463605533](https://github.com/EEkebin/ExtraPane/actions/runs/42463605533).
This includes Windows packaging/startup checks, Linux AppImage startup, Android
build/lint/signing and macOS Universal packaging with Intel/Apple Silicon checks.
The Windows installer and portable archive passed SHA256 verification. The installer
is on both the desktop PC and Surface Desktops; the remote copy hash matches.
Android streamed update succeeded, reports versionCode `9301`, and resolves the
normal launcher to `PeerActivity`. Open the updated app normally for Devices / Share /
Connections; desktop `--legacy` explicitly selects the older interface.

Windows installation and physical resource acceptance remain user steps. No
Windows driver installation or media session was started during this delivery.
The child branch and draft PR remain unmerged; these are test artifacts.

### Resource groups and Android navigation correction

- [x] Desktop Devices catalogs, Share offers and Connections use collapsible
  Monitors, Cameras, Speakers, Microphones and USB groups with full-width header
  actions, centered labels, counts and icons. Empty groups are hidden. Existing
  screen capture stays distinct from virtual-display destinations.
- [x] Pending resource names/kinds are retained if a catalog changes, so their
  group and individual cancellation control remain visible.
- [x] Android replaces its long single form with Devices, Share, Connections and
  About pages and an animated navigation drawer. Group collapse/navigation retain
  the display surface; system/keyboard insets, readable dark controls and touch
  targets are explicit. USB remains visibly unsupported.
- [x] Android audio inventory filters internal ports and names physical routes:
  Loudspeaker, Earpiece, built-in microphones and connected peripherals. Exact
  routing IDs remain internal. Earpiece requests per-track communication usage;
  it does not seize global call routing and can fail under device audio policy.
- [x] Isolated desktop group/selection/cancel tests, controller kind-retention
  regression and actual app startup pass. Synthetic desktop screenshots reviewed.
  Android audio catalog host checks, both-ABI APK build, instrumentation compilation
  and lint pass. No physical audio route or streamed resource test was run.
- [x] Replacement Windows and Android test packages delivered as `pre.94.1`.
- [ ] Physical Android layout/audio-route acceptance remains pending.

Packaging checkpoint: CI run `42465820473` encountered GitHub HTTP 504 errors
downloading Gradle, Mbed TLS and the Windows USB dependency. Android host checks
passed before the download failure. A local release build from the same code
(`1ff17bc`) succeeded using the cached pinned tools; the existing project signing
key, certificate, APK metadata, ABIs and alignment were verified by the standard
packager. Streamed update on the Pixel 8a succeeded with `2026.11.08-pre.94.1`,
versionCode `9401`. No app launch or physical resource test was performed.

The matching Windows native helpers and UI were rebuilt locally with the cached
pinned dependencies. Standard package verification passed isolated extracted app
startup without the SDK on PATH, complete manifest hashes, and embedded camera,
USB receiving/source payload checks. The installer is on both the desktop PC and
Surface Desktops, with matching SHA256. Windows installation remains for the user.
The local build manifest marks working-tree changes because this checklist was
being updated; application source matches `1ff17bc`.

CI retries still encounter external download timeouts/HTTP 504s; do not describe
this revision as passing all four remote package jobs. Linux compiled successfully
before a packaging-tool download failed. No release was published or branch merged.

### Restore power controls and consistent navigation

- [x] Desktop and Android resource offer checkboxes replaced with green enable /
  red disable power icons and adjacent resource labels. Settings such as pairing
  and input permissions remain separate controls. Offering still does not start
  native capture by itself.
- [x] Desktop resource actions turn red for active/pending sessions matching that
  exact peer, owner and resource. They disconnect/cancel those sessions without
  affecting another peer or starting a duplicate. Connections uses red power
  controls on both desktop and Android.
- [x] Desktop sidebar toggle uses the same fixed hamburger convention as Android;
  sidebar sliding remains, icon rotation is removed.
- [x] Desktop offscreen action/color/identity/group/navigation tests and actual
  app startup pass. Android both-ABI build, instrumentation compilation and lint
  pass. No physical resource session was started.
- [x] `pre.95.1` locally packaged from clean code commit `01109f4`. Windows
  extracted-app startup, manifest hashes and native payload checks passed; the
  installer was copied to both Desktops and the Surface checksum verified.
  Android release signature/metadata/ABIs/alignment passed; streamed update
  succeeded and the phone reports versionCode `9501`.
- [ ] Physical acceptance remains. No Windows installation or resource session
  was started during delivery. Android CI run `42466926486` failed downloading
  Gradle with GitHub HTTP 504; local cached-toolchain checks passed instead.

### Simplify monitor choices

- [x] Share separates monitor rows under one **Use as an extra monitor** heading
  and one **Share an existing screen (view only)** heading. Devices uses
  **Extend this desktop** / **View an existing screen (view only)**. Repeated
  per-row descriptions and duplicate action labels removed.
- [x] Custom mode is named **Custom resolution**. Windows uses friendly monitor
  names with numbered fallbacks instead of raw display paths. Linux uses model
  names where available; macOS uses numbered monitor labels. Resource identities,
  offers and negotiated modes are unchanged.
- [x] Offscreen subgroup ordering/single-heading tests and existing power/routing
  checks pass. Actual Windows app rebuild and startup check pass, including native
  friendly-name mapping compilation. No physical display session was started.
- [x] `pre.96.1` Windows installer/portable built locally from clean `6a73020`.
  Standard extracted startup, complete file hashes and embedded payload checks
  passed. Installer copied to both Desktops with Surface SHA256 verified. Windows
  installation remains a user step; no physical display session was started.
- [ ] Linux/macOS package validation pending. Run `42467692279` again encountered
  external dependency HTTP 504/download timeouts on macOS/Android. Android source
  is unchanged by this correction and the phone remains on `pre.95.1`.


### Resource-controlled sharing and Nearby-first navigation

- [x] Android and shared desktop UI start the listener from the first resource
  power button. No separate Start sharing action; Stop sharing all is hidden
  while idle and clears enabled offers. Last-off stops sharing. Explicit
  gateway-only desktop sharing remains supported.
- [x] Listener generation tracking rejects stale startup success/failure after
  cancellation or restart. App startup offers are off; Android backgrounding
  clears its offers instead of silently restarting capture later.
- [x] Desktop Devices shows Nearby first and collapses Connect by address.
  Normal Windows/Linux startup starts discovery; isolated Windows UI smoke
  and Linux offscreen startup remain network-silent. Static LAN, localhost,
  selected interface and manually forwarded destinations remain supported.
- [x] Isolated desktop UI/controller lifecycle checks pass, including busy bind,
  pending cancellation, gateway-only sharing and hidden idle actions. Android
  eight host checks, debug/test APK compilation for both native ABIs and lint
  pass. No physical resource session was started.
- [x] Native macOS sharing controls and Nearby/manual layout updated and statically
  reviewed. Windows app compiled. Core service, native-async, reconnect and
  self-connection isolated checks pass, including outgoing byte delivery after
  stopping local sharing.
- [x] Android `pre.97.1` signed release built from `af52e2f`; signature,
  metadata, both ABIs and alignment verified. Streamed update to Pixel 8a
  succeeded; installed versionCode `9701` confirmed without launching capture.
- [ ] Linux/macOS hosted compilation and package validation pending. Run
  `42468944679` encountered Mbed TLS / Gradle GitHub download HTTP 504s
  on macOS and Android; Android cached-toolchain build passed locally.
- [x] Windows `pre.97.1` final package built from clean `af52e2f`; exact
  executable version, extracted offscreen startup, file manifest and native
  component package checks passed. Installer copied to this PC and Surface
  Desktops; Surface SHA256 verified. No Windows installer was executed.
- [ ] Physical acceptance remains; no media/resource session was started.
  The Windows hosted job also encountered an HTTP 504 dependency download;
  local cached-toolchain packaging passed.


### Android Back gesture follows page history

- [x] Drawer/fullscreen dismissal precedes page-history navigation; repeated
  selection and fullscreen redraw do not create duplicate entries. Navigating
  back does not finish the activity or pause active resources. Root Back is
  guarded while sharing/connected and retains ordinary idle exit behavior.
- [x] Nine isolated host checks pass, including page history and branching after
  Back. Android debug/test APK compilation and lint pass.
- [x] Signed Android `pre.98.1` packaged and streamed to Pixel 8a; installed
  versionCode `9801` confirmed. No app/resource session launched.
- [ ] Physical gesture acceptance pending.

- [x] Matching Windows `pre.98.1` installer/portable packaged on request from
  clean `dacc578`. Exact executable version, extracted offscreen startup,
  checksums and component payload checks pass. Installer copied to both
  Desktops; Surface SHA256 verified. Windows behavior is unchanged from
  `pre.97.1`; the `.98` feature fix is Android Back navigation. No Windows
  installation was performed.


### Android empty-space drawer swipe

- [x] Rightward single-finger swipes from non-interactive page space open the
  drawer. Controls, selectable text and remote-display input are excluded.
  Vertical gestures and multiple fingers cancel recognition; recognized swipes
  cancel the underlying scroll gesture and consume the remaining events.
- [x] Isolated swipe direction/threshold/cancellation checks and Android build
  and lint pass. Physical gesture acceptance remains pending.
- [x] Signed Android `pre.99.1` release and lint passed; package verified and
  streamed to Pixel 8a. Installed versionCode `9901` confirmed. No physical
  resource session was started.
- [x] On request, Android `pre.99.1` reinstalled and matching Windows installer
  packaged from clean `e9f92f0`. Exact version, extracted startup and payload
  checks passed; installer copied to both Desktops with Surface SHA256 verified.
  Windows behavior is unchanged; no Windows installation performed.


### Default availability and actual-use power controls

This replaces the earlier opt-in offer power behavior.

- [x] Normal Windows/Linux/Android startup publishes supported resources and
  listens automatically. Pairing defaults stay enabled; advertising/pairing do not
  start capture, create monitors or claim USB storage. OS permission gates remain.
- [x] Provider red power is derived from exact incoming resource sessions, with
  consumer names. Idle resources remain available. Disconnecting a resource or
  all incoming uses preserves the catalog/listener and unrelated outgoing uses.
- [x] Idle automatic listeners do not prevent normal app exit. Network settings
  can be applied/retried while idle; active uses block disruptive reconfiguration.
- [x] Android keeps foreground-only hosting, explicit permission actions and
  independent per-resource session tracking. Returning restores availability,
  never old capture sessions. Failed configurations do not retry continuously.
- [x] Windows application compiles; isolated Qt controller/UI tests cover automatic
  inventory, exact-resource/fanout activity, idle lifetime and network validation.
  Eleven Android host suites and APK/test APK compilation/lint pass.
- [x] Native macOS default availability, provider-use names and exact-resource
  consumer power/cancellation implemented and statically reviewed.
- [ ] Linux/macOS hosted build/package validation pending.
- [x] Signed Android `pre.100.1` built/verified and streamed to Pixel 8a;
  installed versionCode `10001` confirmed. No app/resource session launched.
- [x] Windows `pre.100.1` packaged from clean `dc120b1`; exact version,
  extracted offscreen startup, manifest and native payload checks passed.
  Installer copied to both Desktops; Surface checksum verified.
- [ ] Physical pairing/camera acceptance pending. No Windows installation or
  physical resource session was performed.
- Hosted run `42471761296` hit an Mbed TLS download HTTP 504 on macOS;
  native macOS compilation is not yet verified for this increment.


### Android drawer follows the finger

- [x] Replaced threshold-triggered swipe opening with continuous drawer position
  and scrim opacity during drag, in both directions. Release settles by position
  or fling; cancelled/multi-touch gestures restore their starting side.
- [x] Buttons, vertical scrolling, selectable text and remote-display touch input
  retain their touch streams unless a drawer drag is claimed. Rotation/pause
  cancel tracking. Isolated drag progress/reversal/release checks pass.
- [x] Android release/test APK compilation and lint passed; signed `pre.101.1`
  verified and streamed to Pixel 8a, installed versionCode `10101` confirmed.
- [ ] Physical drag acceptance pending; no resource session launched.

- [x] Matching Windows `pre.101.1` packaged from clean `b75d56d`; exact
  executable version, extracted startup, checksums and payload checks passed.
  Installer copied to local and Surface Desktops; Surface hash verified.
  Android is already on `pre.101.1`; no Windows installer was run.
- [x] Persisted requirement in AGENTS.md: every Android revision delivery includes
  the matching Windows build, including minor/Android-only changes.


### Collapsible device cards

- [x] Connected device cards in the shared Devices UI hide/show their resources
  from a full-width header. Disconnect remains a separate action. Header retains
  name, verification and resource/active counts; state is keyed by peer identity
  and connection and survives catalog refresh/reorder.
- [x] Android paired-device cards collapse details/actions with persistent
  per-device state. Navigation drawer and session lifecycle are unchanged.
- [x] Offscreen checks verify independent peers, refresh retention and no session
  changes on collapse. Windows compiled; Android build/test APK and lint passed.
- [x] Native macOS device headers/body collapse implemented and statically reviewed.
- [x] Matching `pre.102.1` Windows/Android packages verified from `23b754b`.
  Windows exact version, extracted startup, checksums and payload checks passed;
  installer copied to both Desktops with Surface checksum verified. Android
  streamed install succeeded; installed versionCode `10201` confirmed.
- [ ] Linux/macOS hosted verification pending (run `42472790511`).
- [ ] Physical acceptance remains; no resource session started.


### Pairing rejection investigation on installed pre.102.1

- [x] Both installed PCs run `pre.102.1`; Surface listener reachable. TLS 1.3
  handshake and fresh diagnostic pairing/catalog (seven resources) succeed.
  A private copy of the desktop identity reproduces the generic TLS failure.
- [x] Confirmed asymmetric trust: Surface remembers desktop; desktop has no
  corresponding remembered peer. Server rejects duplicate pair mode before
  its response, resulting in a misleading TLS error.
- [x] Removed only the stale Surface record and diagnostic pairing, preserving
  encrypted backups inside its private trust directory. No media resources used.
- [x] User screenshot confirms the repaired Surface GUI pairing: verified peer
  with seven advertised resources.
- [ ] Improve explicit stale-pairing diagnostics/recovery without automatic
  authentication downgrade or silent trust replacement.
- Installer review: ordinary Update does not delete the current peer trust store;
  the cause of lost local pairing is unproven. Separately, current uninstall
  cleanup misses the new peer trust directory; track for a scoped cleanup fix.


### Devices card boundary (2026-11-08)

- [x] Shared desktop Devices page wraps each connected device's header,
  Disconnect button and resource groups in one rounded, bordered card with
  internal padding. The border remains when collapsed and follows the expanded
  content height; resource and pairing behavior is unchanged.
- Verification: source diff reviewed and whitespace check passed. This is a
  presentation-only change; no device sessions or installations performed.
- [ ] New packaged build and visual acceptance pending.


### Devices management and Connections catalogs (2026-11-08)

- [x] Shared desktop Devices manages discovery, saved peers and device links;
  Connections contains bordered, collapsible resource catalogs and active session
  controls. Connected peers hide Connect; known peers are excluded from Nearby.
- [x] Nearby is an explicit, bounded search. Automatic provider announcements
  remain independent; browsing never starts resource capture or media sessions.
- [x] About displays the running application version. Android and native macOS
  navigation/management updated, preserving platform capability limits.
- Verification: shared desktop offscreen UI/routing checks, synthetic discovery
  and endpoint lifecycle checks, and Windows compilation passed. All 12 Android host
  suites and final compilation/lint passed. Native macOS source reviewed; native
  compilation remains pending.
- [x] Matching `2026.11.08-pre.104.1` packages delivered from clean `3951685`:
  Windows installer copied to local and Surface Desktops (both SHA256 verified);
  Android streamed install succeeded, versionCode `10401`/versionName confirmed.
  Windows exact version, extracted portable UI smoke, native component payloads
  and checksums passed. Android signature, ABIs, alignment and metadata passed.
  Windows installer SHA256:
  `f9f549a387b37d53bad9a173a5a111e6dde219007b761c486775bc36c13ef36e`.
  Android SHA256:
  `0aa82198307c61496a27174d4e0ec12c309290a410583cf6809f1be35c4f1a9f`.
  No Windows install or media session performed. Hosted run `42474922709` is
  still running; Linux/macOS results remain pending.
- [ ] Linux/macOS hosted compile and physical acceptance pending; no new media
  connection, driver installation or device test performed.


### Persistent paired resources and camera startup lifecycle (2026-11-08)

- [x] Shared desktop navigation now combines Share into Devices and names the
  resource/catalog page Resources. Android and native macOS follow the same pages.
- [x] Isolated desktop page/routing checks passed after the navigation change.
- Investigation: Service's old used-resources/empty-session rule closed a device
  link after a resource failed or stopped. This could force reconnecting the
  device before retrying its first camera. Native device acceptance is still needed
  to establish whether any separate camera startup failure remains.
- [x] Persistent catalog lifecycle and authenticated remembered-peer automatic
  connection implemented. Targeted regressions cover slow first preparation,
  cancellation/failure isolation, catalog-only reconnection, incoming-only saved
  identity route hints, explicit suppression and older-peer cancellation fallback.
  Five targeted core regressions passed (service, presence, asynchronous native
  setup, reconnect and self-connection); shared Qt lifecycle/UI checks passed and
  Windows app rebuilt. Resource cancellation negotiation is covered by an
  older-peer echo/no-new-opcode/late-grant-close regression. Unchanged periodic
  catalogs do not reset the resource UI.
- [x] Matching `2026.11.08-pre.105.1` built from clean `7ef7073` and delivered:
  installer copied to both Desktops with SHA256 verified; Android streamed install
  succeeded and versionCode `10501` / versionName verified. All 12 Android host
  suites, both ABI builds, test APK and lint passed. Windows portable extraction,
  version/build metadata, UI smoke and native payload checks passed.
  Installer SHA256:
  `0d80bfd2a0b768e53c954d2170cb97c5782ef373111f4130c8899985791c4de4`.
  APK SHA256:
  `89756229b434823f0864b56d5e776f8d96af5e26054ea1b555cefa13cf1407f3`.
- [ ] Physical camera/auto-presence acceptance pending after user updates Windows;
  no Windows installer or media connection was run. Cross-platform hosted run
  `42476646478` remains in progress.
- [x] Native macOS management now deduplicates mutual control links by identity;
  device-level Disconnect closes both directions. Physical acceptance pending.
- Previous pre.104.1 hosted Linux/Android jobs passed. Both macOS jobs exposed an
  Objective-C ivar initializer syntax error; source corrected here, CI recheck pending.

### pre.105.1 redelivery requested

- [x] Re-copied the verified installer to the current Windows Desktop; reinstalled the matching Android APK through ADB and confirmed versionName/code 10501.
- [x] Re-copied the installer to the Surface Desktop at its new address, 10.0.20.143. Verified the SSH host key against the previously saved Surface identity and verified the remote installer SHA256. No Windows installation performed.



### Discovery cards and About presentation (2026-11-08)

- [x] Manual Nearby search uses an icon refresh control, spinning during search,
  with three sequential bouncing dots. Nearby results appear as named, bordered
  cards as they arrive; Android publishes bounded immutable progress snapshots.
- [x] Nearby/Paired/Connected/This device headings are larger with separators;
  shared desktop paired cards use the same spacing and borders. Existing dark
  blue theme and accent colors remain consistent.
- [x] About navigation is simply About. Redesigned product/description/version
  card, high-resolution original logo with smooth/mipmapped Qt downsampling,
  aligned project links and license status. Connection status is hidden on About.
- [x] Android and native macOS have equivalent search/About presentation;
  their search animations respect the platform disabled/reduced-motion setting.
- Device-type limit: discovery metadata does not identify platform/type. Cards
  use neutral computer icons; no Android/phone logo is guessed from names.
- Verification: offscreen Qt UI checks cover spinner lifecycle/no duplicate scans
  and existing routing; rendered Devices/About screenshots reviewed. All 12
  Android host suites and final build/lint passed. Native macOS static review only
  for this increment; hosted build/visual acceptance still pending.
- Previous pre.105.1 hosted run `42476646478` passed Windows, Linux, Android and
  both macOS architecture jobs. No new physical media tests performed.
- [x] Matching `2026.11.08-pre.106.1` packages built from clean `e19e9c6`.
  Windows installer copied to local Desktop and Surface Desktop at 10.0.20.143;
  both checksums verified and Surface SSH identity matched the saved key. Android
  streamed install succeeded; versionName/code `10601` confirmed. Windows exact
  version, clean build metadata, extracted UI smoke and native payload checks passed.
  Installer SHA256:
  `dad49b8c10fb803c441774ddcc7967026ca8f0cfda32ed2fd4c84eb8823f9cf3`.
  APK SHA256:
  `d8845252965b09dc4d682288db551b8c10e368c98913e486f18e5ce1610777f0`.
- [ ] User visual acceptance after Windows installation pending. No Windows
  installer or media connection was run. Hosted run `42538425580` passed all
  Windows/Linux/Android/macOS architecture jobs; publication was skipped.


## Active increment: monitor rows and bilateral Forget (2026-11-08)

- [x] Repeatable virtual-monitor rows using Custom or reusable named presets;
  independent per-row cancellation and stop, including catalog refresh/reconnect.
- [x] Reject duplicate existing-screen views on one consumer, including pending starts.
- [x] Existing-screen viewer startup repair and capability-gated Windows input;
  native Surface-to-desktop video verified, physical input verification pending.
- [x] Bordered resource types, input beside power, automatic saved-device availability.
- [x] Authenticated bilateral Forget with durable offline pending state, idempotent
  acknowledgement retry and no catalog/media access using a revocation credential.
- [x] Review upgrade/trust preservation and safe background availability retries.
- [x] Isolated regression checks, matching Windows/Android packages and delivery.
- [ ] Physical verification remains pending; no capture/input test is implied by builds.

Verification so far: Qt offscreen checks passed after final monitor numbering;
rendered Resources layout reviewed. Checks cover repeated Custom/named presets,
independent stop/cancel, existing-screen duplicate rejection and input default,
pending Forget controls, and keeping drafts when reconnect creates a new link.
Android compile/lint and its host checks passed. Core loopback checks cover
bilateral removal, offline/lost-ACK restart, forbidden ordinary use of revocation
credentials, duplicate pending screen views, and changed-route identity-pinned
recovery after a real 60-second backoff. Trust-store tests cover v1 migration,
key-bound tombstones, capacity and explicit pending dismissal. Native macOS has
static review only until the hosted build. Ordinary installer Update and signed
Android replacement preserve trust; no evidence that Update itself erased it.

Windows existing-screen input tickets now carry the exact requested permission;
backend subprocess failures drain their final output and surface a bounded,
credential-redacted error for the affected resource. Synthetic adapter/runtime
checks pass. The reported Surface-to-desktop early exit is still being diagnosed;
these fixes alone are not evidence that the live failure is resolved.


Approved live existing-screen diagnostic reproduced the reported failure on the
installed Surface and desktop builds: Surface physical DISPLAY1 captures at
3000x2000; Intel Quick Sync H.264 encoder returns HRESULT `0xc00d6d61`, the SDK's
`MF_E_TRANSFORM_STREAM_CHANGE`. The encoder output loop treats this renegotiation
as fatal. Desktop receives the video offer then reports an incomplete TLS message
because the provider exited. No display layout, driver, firewall or saved pairing
was changed. Encoder repair and repeat verification are pending.

Independent revocation review also identified repeated tombstone-only Forget
requests unnecessarily bumping the global authentication epoch. The handler now
acknowledges an already-completed removal without repeating unrelated connection
invalidation; regression verification is included before packaging.


Encoder repair verified with an approved 25-second Surface-to-desktop test:
3000x2000 video decoded and presented continuously, with no encoder/sample failures.
The temporary receiver was deliberately stopped at the end; the subsequent remote
socket-close message is expected teardown. Diagnostic SSH-forward throughput was
roughly 30 FPS after startup, with about 3.6?3.9 ms decode time and three skipped
frames total. This proves native-resolution continuity, not 60 FPS normal-LAN
performance. No input was injected. CPU camera encoding shares the repaired output
logic but was not physically retested. Native host SHA256:
`68710ee5876bd3486ae1c389c01e3dfca2401eff6a993d099de6d760a09928ce`.


### Matching pre.107.1 delivery

- [x] Source saved and pushed on `feat/device-discovery`: `a639811` (monitor rows,
  bilateral Forget and UI) and `f75fd35` (verified encoder repair). Not merged.
- [x] Matching **2026.11.09-pre.107.1** packages built from clean `f75fd35`.
  Date follows GitHub run creation in UTC, rather than the local November 8 date.
- [x] Windows installer copied to this Desktop and the Surface Desktop at
  10.0.20.143. Both SHA256 values verified; Surface SSH identity matches its saved
  key. No Windows installation performed. Exact version, clean metadata, driver
  hashes, portable checksums, extracted UI smoke and native payload checks passed.
  SHA256: `50d0339e2c56196ba7b57419e4c67b844a956c448f5512cb618784e479bcb586`.
- [x] Android streamed replacement install succeeded; versionName and code10701
  confirmed. Signature, both ABIs, release settings and alignment verified.
  SHA256: `59d2e067685bd514d8f74c636b36be170e2d81122547f2236f3cdccd8294de83`.
- [x] Approved Surface diagnostic processes, temporary scheduled tasks, staged
  native binary and SSH forwarding cleaned up. No driver, firewall, layout or
  saved-pairing mutation was part of that diagnostic.
- [ ] Hosted run `42541586067` remains active for Windows/Linux/Android/macOS
  architecture checks. Local package validation does not substitute for these.
- [ ] User installer/update, bilateral Forget UI and existing-screen input checks.
  Existing-screen video is live-verified; no input was injected during the test.
- Limit: bilateral Forget needs a reachable remote listener. One-way forwarded
  routes may leave remote removal pending even after local access is revoked;
  explicit pending dismissal never claims remote acknowledgement.


## Active increment: pairing-code modal (2026-11-08)

- [x] Shared Qt, Android and native macOS prompt for new pairing in a modal on
  Connect, replacing inline code fields.
- [x] Saved trust skips the code prompt and cannot be downgraded by discovery or
  the unpaired option. Unpaired nearby devices are visibly unverified.
- [x] Cancel clears code without a connection; invalid codes remain local;
  submission uses the frozen target/interface; pending removals block re-pairing.
- [x] Isolated UI/Android checks and matching Windows/Android package delivery.


Pairing-modal verification: shared Qt offscreen checks pass for required/open
Nearby flows, cancelled/invalid/single submission, captured target/interface and
saved/pending identity behavior. Normal and error dialog renders reviewed; the
code field uses an external instruction and accessible name so the error border
does not cross a floating label. Android's 13 host suites and build/lint pass.
Native macOS is statically reviewed; hosted compilation remains pending.
Core admission also rechecks saved trust when an open handshake completes, so a
pairing created while the modal/handshake was in flight cannot be downgraded.
Deterministic concurrent-trust tests and five related loopback checks passed.
No physical media or input tests were run for this UI increment.


### Pairing-modal pre.108.1 delivery

- [x] Committed/pushed `cdb7745` on `feat/device-discovery`; no merge/release publication.
- [x] Matching **2026.11.09-pre.108.1** Windows/Android packages built from clean
  source. Windows exact version/build metadata, driver hashes, extracted portable
  UI smoke, payload and checksum checks passed. Installer copied to both Desktops;
  Surface host key and both destination hashes verified. No Windows install run.
  Installer SHA256: `2bfc94f64ff5a7cdab623635aef52c39d0a225a1aa981b35bb93d89d7936d3dc`.
- [x] Android streamed install succeeded and version/code10801 confirmed. Pinned
  signer, release settings, both ABIs and alignment verified.
  APK SHA256: `9c01c89bad7d84623c29faa2be8df75216426ff78ee4ae8a2da1562e777efe7f`.
- [ ] Hosted run `42542690583` is queued behind the previous run. pre.107 hosted
  Linux, Android and both macOS jobs passed; its Windows job is still running.
  Native macOS modal compilation and physical UI acceptance remain pending.


## Active increment: pairing-focused device list (2026-11-08)

- [x] Remove device-level Connected indicators and duplicate paired-peer cards.
- [x] Keep saved pairings and Resources navigation independent of current control
  transport; preserve explicit unpaired identity warnings and actual resource errors.
- [x] Match shared Qt, Android and native macOS presentation; retain media-on-power
  semantics and background resource-list refresh without claiming idle sockets close.
- [x] Isolated checks and matching Windows/Android build delivery.


Verification: Qt offscreen checks passed for persistent paired Resources navigation,
no duplicate paired management card, generic status suppression and preserved
resource errors. Rendered Devices layout reviewed. Android's 13 host suites and
integrated build/lint passed. Native macOS statically reviewed pending hosted build.
No transport/media lifecycle was changed and no physical stream test was run.


### Pairing-state pre.109.1 delivery and encryption follow-up

- [x] Clean `cfc5ccf` packages **2026.11.09-pre.109.1** built and verified.
  Windows installer copied to both Desktops, matching SHA256 verified and saved
  Surface SSH identity checked. No Windows installation run.
  Installer SHA256: `f2483cd6e44e7d5871f8b5f3b8ab44939cb2562e5cd451d8ccbb2ab749d9f083`.
- [x] Matching Android streamed install succeeded; version/code10901 confirmed.
  APK SHA256: `7440ed4f7a9dd0a553a82c1dd5d42900a07012b22601bb6cc9ebeb688bed5e71`.
- [ ] Hosted run `42543989485` queued; local Windows/Android checks passed but do
  not replace native macOS compilation or physical UI acceptance.
- [x] Audited direct/gateway/native media encryption paths. Direct paired links
  authenticate encrypted endpoints; open mode remains unverified. Current relays
  see native admission secrets, so gateway-blind E2E is not guaranteed.
- [x] Confirm end-device pairing policy for gateway access before implementing
  endpoint-only authentication/tunneling. See SECURITY.md for findings and scope.
  pre.109.1 changes presentation only; it is not an E2E protocol upgrade.

### Active increment: mutual peers and complete installation

- [x] User approved independent mutual endpoint pairing through gateways; both
  devices can offer and consume resources concurrently, with no fixed roles.
- [x] Implement and verify an opaque authenticated endpoint tunnel; exclude native
  admission credentials from gateway-visible messages and reject legacy fallback.
- [x] Remove installer role choices and prepare all supported components through
  one ExtraPane install, retaining explicit driver certificate consent.
- [x] Shared Qt endpoint-pairing dialog routes the code to the selected end device
  through its gateway, blocks pending removal, skips the code for saved identity,
  and does not start media. Offscreen UI checks passed; core integration pending.
- [x] Verify lifecycle, compatibility and isolated security checks; update guides.
- [x] Build and deliver matching Windows/Android packages. Physical verification
  remains a separate gate; no driver installation is authorized by compilation.

### Unified full Windows installation â€” 2026-11-08

- [x] Removed the installer role chooser and all role-dependent component branches. One ExtraPane installation prepares the managed display, camera, audio/USB receiving and USB source components; it does not activate resources or export storage.
- [x] Kept explicit development-certificate consent through **I agree - Next**, unified uninstall ownership and existing Update/Restore/Uninstall maintenance behavior. Previously limited installations receive the full component set when updated.
- [x] Silent setup requires `/ACCEPTDEVELOPMENTCERT=1`. Historical `/SERVER=1` remains a redundant alias; `/SERVER=0` or other values reject installation before stopping the app. Uninstall maintenance is exempt from role and certificate requirements.
- [x] Updated driver-installation documentation and generated package/release notes. Packaging now refuses payloads missing any required managed component.
- [x] Verification: compiled the real Inno Setup script against the existing pre.109 payload into isolated `out/installer-full-role-compile`; Python package/release/driver-test scripts compile; `git diff --check` passed. Disposable-runner driver tests now cover obsolete no-driver flag rejection and legacy full-install compatibility.
- [ ] Fresh install, upgrading a formerly limited installation, restore, unified uninstall and physical component behavior still require acceptance testing. The compile-check installer was not run or delivered and does not represent a new release build.

### Integrated desktop endpoint build verification - 2026-11-08

- [x] Built the real Windows desktop application with `cmake --build out/shared-ui --target ExtraPane`: endpoint RPC/session/service, `connectVia` Qt controller and MOC, Windows native runtime, embedded QML and final `ExtraPane.exe` link all passed. No application was launched or installed. This isolated build is not a packaged delivery.
- [ ] Native macOS endpoint-pairing UI still needs CI compilation; physical cross-network endpoint and media acceptance tests remain pending.


### Authenticated gateway endpoint core verification - 2026-11-08

- [x] Implemented one-hop opaque endpoint tunnels with inner TLS and actual-device pairing. Opposite directions use separate tunnel grant namespaces; endpoint pairing persists no gateway/loopback address as the actual device's direct route. Legacy delegated media requests fail closed.
- [x] Verified isolated service, presence, gateway reverse, asynchronous preparation, reconnect and self-connection checks: all six passed (13.12 seconds). Four additional repeated gateway checks passed after fixing concurrent reverse/revocation tunnel grant collisions.
- [x] The gateway regression verifies wrong endpoint code creates neither trust nor media, canceled and unavailable endpoint attempts preserve the gateway, independent endpoint pairing, automatic reverse catalog connection using the same pairing, no media from catalog connections, legacy delegated-open rejection, bidirectional synthetic native payload delivery, and bilateral Forget through a live gateway while its control links remain available.
- [x] Auth and session checks passed. Wire/policy, private trust store, RPC framing/flow control and loopback media checks all passed; new RPC assertions cover exact endpoint identity payload lengths, empty cancellation/close payloads and fixed-width ready grants.
- [x] Bounded retirement handles late endpoint replies, canceled handshakes, completed tunnel tokens and closed streams without stopping unrelated gateway resources.
- [ ] Remaining acceptance gates: real cross-network/native media tests, malicious relay interception/replay testing and physical platform behavior. Credential confidentiality currently follows the nested authenticated TLS implementation and review; synthetic payload tests are not a packet-level hostile-gateway audit. Unavailable/offline endpoints leave bilateral removal pending until an authenticated route returns.

### Complete-install and endpoint-encryption delivery - pre.110.1

- [x] Built matching **2026.11.09-pre.110.1** Windows/Android packages from clean
  `b4663eed91bc859e090ac8710ca540208af539f5`. Windows app/native version fields,
  extracted portable UI and component manifests passed. Android's 13 host suites,
  integrated compile/lint, pinned signing certificate, release flags, both ABIs
  and 16-KB alignment passed; installed version code is 11001.
- [x] Windows installer copied to this Desktop and the Surface Desktop at
  `10.0.20.143`; saved SSH host identity and both SHA256 copies verified.
  Installer SHA256: `7cb38a112ae83632caa3f517ce817813b0d0142404fb5acb371e85ff90851c2a`.
- [x] Android streamed replacement install succeeded and version was confirmed.
  APK SHA256: `5107007a0d93d68698031880fc224a172e4f028071dec9a9a3e6eb9e5b60f0c2`.
- [ ] Hosted run `42546310429` is building Windows, Android, Linux and both macOS
  architectures. Prior run `42543989485` finished successfully on every platform.
  This feature-branch run does not publish a release or merge to main.
- [ ] User acceptance remains pending: neither Windows installer was run, no
  drivers/security settings changed and no physical media test was performed.
