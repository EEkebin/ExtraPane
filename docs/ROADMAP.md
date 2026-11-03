Current macOS milestone: native Universal receiver source, direct-IP TLS, Apple H.264 display path, mouse/keyboard and ZIP/DMG packaging are implemented on `feat/macos-client`. Both native architectures compile and pass initial checks; the M3 Pro has now passed live Windows-host video and mouse/keyboard delivery after an automatic-LAN TLS compatibility fix. Reconnect/layout and physical Intel playback remain pending. See [MACOS.md](MACOS.md). Mac hosting is implemented in development on `feat/macos-host`: virtual monitors, ScreenCaptureKit/VideoToolbox and mouse/keyboard injection, with the initial M3 Pro host session reported working by the user. Universal host/client packaging and native checks pass on hosted Apple Silicon/Intel runners; multiple-client hardware checks remain pending. iOS follows separately.

# Milestones and verification

Current platform work: the user accepted the Linux handoff and requested the Android client. Its first Pixel 8a Wi-Fi session passes picture and physical touch with hardware H.264 decoding. Native stylus and external-keyboard handling are implemented with real-device synthetic checks; physical active-stylus/external-keyboard validation remains open. Signed APK packaging and release workflow integration are added, with hosted build, lint, signing, verification and artifact upload passed; macOS Apple Silicon/Intel is next after Android verification. Protocol checks and lifecycle validation are documented in [ANDROID.md](ANDROID.md). Linux hosting/receiving and portable hosting pass VM checks, and release CI now includes AppImage generation for both channels, with hosted build, portable startup/shutdown and artifact upload verified. Linux physical touch/pen and broader distribution validation remain open; hosting currently supports one portal-owned monitor and software H.264. See [LINUX.md](LINUX.md).

Implementation is authorized. IPv4 diagnostics, paired TLS, GPU-resident raw-pixel H264 at 720p30 on both laptop GPUs, and local peer-loss/restart checks pass. The GPU runs decoded 300 frames each, presenting 295 on Intel and 296 on GTX, with no application raw-pixel readbacks or CPU pixel uploads. Debug/Release pass five CTest suites and separate GPU color/geometry/order checks. Compressed network buffers remain on CPU; Native input now passes generated integration tests; the repaired physical pen session is user-confirmed and native 3000x2000 nominal60 streaming is now tested (see NATIVE-DISPLAY.md). Individual pen/touch/application acceptance remains pending. See GPU.md, DEVELOPMENT.md, SECURITY.md, and H264.md.

Current priority (user direction): independent extended desktop before more media optimization. The locally signed IddCx package is installed; independent third-monitor creation, virtual capture, unchanged physical topology, and session-loss removal pass. Selected secondary-monitor WGC streaming passes locally and on the laptop. See EXTENDED-DISPLAY.md.

## 0 — Settle scope and stack

Stack and first platform are selected: C++20 native engine, C++/WinRT + WinUI 3/XAML controls, Windows 11 host and receiver, x64 first. See STACK.md. Remaining design work covers representative hardware/pens, target drawing applications, packaging, and driver delivery constraints. Use DM as the working name and one host/receiver session as the initial baseline. Record evidence and decisions in DECISIONS.md.

## 1 — Resolve native integration risks

Use bounded prototypes after design discussion:

1. Direct-IP session over selected Ethernet with static addresses and no DHCP, DNS, internet, discovery, STUN, or TURN. Prove control and media stay on the selected adapter with a competing Wi-Fi/VPN route present.
2. Native hardware encode/decode/render at 1080p60 with latency and queue instrumentation. Validate UI/native surface integration before committing the shell.
3. Capture and inject native pen/touch. Test pressure ramp, tilt, hover, eraser, buttons, and multi-touch in selected real host applications. Document exposed source data and host quantization.
4. Windows+K discovery, connection, video/audio, consent, and input through the Windows Miracast receiver app model on representative hardware. Record unsupported hardware/API paths and whether a companion channel is needed.
5. Virtual display and virtual microphone endpoint feasibility, signing/distribution constraints, install/uninstall, and rollback. Prove the microphone appears and carries actual receiver speech in a Windows recording app.

Each spike ends with evidence and an adopt/change/defer decision. Do not make custom LAN work depend on the Miracast spike succeeding.

## 2 — First useful vertical slice

Pair a Windows host and the chosen receiver over manual IP, stream an existing monitor, and deliver keyboard/mouse including reliable drag and cleanup. Include stop controls, permissions, adapter binding, visible errors, and offline operation. Then add optional discovery without changing manual-IP behavior.

Current progress: high-entropy keys provisioned over SSH into Credential Manager, bounded TLS sessions, explicit host video consent, and a native H264 video-only preview are implemented. Opt-in keyboard/mouse and native touch/pen now pass generated end-to-end tests with watchdog cleanup. Consumer pairing UX, persistent session controls, media optimization, and physical hardware/application checks are still required to complete this slice. See INPUT.md.

## 3 — Native touch and pen

Complete capabilities, coordinate mapping, input lifecycle, stroke batching, pen-first behavior, and diagnostics. Test the capabilities actually exposed by reference hardware. Keep unsupported axes visible. Include disconnects during contact, rotation/DPI changes, and focus loss.

## 4 — Audio and microphone

Add system audio, receiver volume/mute, independent mic forwarding/mute, recording endpoint integration, resampling, timing/drift control, and echo/feedback mitigation. Test grants, revocation, endpoint changes, and buffered-audio behavior at mute/disconnect.

## 5 — Extended desktop and conditional Windows+K mode

