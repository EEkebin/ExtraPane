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

## SideScreen, client initiation and persistent driver (0.11)

The user chose SideScreen and requested a private repository under EEkebin. Client enters the server address only; automatic discovery is explicitly deferred. Both apps enumerate their local addresses instead of asking users to configure local binds. Server listens on active IPv4 interfaces; Client follows Windows routing, including static Ethernet paths. The firewall permits scoped LAN ranges rather than requiring a client address.

Start Driver owns a persistent virtual monitor across sequential authenticated clients. Preserve the current geometry between clients and remember it on stop; client mode negotiation remains the initial preference. Keep the COM apartment alive across media pipeline recreation. Close hides setup windows to notification-area icons; tray Exit is the explicit shutdown action. Existing DM driver names, registry locations, pairing names and installation folders remain compatible.
