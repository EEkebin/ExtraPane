# Decisions, assumptions, and evidence

## Multiple client connections and tray lifetime (2026-11-07)

Latest client interaction: each resource has a green power button to connect and
a red power button to disconnect; there is no separate Connect to server button.
Disconnect all stops all rows. A display defaults to a custom virtual monitor (1920x1080 at 60 FPS), with
no local monitor selection required. Matching a selected local monitor's native
mode/fullscreen destination is an alternative, not a prerequisite.

Global and per-resource-group Disconnect all buttons are hidden when their scope
has no active or pending connection. They remain available during connection and
teardown so users can cancel work; stopping one resource does not stop others.
The final resource stopping, failing or being cancelled also releases its server
admission connection. An idle client retains configuration but no server connection;
retiring helper processes alone do not keep Disconnect all visible.

The Windows shared client uses repeatable resource rows with independent source,
destination and lifecycle state. Single server mode supplies one destination to
all rows; Multiple servers mode exposes each row's destination. Running rows
retain their destination until disconnected. Camera and microphone capture can
feed several destinations through bounded native queues; USB remains exclusive
and retains its safe-return rules. See MULTIPLE-RESOURCE-CONNECTIONS.md for
implementation status and tested limits.

Each display row has its own resizable preview window and fullscreen destination.
Closing the main window keeps it in the tray while a server or client connection
is active; with no work it exits. Double-clicking the tray icon restores and
focuses the main window for either role. Tray Exit requests normal disconnect
and server stop, including the bounded USB ownership-return policy below.

## Automatic USB return (2026-11-07)

The user superseded the previous indefinite-quarantine policy after an absent
microSD reader blocked both upgrade and uninstall. Unchecking USB, disconnecting
or exiting first requests receiver flush/eject. If that fails or the connection
is lost, the source ends its own transport/exporter, releases only its exact
binding, proves exclusive local ownership and restores the verified source's
prior online state. Interrupted return is reported as such: it is not a clean
eject and cannot recover writes remaining in the receiver's cache. Identity
changes, foreign ownership or an exporter that cannot be stopped still prevent
unsafe disk mutation.

An absent source does not need to be plugged in to remove its inactive owned
binding: archive the unchanged journal, prove device absence and no exporter,
release the exact private binding, and retire the active journal. No disk is
opened or modified. This is ownership cleanup, not proof of filesystem health.
Upgrades may reuse an exactly verified compatible backend without modifying its
bindings/journals. Uninstall attempts guarded cleanup before removal, with a
clear error for genuinely unresolved ownership rather than a raw script error.
Implementation and verification are tracked in IMPLEMENTATION-CHECKLIST.md.

## Live resource selection (2026-11-07)

The client must be able to connect with no resources selected. While connected,
checking Display, Camera, Microphone or USB starts that resource; unchecking stops
only that resource. One Disconnect all action clears all selections and closes
the client session. Source selection remains available for idle resources.
USB still requires its existing safe eject/recovery lifecycle.

Implement a real persistent control connection, including in automatic LAN mode,
rather than treating a saved server address as a successful connection. Access-code
admission happens once for that connection; resource starts use fresh scoped
credentials. Neither codes nor channel credentials belong in saved settings or
process arguments. Track implementation and verification in IMPLEMENTATION-CHECKLIST.md.

## Device-sharing completion before testing (2026-11-07)

The user requested camera (including Windows sender controls), microphone and
USB implementation before any further physical device testing. Isolated builds
and synthetic checks remain authorized. Track increments in
IMPLEMENTATION-CHECKLIST.md; do not present the camera-only package as completion
of this expanded milestone.

Investigate a local USB Audio Class endpoint through the existing signed
usbip-win2 driver for Windows microphones. This can avoid a new kernel audio
driver/signing dependency while still exposing a normal recording endpoint.
The C++20 app and native capture remain unchanged; an isolated Rust helper is a
concrete integration exception for the MIT usbip-audio library, pinned to
58032ab0664ffec1d0c178f40a3a69359c21f597. Its upstream tests are not our device
verification. Managed driver setup, identity, lifecycle and application delivery
remain gates; never substitute speaker loopback and call it a microphone.

Raw USB storage must remain behind admitted encrypted transport. Stock usbipd-win
starts an additional listener and persistent bindings, so bundling its installer
unchanged does not satisfy this design. Require local ownership, complete disk
classification, source volume locks/dismount, destination eject, and explicit
recovery after interrupted transfer. Preserve independently installed USB/IP
components and never detach unrelated virtual ports.

## Shared interface and resources (2026-11-07)

The user approved Qt Quick with native C++ engines, one managed installation and
one server start/stop control. Clients contribute selected display, camera,
microphone and USB resources. See [RESOURCE-SHARING.md](RESOURCE-SHARING.md) for
the approved direction, distribution gates and actual implementation status.
Existing display applications remain unchanged pending integration and validation.

## Optional access-code admission (2026-11-03)

The user requested direct reachable IPv4 address, port and six-digit one-time codes on every implemented platform, with forwarding handled outside the app. Retain explicit trusted-LAN mode; code mode uses pinned BoringSSL SPAKE2, key confirmation and authenticated records inside existing TLS before display/input allocation. Hosts share a five-attempt, five-minute, one-use admission gate across interfaces and slots. No automatic regeneration, pairing, discovery, relay or UPnP. See ACCESS-CODES.md for current validation and limits.

The user also authorized an original GitHub Pages showcase using the canonical app icon, followed by merge, release and removal of merged feature branches. iOS remains deferred.

## Android receiver milestone (2026-11-03)

The user accepted the Linux handoff and authorized installing/testing an Android client on their ADB-connected Pixel 8a. Keep C++20 shared protocol validation through JNI; use Android's platform UI, TLS sockets and MediaCodec Surface decoding for platform integration. First slice requests native touch, mouse and physical keyboard, with pen/audio/Android hosting explicitly unavailable. API 30 minimum, ARM64 and x86_64, Gradle 8.13/AGP 8.13.2 and NDK 27.2.12479018 are pinned. The first Wi-Fi Windows-host session negotiated 1080x1920 and the Pixel's hardware H.264 decoder at approximately 60 received FPS. The user confirmed the picture and physical touch work. Continue lifecycle and packaged build checks before claiming a release.

