# Decisions, assumptions, and evidence

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