Deliver signed virtual display integration with supported resolution/refresh negotiation and safe disconnect topology restoration. Ship Windows+K mode only for the tested hardware/app model and publish its separate feature matrix. Full requested scope is not complete while extended display or mic endpoint work remains deferred.

## 6 — Product polish and release

Installer/uninstaller, per-component driver lifecycle, accessible setup, keyboard navigation, session status, pairing revocation, redacted diagnostic export, recovery, and measured performance. Resolve licenses and distribution obligations before shipping dependencies/drivers.

### Renderer work completed for verification

D3D12, Vulkan and OpenGL client backends and shared app Settings are implemented. Renderer selection is configured before connecting and saved on this PC; live toolbar switching was removed at the user's request. D3D11 remains the default. Shared Settings, unavailable-option handling, GPU presentation, window controls and display reconfiguration checks passed. The user has proceeded to installer work; retain manual hardware acceptance checks. OpenGL requires working D3D11 interop on the selected GPU; unsupported hybrid-adapter combinations fall back to D3D11.

See [D3D12 evidence and limits](D3D12.md) and [Vulkan evidence and limits](VULKAN.md), and [OpenGL evidence and limits](OPENGL.md).

### Launch after installation

Implemented for the reconnect/lifecycle fixes: a **Launch ExtraPane** checkbox on the installer's final **Finish** page, checked by default. Clicking Finish launches the app if checked; unchecking it finishes without launching. Silent installations do not launch the app automatically.

### Installer shutdown and conditional tray behavior

Implemented for the reconnect/lifecycle fixes:

- Before replacing installed files, the installer must gracefully close running ExtraPane instances and stop their server driver runtime, sessions, previews and helpers. Wait for shutdown so files are no longer in use and session-owned monitors/input are cleaned up. Stopping the driver runtime does not mean uninstalling the separately installed display driver.
- Closing the main app hides it to the tray **only while its Server driver runtime is running**, regardless of which sidebar page is selected.
- If the Server driver runtime is stopped, closing the main app exits normally, disconnects any active Client session, and leaves no tray-resident app or helper behind. Client-only use must not require starting the driver or using tray Exit to quit.
- Explicit tray **Exit** continues to stop all active roles and fully quit.

## Acceptance scenarios

| Scenario | Required observable result |
| --- | --- |
| Direct static Ethernet | Example static /24 pair connects and carries video, reverse input, and enabled audio without gateway/DNS or external services |
| Discovery blocked | Manual IP still works when multicast is unavailable |
| Adapter selected | Actual media/control route uses the selected adapter; unplugging does not leak/fail over silently |
| Wrong address/subnet/firewall | Error distinguishes reachability, control handshake, authentication, and media failure |
| Pairing rejected or peer spoofed | No media, mic, or input before authenticated approval |
| Network fails during drag/stroke/key hold | Session-owned state clears within the configured timeout; no stale replay on reconnect |
| Pen feature matrix | Capture and host-app observations verify every advertised axis/tool/button; unavailable fields show reasons |
| Multi-touch | Independent IDs, native manipulation, cancellation, and no duplicate mouse clicks |
| Geometry changes | Accurate mapping with DPI, rotation, letterboxing, negative desktop origins; active contacts cancel safely |
| Keyboard layouts | Physical keys, shortcuts, text composition, release state, and local escape work according to selected mode |
| Audio and mic | Independent mute; mic selectable in a host application; no buffered speech after mute/revoke |
| Extend | Windows detects an additional monitor and moves a window onto it; topology restores after disconnect/uninstall |
| Windows+K | Actual Windows Cast discovery and connection on documented hardware; feature claims match observed behavior |
| Recovery | Lock/unlock, sleep/resume, GPU reset, adapter/address changes, and device removal yield a recoverable state |

Automated checks should cover parser limits, negotiation, authentication state, input lifecycle, epochs, transforms, and queue bounds. Hardware integration checks must confirm delivery and output. Record latency measurement methods, p50/p95/p99, dropped frames, CPU/GPU usage, and run conditions. API success alone does not establish functional pen or touch delivery.

## 0.8 runnable launchers

Native host/receiver windows, persistent Credential Manager pairing, direct LAN settings, continuous normal sessions, opt-in debug pattern/duration and desktop shortcuts are implemented. Current controls are C++ Win32 around the existing engine; the WinUI shell and portable app folders followed in 0.10; automatic reconnect, production installer/signing, audio and microphone remain future work. See DESKTOP-APPS.md for test evidence and limitations.

## 0.9 live display changes

In-place media recovery and saved layout are implemented for resolution, orientation and arrangement changes. Native input is reset and remapped, including portrait letterboxing and negative desktop origins. WinUI 3 and portable Client/Server distribution followed in 0.10; driver signing/installation remains a separate requirement for extended desktop.

## 0.10 WinUI and portable apps

Implemented WinUI 3 Client/Server launchers, self-contained x64 app ZIPs, client screen preference negotiation, and a resizable/fullscreen preview with local auto-hiding controls. Remaining release work: production application/driver signing, clean-machine driver setup, update/installer path and ARM64. Remaining runtime work includes audio/mic, automatic reconnect and physical-monitor change renegotiation on the client.

### Current handoff: server driver installer

Setup offers Client only and Server + Client. Server setup requires the bundled elevated driver dependency and explicit development-certificate consent. Portable hosts use the included driver setup. The driver has a separate machine-wide uninstall entry with exact package/trust ownership tracking. No boot policy changes or automatic reboot are made. CI stages/removes on disposable machines; runtime loading on the user's PCs remains the prerelease handoff. See DRIVER-INSTALLATION.md. Preserve all existing prereleases and wait for explicit user approval before deleting them or publishing stable.