The user also requested the missing Linux release automation. Both prerelease and stable publication must include the AppImage and dependency notices, with combined checksums, and depend on successful Windows and Linux builds.

## Current Linux milestone (2026-11-03)

Later user authorization supersedes the historical source-only restrictions below: complete Linux hosting, native input and deployment before Android. Arch KDE/Wayland builds and Windows interoperability have been exercised in both directions. The user confirmed Linux-host to Windows-client video and mouse/keyboard. Native Linux touch/pen capture passes synthetic checks; physical devices remain unverified.

Use the standard ScreenCast/RemoteDesktop portal for a session-owned virtual output and PipeWire capture. Linux hosting currently supports one client, portal-controlled dimensions (1920x1080 on tested KDE) and software OpenH264. Portal mouse/keyboard/touch injection is implemented; pen injection is not exposed and is not granted. Closing the app ends the Linux host; tray hosting and hardware encoding remain future work. Keep these limits visible instead of claiming Windows feature parity. AppImage generation succeeded; packaged runtime and broader distribution validation remain deployment gates. See LINUX.md for current evidence; earlier entries are chronological decisions, not current capability claims.

Initialized 2026-11-01. Direct-IP, paired TLS, and GPU-resident raw-pixel H264 at 720p30 pass on both Surface Book 2 GPUs. Current GPU runs decoded 300 frames each and presented 295 Intel / 296 GTX, with no application raw-pixel readbacks/uploads. Local interruption/restart and GPU color/geometry checks pass. The latest human visual check is pending; the user confirmed the earlier JPEG image. Local virtual-display installation and session-lifetime tests pass; Native mouse/keyboard/touch/pen forwarding passes generated laptop-to-host tests; the repaired physical pen session is user-confirmed. Native 3000x2000 nominal60 now delivers about 59 fps in the measured diagnostic workload (see NATIVE-DISPLAY.md); individual pen/touch features, drawing-app acceptance, audio and production driver distribution remain pending. See GPU.md and the development/security notes.

## Confirmed user requirements

- Polish the project before implementation.
- Local network display with touch, drag, mouse, keyboard, muteable audio, reverse microphone, and comprehensive exposed pen features.
- Investigate Windows+K/Wireless Display compatibility if possible.
- Direct IP on a chosen LAN interface, including two static addresses and a direct Ethernet connection; DHCP must not be required.

## Selected direction and feasibility decisions

| Topic | Proposal | Status |
| --- | --- | --- |
| Connection architecture | Custom LAN mode plus separate Miracast compatibility adapter | Recommended; feasibility pending |
| Core and shell | C++20 native engine; C++/WinRT + WinUI 3/XAML controls; Direct3D presentation | Selected under user delegation; see STACK.md |
| Build tooling | MSVC; CMake presets for engine/tests; MSBuild for WinUI/XAML and WDK components | Selected; compatible versions to be pinned |
| First host | Windows 11 x64 | Selected initial target |
| First receiver | Windows 11 tablet/PC; x64 first, ARM64 after dependency validation | Selected initial target; other platforms deferred |
| Reference laptop | User's Surface Book 2; live Intel UHD 620 and GTX 1050 adapters confirmed | Default Intel path drives the attached panel; explicit decoder-adapter override available for tests |
| Media transport | Native WebRTC, offline local signaling, local ICE candidates | Integration and adapter-binding spike required |
| Current secure prototype | Mbed TLS 4.1.1 / TF-PSA-Crypto 1.1.1, TLS 1.3 PSK-DHE over interface-bound TCP | Tested; separate from the production WebRTC candidate |
| Pair storage | Per-user Windows Credential Manager | Tested in interactive sessions; SSH-only logon cannot access it |
| First video path | D3D11 Desktop Duplication, bounded WIC JPEG frames, TLS transport, native D3D11 preview | Tested and user-visible; temporary codec and diagnostic Win32 shell |
| Current codec path | GPU capture and scaling/conversion, D3D11-surface hardware H264, GPU decode and shader presentation | Tested on NVIDIA desktop and both laptop GPUs; no application raw-pixel readbacks/uploads in H264 |
| Receiver surface lifetime | Copy pooled decoder slice to an owned GPU texture, complete that GPU copy, queue at most one latest frame | Avoids retaining decoder pool surfaces across UI lifetime; GPU copies/fence waits remain |
| Video protocol | Version 7, client display preferences, JPEG/H264, decoder-ready acknowledgement, bounded optional reverse-input batches, capability grants and session epoch | Both endpoints must be rebuilt; no silent older-version compatibility or software fallback |
| Input | Native pointer capture and synthetic touch/pen, scancode/mouse injection through separate watchdog child | Host native app and generated laptop-to-host tests pass; real hardware/drawing-app checks pending; see INPUT.md |
| Extend | One-monitor IddCx, session-owned software device, WGC monitor targeting | Driver builds/package-checks pass; selected physical-monitor capture tested; local driver installed, independent third monitor and capture/loss cleanup verified; see EXTENDED-DISPLAY.md |
| Microphone | Reverse audio plus actual Windows recording endpoint | Required capability, endpoint choice pending |
| Initial scope | One host, one receiver, SDR 1080p60 reference setup | Proposed baseline |

## Open product questions

1. Which pen hardware and drawing applications define the initial compatibility matrix? This is needed for hardware validation, not to choose the stack.
2. Are driver installation and administrative setup acceptable for virtual display and mic endpoint features? Plan separate optional components; do not install them as part of document preparation.
3. Should host speakers keep playing while receiver audio is enabled? Default: yes, with explicit routing controls.
4. Which additional receiver platform should follow Windows? Keep native adapters separable so this does not block the first release.

The original workspace had no existing stack. On 2026-11-01 the user delegated the architecture choice, then asked to start implementation. The user has a laptop ready for two-device testing. Proceed through the bounded feasibility slices without reopening settled stack choices.

## Primary-source findings

These findings support architecture boundaries; they do not establish that the project already works.

- [Microsoft: screen mirroring and projecting](https://support.microsoft.com/en-gb/windows/hardware/display-graphics/screen-mirroring-and-projecting-to-your-pc-or-wireless-display): Windows+K connects to Miracast displays; Windows reception has OS/hardware setup requirements.
- [Microsoft: wireless projection](https://learn.microsoft.com/en-us/windows-hardware/design/device-experiences/wireless-projection-understanding): documents Miracast, infrastructure projection, and user-authorized UIBC input. Protocol-level input support is not proof that a particular app API exposes every feature.
- [Microsoft: MiracastReceiver](https://learn.microsoft.com/en-us/uwp/api/windows.media.miracast.miracastreceiver?view=winrt-26100): Windows receiver API, Wi-Fi Direct requirement, and CoreApplicationView-based session creation. Packaging/app-model compatibility remains a spike.
- [Microsoft: MiracastReceiverInputDevices](https://learn.microsoft.com/en-us/uwp/api/windows.media.miracast.miracastreceiverinputdevices?view=winrt-22621): documented properties include keyboard and game controller. Full pen/touch forwarding cannot be inferred from this API surface.
- [Microsoft: MS-MICE protocol details](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/940d808c-97f8-418e-a8a9-c471dc0d21bb): infrastructure projection setup begins with Wi-Fi Direct peer discovery. Ordinary Ethernet/mDNS advertisement is not an implementation of this path.
- [Microsoft: screen capture](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture): display/window capture, support checks, consent UI, and device/resize handling.
- [Microsoft: Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api): GPU frame surfaces and monitor capture metadata. Capture does not add an extended monitor.
- [Microsoft: indirect display driver model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview): IddCx/UMDF supports network displays and virtual monitors; installation/signing remains separate release work.
- [Microsoft: synthetic pointer injection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-injectsyntheticpointerinput): native touch/pen injection. The pen call takes one pointer; do not infer arbitrary simultaneous pen-device support.
- [Microsoft: pen information structure](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_pen_info): pressure, tilt, rotation, and pen flags; pressure is normalized to 0–1024. Source fidelity and destination precision can differ.
- [Microsoft: SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput): keyboard/mouse injection is subject to integrity-level restrictions. Elevated-window and secure-desktop control are not baseline promises.
- [Microsoft: SYSVAD sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/): virtual audio driver architecture and signing; sample capture uses a generated tone. It must be adapted or replaced to carry network microphone audio.

## Evidence still needed

The TLS dependency and GPU-resident H264 raw-pixel path are tested. Higher resolution/refresh, Opus/WebRTC, WinUI, drivers, signing/distribution, and latency targets remain unvalidated. Measure real Windows+K behavior, pen delivery, competing network routes, static direct-cable operation, mic endpoints, and driver lifecycle on representative hardware.

## Native Surface Book 2 profile

Live laptop inventory reports 3000x2000 and integer refresh 59 (nominal 60 Hz), with the panel attached to Intel UHD 620. Version 0.7 adds a 3000x2000/60 virtual mode, explicit capture dimensions, bounded H264 up to 60 fps, area/rate-derived bitrate (45 Mbps target at native60), optional borderless fullscreen, high-resolution per-thread waitable timers, and input return before decode to overlap host encoding with receiver work. Transport remains protocol v4 with per-frame input batches; no concurrent access to a TLS context. Actual sustained rate is measured separately from requested refresh.

## Desktop launchers and continuous sessions (0.8)

User requested a runnable UI at each endpoint, no SSH UI, Debug off by default, continuous normal sessions and Git tracking. Native C++ Windows-control launchers wrap the existing engine now; the planned C++/WinRT/WinUI 3 shell remains a later migration. Protocol v5 uses zero frames for continuous streaming, with 64-bit media timelines and explicit wire-sequence wrap. Failure/watchdog deadlines remain mandatory. Debug on the host restores the 60-second pattern test. See DESKTOP-APPS.md for pairing, firewall scope, lifecycle and validation.

## Live display configuration (0.9)

Keep the session-owned monitor alive across Windows layout/resolution/orientation changes. Recreate the media pipeline against the selected monitor identity, negotiate new dimensions in protocol v6, reset native input and renew its epoch. Persist the latest layout for the same launcher profile; do not repeatedly apply startup defaults. Portrait presentation uses aspect-fit geometry shared with input mapping. Native synthetic pointer locations are relative to the virtual desktop origin, as documented by [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-injectsyntheticpointerinput).

## WinUI and portable runners (0.10)

Client and Server are now C++/WinRT WinUI 3 setup apps. Keep D3D11 video/native input in the existing engine and its resizable preview, with local auto-hiding controls. Deliver self-contained ZIP folders rather than requiring a global WinUI runtime installation. Pin NuGet dependencies and include runtime licenses and file hashes. The development display driver stays separately installed; portable applications do not imply portable kernel/UMDF installation or production signing.

The authenticated client supplies its screen size and refresh at connection time. Use that as the initial extended-display preference; preserve later Windows layout changes for the same client size. Bound and select supported driver/codec modes with a visible fallback rather than claim arbitrary native resolutions/refresh. Window resizing scales video and never resets the host layout.

## ExtraPane, client initiation and persistent driver (0.11)

The user chose ExtraPane and requested a private repository under EEkebin. Client enters the server address only; automatic discovery is explicitly deferred. Both apps enumerate their local addresses instead of asking users to configure local binds. Server listens on active IPv4 interfaces; Client follows Windows routing, including static Ethernet paths. The firewall permits scoped LAN ranges rather than requiring a client address.

Start Driver owns a persistent virtual monitor across sequential authenticated clients. Preserve the current geometry between clients and remember it on stop; client mode negotiation remains the initial preference. Keep the COM apartment alive across media pipeline recreation. Close hides setup windows to notification-area icons; tray Exit is the explicit shutdown action. Existing DM driver names, registry locations, pairing names and installation folders remain compatible.

Address-list refinement: show connected physical adapters only; omit disconnected, loopback, tunnel and virtual adapters. This is a presentation filter; existing listener and route selection behavior is unchanged.

Lazy display creation and overlay input: Start Driver only listens until a paired client sends valid display preferences. Retain that monitor across later disconnects until Stop Driver. Reserve a 96-by-8 logical-pixel strip at the top center for a downward touch swipe, with a 24-pixel threshold; mouse reveal requires 600 ms hover there. Other title-bar taps and pen events pass through. Ctrl+Shift+Q disconnects the preview, leaving the server listening.

## Unified app (0.13)

User chose one app with Server and Client pages in a left sidebar and About at the bottom. Ship one portable ZIP and one installer for either PC. Keep page state independent. Main UI runs asInvoker; a narrowly scoped elevated helper owns server engine/firewall lifecycle with user-restricted, process-checked IPC. Keep the existing driver, pairings and layout settings compatible.

## Connection quality and renderer rollout

Implement quality before additional Windows renderers. Quality controls and opt-in statistics belong in the receiving preview's Settings menu, not a permanent main-window panel. Adapt transmission dimensions/bitrate without modifying the extended monitor's Windows geometry. Protocol 8 carries explicit bounded feedback; never label local decode/render measurements as end-to-end latency.

The user requested a manual verification stop after every feature. Finish and hand off connection quality first. Only after verification, implement D3D12, then Vulkan, then OpenGL as separate stages. Keep unavailable renderer choices visible but disabled with an explanatory yellow status based on actual runtime/device/interop checks. Linux and macOS work is deferred.

## Automatic LAN connections and multiple monitors

The user explicitly removed pairing and per-connection approval. Start Driver is the permission to accept reachable LAN clients; Stop Driver closes every session. Use ephemeral ECDSA certificates and TLS 1.3, without saved peer trust or a built-in shared secret. This encrypts traffic but does not authenticate the remote PC or prevent an active intermediary. Retain paired PSK mode only for diagnostic CLI compatibility.

The persistent listener admits up to four simultaneous sessions. Each owns its own software-device adapter, capture/encoder, TLS context and input watchdog. Validate the display request before creating a monitor. A disconnect removes only that session's monitor and frees its slot. The listener and other sessions survive. Display creation/removal is serialized and identifies the newly created target rather than selecting the first virtual display. Layout profiles use client IPv4 address and native dimensions; a changed address starts a new profile, and overlapping saved virtual layouts fall back to placement beside existing screens.

Windows still has one pointer and keyboard focus. A process-shared gesture lease prevents different remote clients from overlapping held input; other clients' input is ignored until that gesture is released. Video reception remains independent. This is one desktop with multiple interactive monitors, not separate Windows user sessions.

Validation: four concurrent certificate TLS sessions with bounded fifth-client rejection, malformed-request isolation, and slot reuse; four simultaneous live extended streams (1280x720 and 1920x1080), isolated disconnect/reconnect and physical-topology restoration. Existing monitors are restored together in one Windows display transaction after attachment/removal, avoiding invalid intermediate layouts. Surface hardware pen/touch validation remains a user test before proceeding to About/updater and then additional renderers.

## Responsiveness regression: fixed streaming defaults

After testing pre.21 the user reported worse responsiveness than the earlier releases and prioritized restoring the old behavior over adaptive tiers. Restore native fixed streaming as the default and remove per-frame quality feedback entirely in protocol 9. Retain explicit fixed 75% and 50% stream-scale options, applied only when connecting. There is no adaptive controller, load-triggered encoder/decoder replacement, or load-triggered input reset. Preserve input-before-decode timing, the original bitrate formula and pacing, and the existing multi-monitor functionality. Ignore old adaptive preferences on upgrade. Statistics remain local and optional. Verify Surface responsiveness before moving on to About/updater or other renderers.

## Saved-mode reconnect and app lifecycle

Repeated reconnects on the Surface reproduced a capture startup failure after restoring a saved Windows display mode, including at Native resolution. Windows returns from the layout change before WGC finishes resizing. Before sending the initial offer, retry capture for up to 20 seconds against the same monitor identity and require a stable mode and first frame. Never switch to an unrelated display. Native, 75%, 50%, then Native all passed with the real Surface preview and input enabled after this fix; the persistent multi-client test also exercises saved-layout reconnects.

Quality choices are now one-shot: the preview sends a pending scale to its launcher for the next connection only. Do not write or read saved StreamScale preferences. Consume the pending choice on Connect, default later connections to Native, and reset on failure. A UI test verified Native → 75% → Native while an old saved 50% preference remained ignored.

Keep a tray icon only while the Server runtime is active. Closing a client-only or idle window exits, including its active receiver. Closing a running Server hides it; stopping a hidden Server exits. Installer setup and uninstall request the existing tray Exit command from every instance, then wait for app and driver-runtime mutexes to disappear. Abort file replacement if graceful shutdown times out. The Finish-page launch checkbox is checked by default and skipped for silent installations. This stops the runtime, not the separately installed driver package.

## D3D12 and shared Settings

The next renderer is native D3D12 on the client, followed by Vulkan and OpenGL only after separate user verification. Keep server capture and H.264 encoding/decoding unchanged. D3D12 shares an NV12 texture with the D3D11 decoder on the same adapter, synchronizes the GPU copy with a shared fence, and draws/presents with native D3D12 commands. Keep D3D11 as the default and fallback; no production CPU pixel bridge.

The user changed the interaction requirement during implementation: configure the renderer beforehand in a shared main-app Settings page, not through live switching in the hover toolbar. Save the renderer locally and apply it on Connect. Lock the selector during a client session. Group client input/debug and server input/debug options on that same page, respecting each role's running state. Unavailable renderers stay visible and disabled with a yellow explanation from device/interop checks. Actual stream resources are checked again at connection time; failure reports its reason and falls back to D3D11. Statistics and one-shot resolution choices remain in the preview toolbar.

Pixel/color/crop/aspect-fit checks passed on RTX 4090, Intel UHD 620 and GTX 1050. Surface D3D12 native 3000x2000 runs decoded 360 frames each, presenting 354 Intel / 349 GTX, with zero application raw-pixel readbacks/uploads. These were short functional runs, not measured latency improvements. Preview window controls and live display reconfiguration passed with D3D12 active. Shared Settings passed renderer persistence across restart, active-session locking, independent role options, and clean exit. Control reparenting uses the named owning panels rather than relying on a mounted visual-tree parent. A synthetic unavailable-device UI fixture verified the disabled choice, explanation and fallback. See D3D12.md.

Stop for user verification after this D3D12 release before starting Vulkan or OpenGL.

## Vulkan Windows client renderer

The user requested Vulkan after the D3D12 handoff. Add it to the same saved, pre-connection Settings selector; retain D3D11 as the default. Dynamically load the system Vulkan runtime so its absence does not break other modes. Match the decoder adapter by LUID and require actual Win32 presentation, external-memory import and keyed-mutex support. Keep each optional renderer's availability/reason independent.

Reuse D3D11 GPU decode/color conversion into a shared BGRA texture, then use native Vulkan blit/scaling and swapchain presentation. This avoids a CPU bridge and permits explicit import/synchronization checks across the tested Intel/NVIDIA adapters. Do not call this an all-Vulkan decoder or promise faster performance. Vulkan-Headers is pinned and header-only; no SDK/loader/driver is shipped. See VULKAN.md for the synchronization contract, tests and measurements.

GPU validation passed on desktop RTX 4090 and both Surface GPUs. Native Surface streaming, shared Settings persistence/locking, unavailable-option handling, window controls and saved-layout reconfiguration passed. Stop for user verification after this Vulkan release before OpenGL.

## OpenGL Windows client renderer

The user requested OpenGL after the Vulkan handoff. Add it to the same saved pre-connection selector and retain D3D11 as the default/fallback. Use D3D11 GPU decode/color conversion with WGL_NV_DX_interop2 texture registration and lock/unlock synchronization, followed by native OpenGL framebuffer scaling and Windows presentation. Do not add a CPU pixel bridge or promise improved performance. Probe real device sharing and framebuffer completeness, not only the OpenGL version or extension list.

Desktop RTX 4090 and Surface Intel UHD 620 passed GPU pixel validation. The Surface GTX 1050 decoder was rejected because its shared framebuffer was incomplete with the Windows-selected OpenGL context; this hybrid-adapter limitation must remain visible. Settings persistence/locking, synthetic unavailable-driver handling, window controls, live display reconfiguration and saved-layout reconnect passed on the desktop. See OPENGL.md for evidence and limits. Stop for user verification after this feature.

## Required server driver installation and release handoff

The user requested driver installation through Setup and clarified that the driver is required for hosting. The installer offers Client only and Server + Client; the latter cannot continue if the elevated driver dependency fails. The unified app retains both pages. Portable hosts run the bundled driver installer separately.

Development certificate trust requires explicit consent. Keep the app per-user and the driver manager machine-wide in Program Files, with a separate Windows Apps uninstall entry and protected package/ownership records. Remove only packages and certificate entries added by that manager; preserve older manual installations and other users/portable copies when uninstalling the app. Never change Secure Boot, Memory Integrity or test-signing policy to make installation pass. Staging is not proof of runtime loading. See DRIVER-INSTALLATION.md.

Rewrite the README as straightforward, accurate usage and installation documentation. Publish a prerelease for the user to verify this change. Retain all existing prereleases until the user explicitly gives the green light to delete them and publish a date-tagged stable release. No stable publication or prerelease deletion is authorized before that approval.

## Linux x64 AppImage client

After the Windows stable release, the user selected Linux AppImage development and explicitly prohibited testing while playing a game. Start with a Windows host and Linux receiver, targeting KDE Plasma/Wayland first; Linux hosting follows as a separate milestone. Do source work only until testing is authorized: no builds, execution, GPU/connection tests, display changes, driver operations or CI dispatches. macOS remains deferred.

Retain C++20 and the shared protocol 9 serializer. Use Qt 6/Qt Quick for the Linux app, OpenSSL TLS 1.3 for Windows automatic-LAN interoperability, and GStreamer H.264 decoding/OpenGL presentation through qml6glsink. Keep the media/network path off the UI thread and queues bounded. Mouse/keyboard are the initial return-input families; do not advertise touch/pen before native implementation. The Linux Server page stays disabled until real virtual-monitor hosting exists. See LINUX.md for the source-only status, packaging draft and acceptance gates. Do not publish an AppImage before those gates and dependency packaging are validated.

## ExtraPane rename (2026-11-03)

The user selected ExtraPane to distinguish this project from another display application named SideScreen. Rename app branding, executable/artifact names, build metadata, packaging, documentation and the repository without changing the wire protocol or driver device identity.

Keep the legacy installer AppIds, cross-version exclusion mutexes, Windows settings keys and fixed privileged `Program Files\SideScreen Driver` directory. These are compatibility identifiers, not visible product names. This retains existing install locations and driver package/certificate ownership backups, avoiding a second independently registered driver manager. The new manager accepts original certificate filenames in verified ownership backups. Installers close both old and new app window classes, remove known obsolete app filenames/shortcuts and install the new names. Fresh per-user app installations default to ExtraPane; upgrades reuse their existing directory.

Published releases, tags, checksums and signed packages remain historical artifacts; do not relabel old binaries as rebuilt ExtraPane versions. The rename and Linux source have not been built or tested. No app launches, GPU probes, driver operations or CI runs are authorized during the user's current no-testing period.

## Linux VM build authorization (2026-11-03)

The user supplied an Arch Linux KDE/Wayland Hyper-V VM and explicitly approved installing dependencies and building the Linux client with two jobs. This supersedes the source-only restriction for that VM build. Compilation and linking succeeded; fix the compiler formatting warnings and record actual dependencies in LINUX.md. App launches, streaming/input tests, Windows builds, driver operations, display changes and release publication are outside this authorization. AppImage packaging and real-hardware performance remain unverified.

## Linux connection verification and toolbar fixes (2026-11-03)

The user authorized a Windows-to-Arch VM connection test and confirmed video, mouse/keyboard, reconnect and fullscreen. Screenshots and feedback identified mixed theme contrast and unreadable toolbar controls. Fix the Linux UI using a consistent Qt Controls style, explicit toolbar contrast and local F10/F8 input suspension. Do not interpret VM mouse feedback as proof of a transport fault or claim Linux touch/pen support based on synthetic events. Rebuild on the VM, preserving the current session until the user restarts the client.


## Android stylus, keyboards and distribution (2026-11-03)

Implement native Android stylus forwarding and USB/Bluetooth keyboard handling before macOS. Preserve actual reported pressure, tilt, hover, primary barrel and eraser semantics; do not manufacture barrel rotation from Android direction. Reset owned input on input-device removal/change. Keep unsupported axes/buttons and host grants visible. Android hosting remains deferred: MediaProjection screen sharing is distinct from an extended desktop and remote control needs separate platform permission/integration.

Build and release adds a persistently signed Android APK with pinned toolchain, certificate verification and numeric upgrade versioning. Fork PRs never receive release signing secrets. macOS (Apple Silicon and Intel) follows Android user verification. Physical Android stylus verification remains pending; synthetic event tests are not hardware evidence.


## macOS receiver milestone (2026-11-03)

The user approved macOS after the Android input/packaging checkpoint and specified Apple Silicon plus Intel. Use Objective-C++20/AppKit, Network.framework TLS 1.3 and Apple's CoreMedia/AVSampleBufferDisplayLayer H.264 path, retaining the shared C++ wire protocol. Build Universal arm64/x86_64 for macOS 12+ and validate natively on both hosted architectures. First implement receiving with mouse/keyboard; Mac hosting and iOS remain separate milestones. Trackpad gestures are not evidence of native remote touch, and tablet mouse events are not native pen support.

Package a ZIP and drag-to-Applications DMG. Ad-hoc signing is a preview packaging measure, not Developer ID signing/notarization. Do not change Gatekeeper settings or imply notarization. Real M3 Pro connection, presentation and input validation require the user's Mac access; hosted build/codec tests do not replace them. Stop at this receiver checkpoint for user verification before hosting or iOS.


## Physical Mac automatic-LAN TLS compatibility (2026-11-03)

The M3 Pro test reached TCP but Network.framework rejected the Windows ephemeral certificate with TLS -9808 before the custom verification callback. Set peer authentication explicitly off for this connection, matching product automatic-LAN semantics; preserve TLS 1.3 and do not alter Keychain/system trust. This completed TLS and negotiated 3000x2000. The user confirmed picture and mouse/keyboard input. Surface the underlying Network.framework error for waiting/failed connections instead of reporting a misleading timeout. Keep paired/authenticated modes separate.

The physical Mac test also exposed the F11 Show Desktop conflict. Use the native Control+Command+F menu equivalent and a View > Toggle Full Screen action; preserve the user's system keyboard settings. F11 remains a fallback only when delivered to the app.

The user confirmed native Mac fullscreen works, then identified that hovering to reveal controls appeared to freeze the stream. The reveal handler was setting the persistent input-pause flag. Reveal must not pause input; F8 is the explicit pause control, dialogs suspend input only while open, and hover keeps the toolbar visible. The user confirmed continued mouse/keyboard input after this fix.


## macOS virtual-monitor host (2026-11-03, implementation in progress)

The user explicitly requested a real extended Mac desktop, matching the other hosts. Use runtime-checked CGVirtualDisplay for monitor creation, with a separate same-bundle owner process per connection so process/pipe lifetime bounds ownership. This undocumented interface is isolated and may break on future macOS versions; no App Store or long-term private-API compatibility claim is made. A physical M3 Pro probe created an independent non-primary 1280x720 display and returned the online display list to its original count afterward. Probe cleanup must enumerate displays: CGDisplayIsOnline returned -1 for a retired ID and initially caused a false failure.

The host requires macOS 13+, Screen Recording permission, and optional Accessibility permission for mouse/keyboard. ScreenCaptureKit supplies GPU-backed pixels to VideoToolbox H.264. Reuse the pinned Mbed TLS dependency for an ephemeral TLS 1.3 listener without Keychain trust or saved pairings. Bind to a selected connected LAN address and create no monitor until a valid client request. Support four client slots, each with independent display/capture/input cleanup. Keep the listener alive when clients leave. Reconfiguration changes the offer/epoch on the same connection; coordinate mapping follows the current display arrangement. Native Mac touch/pen injection and audio remain unsupported.

Physical capture/input, simultaneous clients, reconnect and packaging gates remain open until tested. Do not substitute mirroring of the built-in screen for a virtual-monitor failure.

The user enabled both Mac permissions and reported the host session working, then requested natural shortcut modifiers. Incoming Mac-host keys now map Ctrl to Command, Windows/Super to Control, and Alt to Option, preserving sides. Apply the mapping before session ownership tracking so key-up/reset uses the same native key as key-down. Client wire encoding stays unchanged. Physical shortcut verification remains pending.

The modifier build passed native protocol/input/media checks on the M3 Pro. During replacement, SIGTERM stopped the listener and removed the session monitor (online display count returned to one), but AppKit termination waited inside the dispatch-source callback. Schedule termination through the run loop instead of entering the termination wait from a main-dispatch callback.

Automatic route now works for Mac hosting: enumerate the same connected physical IPv4 addresses shown in the UI, then bind a separate listener to each, sharing four session slots. Explicit selection binds only that address. Never bind a wildcard address or require a default internet route. The listener snapshot updates on server restart; Refresh preserves a still-present explicit selection.

Host/client Universal deployment validation passed in run 41738412800 on both hosted Mac architectures, including extracted ZIP app checks and DMG integrity. This validates packaging, not a physical Intel host. At the user's request, ExtraPane was stopped and removed from the M3 Pro, along with its entire preview workspace, settings/logs and app-scoped privacy permissions. No app process, listener or virtual monitor remained; do not reinstall there without new authorization.

## Four-platform release authorization (2026-11-03)

The user deferred iOS before implementation and explicitly requested merging the completed Windows, Linux, Android and macOS work and publishing a release. Consolidate the platform branches, keep all four package jobs required for publication, preserve the existing Android signing identity and Windows upgrade identifiers, and retain the documented preview limitations. No installation on the user's Mac is authorized.

## Server accepts client-selected resources (2026-11-07)

The user reconfirmed that the server starts once without an exclusive resource
mode. Each client independently selects display, camera and microphone (and
eligible USB storage), including concurrent display reception and reverse media
forwarding. One client's choices must not replace server-wide resource settings.
The current per-mode development dropdowns are temporary test scaffolding, not
completion of this shared-session requirement. Preserve independent permissions,
capability failures and stop/mute controls for each resource.

The first server unification keeps existing client wire formats. One TLS listener
classifies an encrypted, retained prefix and dispatches independent sessions;
resource-specific components are prepared lazily. There are no separate public
resource ports. The shared server dropdown is removed. Client checklist/grouped
admission remains a separate unfinished item; one-use access codes have not been
silently made reusable to simulate that feature.

## Multi-select Windows client (2026-11-07)

Remove the exclusive client resource dropdown and use the real resource checklist
for all selections. Each camera/microphone/USB source has independent inventory,
selection, status and stop controls; display receives while media sources send.
One Connect action launches the selected resource channels. Failures are isolated
per resource. A code-protected selection first authorizes once and receives
one-use, resource-bound child tickets; never silently reuse the six-digit code.
The current implementation uses separate protected streams, not one multiplexed
TCP connection. Selection changes require disconnect; stopping/muting individual
resources does not stop the others.

## Consolidated Windows layout (2026-11-07)

The user requests one ExtraPane installation instead of scattered SideScreen and
ExtraPane component folders. Use Program Files/ExtraPane for the app and managed
components, with one visible app entry. Preserve stable internal installation IDs
for upgrades and retain exact driver ownership records during migration. Independent
usbipd-win/USBip installations are not ExtraPane-owned and must remain untouched.
A retained legacy rollback folder is an incomplete cleanup step, not proof of full
consolidation. Installation, upgrade and unified removal need end-to-end validation.

## Custom Windows display refresh target (2026-11-07)

Custom display targets accept 1-360 FPS/Hz; the default remains 60. This is a requested mode and stream target, not a delivery guarantee. Capture, encoder, decoder, network and presentation hardware must support the requested rate. Driver 0.9 extends the private mode descriptor to three hexadecimal refresh digits, preserving legacy descriptor decoding. Camera and JPEG limits are unchanged.

## Grouped resources and remote keyboard (2026-11-07)

Resource groups collapse without stopping connections and disconnect independently. Hide routine component-presence messages, preserve actionable failures. Windows automatic keyboard requests target the streamed host monitor after confirmed editable touch; use the OS accessibility keyboard, preserve unrelated instances and expose limitations. A complete modern touch keyboard/IME integration is not claimed.

## Desktop shell presentation (2026-11-07)

Open on Server by default without starting it automatically. Use centered resource
group titles with separate compact icon actions, accessible names and tooltips.
The desktop navigation can collapse into an icon rail; collapsing navigation or
a resource group never changes connections. Keep actionable status visible and
move general usage limits out of the primary connection workflow.

## Installer role and consent flow (2026-11-07)

Role choice is Client only or Server + Client. USB source support is mandatory in both roles, with no checkbox or separate optional setup flow. Driver certificate information remains explicit; the driver page uses an I agree - Next action instead of a checkbox. Silent server deployment still requires /ACCEPTDEVELOPMENTCERT=1. Installation never selects or exports storage automatically.

## Dynamic Windows resource capacity (2026-11-07)

Replace fixed product counts with dynamic endpoint/session allocation and remove
the Connection limits UI section. Preserve bounded pending admissions, tickets
and per-recipient queues, plus exact USB ownership and per-device exclusivity.
Do not advertise unlimited hardware capacity: endpoint/driver allocation may
fail. The current USB source backend retains its one-exporter-process constraint
until a coordinated backend/broker redesign preserves its attachment evidence.

## Remote speakers (2026-11-08)

The user confirmed that a client's chosen speaker/headset must appear as a normal
playback device on the server. This is independent of reverse microphone forwarding
and does not automatically copy system audio or change global defaults. Add Speakers
above Microphones, with repeatable connections, per-session mute and volume. Initial
implementation is a Windows virtual playback endpoint and Windows/Android output;
unsupported Linux/macOS paths remain explicit. Reuse the managed USB/IP audio backend
and existing installer ownership rather than introducing a new kernel driver.

## Device discovery and remembered pairing: next branch design

Status: design decisions for `feat/device-discovery`, branched from the preserved
`feat/shared-resources` baseline. These describe intended behavior, not capabilities
of the currently shipped app. Do not merge either branch into main until the user
says the work is ready.

- Devices are equal participants: each may offer local resources and use remote
  resources concurrently. Proposed primary pages are Devices, Share and Connections,
  with Settings/About retained. Server/client remain implementation roles.
- Advertise only resources explicitly enabled for sharing. Advertising does not
  start camera/microphone capture or export a USB disk. Retain direct IPv4/port and
  selected-interface access when discovery is unavailable, including static-IP LANs.
- Pairing codes are optional and enabled by default. Remembered access uses device identity/trust rather than
  treating a stored short code or IP address as proof of identity. Codes rotate only on a fresh app/sharing-service startup, not on pairing or reconnect. Trust must be revocable, and disabling
  pairing must be explicit; do not imply peer identity verification in open mode.
- Default sharing lifecycle (delegated choice): remember offered-resource selections,
  provide Start sharing / Stop sharing, and start sharing manually by default.
  Stop sharing withdraws offers and ends sessions using them. Merely offering a
  resource does not activate it.
- Keep two explicit display modes: Extend desktop creates a virtual monitor on the
  computer using another device as its display; Share existing screen captures an
  already-existing display. Do not call existing-display capture an extended monitor.
- Virtual monitor configuration retains both matching a selected receiver monitor's
  native resolution/refresh and custom resolution/target FPS. Selecting a physical
  monitor remains optional. Hardware/OS limits remain visible.
- Existing-screen sharing is now in scope; its default is view-only, with input explicitly enabled. Existing virtual-display input semantics remain intact.
- Brief network loss should trigger bounded retries for non-USB resources, provided
  permission/trust and resource availability still hold. Never silently fall back to
  another camera, microphone, screen or output. USB reconnect requires explicit action
  and remains exclusive; preserve safe ownership-return behavior.

Implementation gates: settle pairing lifetime and existing-screen input defaults;
then define capability/identity/discovery messages and cross-platform adapters;
implement device/resource discovery with direct-IP fallback; add trust and revocation;
connect resource selection to existing media sessions; add existing-screen capture
and bounded reconnect. Preserve current device-sharing paths while migrating.
Security checks must cover code replay/rate limits, remembered identity changes,
revocation and cross-device resource isolation before claiming the new pairing mode
ready. Physical-device validation remains a separate gate.

## Device discovery implementation authorization

The user authorized implementing this design on the child branch. UPnP is explicitly
excluded. Manual port forwarding uses the offering device's reachable address/port;
LAN discovery is optional convenience. The using device initiates a connection to
the offering device. Media may flow in either direction; the virtual-display driver
always belongs on the computer whose desktop is extended.

Gateway sharing is disabled by default. When explicitly enabled for a trusted
visitor, the gateway delegates its authorized access to its paired devices without
requiring that visitor to pair separately with each destination. This does not
create permanent visitor/destination trust. Enforce one gateway hop, ExtraPane-only
resource sessions, existing resource permissions, bounded queues and immediate
revocation/stop. Gateway discovery alone is insufficient: relay traffic over an
authorized session when private destinations cannot be reached directly.

Accept localhost and IPv4 loopback for manual/self connections while excluding
loopback from LAN address listings. IPv6 support requires an explicit transport
gate; never treat an unsupported address as a different destination. Self-extension
still uses the real media pipeline unless a separate local path is implemented.

For manual reconnection, a unique saved address/port may select the remembered
identity to authenticate. The address is only a lookup hint: the peer must prove
the saved secret. An explicit selected identity takes precedence. Ambiguous saved
addresses require choosing a saved device; a failed identity proof never falls
back to pairing or unverified access automatically.

## Device workspace is the normal launch path

The user clarified that the delivered app should open Devices / Share /
Connections without a flag. This supersedes the temporary opt-in migration plan.
Desktop normal startup uses the new workspace; `--devices` remains an alias and
`--legacy` explicitly opens the older interface. Native helper and diagnostic
commands retain their dispatch behavior. Android's launcher opens device sharing,
with explicit Legacy connections navigation. This does not imply physical-device
acceptance or authorize merging the branch.

## Resource controls and navigation consistency

Resource rows use power buttons, not checkboxes: green enables/offers or connects
the resource; red disables/withdraws or disconnects it. Action labels distinguish
offering a resource from starting an active connection. Pairing, input permission
and gateway preferences remain settings controls. Merely enabling an offer does
not activate its camera, microphone or other native capture.

Desktop navigation uses a fixed hamburger icon, matching Android. The sidebar
may slide, but its toggle does not rotate or swap between arrow glyphs.

## Individual resources start sharing; discovery is the default entry

This supersedes the earlier separate Start sharing step. Enabling the first
resource validates the selected address/port and starts sharing automatically.
Disabling the last offered resource stops the listener; explicitly enabled gateway
sharing also counts as an offer. Stop sharing all clears local offers and gateway
sharing, withdraws advertisements and ends incoming use while preserving outgoing
resource connections. Startup failure must leave no falsely enabled resource.
Fresh app startup does not silently restore active offers, and stale listener
events after stop/restart cannot reactivate sharing. Pairing-code lifetime is unchanged.

Desktop Devices browses Nearby devices by default, with collapsed Connect by address
as an equally functional direct-IP fallback. Do not label it External: static-IP LAN
and localhost connections use it too. Discovery is an untrusted local-network hint;
paired identity is still verified by the authenticated connection. Android automatically
advertises enabled offers; its current workspace remains a resource provider.


## Available resources by default; power represents actual use

This supersedes opt-in resource offers and the first-offer/last-offer listener
lifecycle above. Normal app startup makes supported local resources discoverable
and available through an authenticated catalog. Pairing remains enabled by default;
discovery alone does not grant access. Camera/microphone capture, virtual monitor
creation and USB ownership begin only after an authorized resource request. OS
permissions and endpoint capability requirements still apply.

A provider's idle resource is Available, not red. Red power identifies actual
incoming use of that exact resource and disconnects that resource's users. The
consumer's corresponding resource is red while connected. Show who uses each
resource, and preserve unrelated connections when stopping one. Disconnecting all
active resources leaves the listener/catalog available for subsequent requests.
Gateway sharing remains a separate, disabled-by-default setting. Android retains
its foreground-only lifecycle and never resumes old capture sessions simply by
returning to the app.


## Matching Windows build for every Android revision

Every delivered Android revision also produces the matching Windows version,
even when its changes affect Android only. Deliver the installer to both the
local and Surface Desktops. Do not finish an Android delivery leaving Windows
on a previous version. Windows installation remains a user step.
