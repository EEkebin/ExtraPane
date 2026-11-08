# macOS preview

The macOS app receives a Windows or Linux host's display using the same protocol 9 and automatic LAN mode. It targets macOS 12 or newer and contains both Apple Silicon (arm64) and Intel (x86_64) code in one Universal app. The development host also creates a real extended Mac monitor on macOS 13 or newer; iOS remains a separate milestone. The host/client Universal ZIP and DMG pass hosted packaging validation on Apple Silicon, with native build/checks also passing on Intel.

## Install and connect

Download the macOS Universal DMG or ZIP from the matching build/release. Drag `ExtraPane.app` to Applications, or extract and run the app from the ZIP. The preview is ad-hoc signed, not Developer ID signed or notarized. Gatekeeper may block a downloaded copy; use macOS Privacy & Security approval only if you trust the source. Do not disable Gatekeeper globally. No driver, Accessibility permission, Screen Recording permission or administrator install is required for this receiver. Approve Local Network access if macOS asks.

Start the server on Windows or Linux. On the Mac, enter its numeric IPv4 address, optionally select a connected Ethernet/Wi-Fi source address, and select Connect. Automatic routing is the default. Interface selection binds the connection to the selected local address; static IPv4 over Ethernet works without DHCP, DNS, multicast or internet. Address lists exclude loopback and inactive interfaces and currently show physical `en*` interfaces. VPN/tunnel selection is not implemented.

The client requests its current screen's backing-pixel dimensions and a 60 FPS target. The host chooses a supported mode. Resize, maximize or use the green macOS fullscreen control. Video keeps its aspect ratio and input coordinates account for letterboxing. Host display reconfiguration flushes the decoder, releases owned input and renegotiates on the existing TLS connection. Disconnect removes that client's host session; it does not stop the server.

## Controls and input

| Action | Control |
| --- | --- |
| Show controls | F10, View > Show Controls, or move the pointer to the top-center strip |
| Pause/resume remote input | F8 |
| Fullscreen | Control+Command+F, View > Toggle Full Screen, or the green window button |
| Disconnect | Ctrl+Shift+Q or Disconnect |
| Reception statistics | Settings > Show statistics |

Depending on keyboard settings, use Fn with F8/F10. macOS may reserve F11/Fn+F11 for Show Desktop, so use Control+Command+F for fullscreen. Revealing controls does not pause remote input. They hide automatically after the pointer leaves them. Only F8 explicitly toggles input pause. Settings/About dialogs temporarily suspend input and restore its previous state when closed. Controls also hide after connection. Closing the window quits the client; there is no idle tray process.

Mouse movement, buttons, dragging and scrolling are forwarded. Physical keyboard positions map to Windows set-1 scan codes, using the host layout. Command maps to the Windows key; Option maps to Alt. macOS-reserved shortcuts remain local. IME composition, arbitrary vendor/media keys and text injection are not implemented. Trackpad scrolling is mouse input, not native multi-touch. Tablet pressure, tilt, eraser and native pen forwarding are not implemented; devices presenting as ordinary mouse pointers can only provide pointer input.

Focus loss, local controls, disconnect and reconfiguration reset session-owned input. Queue overflow closes the session rather than silently dropping key/button releases. Input permissions are granted by the host and unavailable families are not requested. Audio and microphone forwarding are not implemented.

## Host a Mac desktop

Leave **Automatic route** selected to listen on all currently connected Ethernet/Wi-Fi IPv4 addresses, or select one address to restrict hosting to that interface, then choose **Start Server**. Loopback and VPN/tunnel interfaces are excluded. Automatic hosting does not require a default gateway or internet access. Restart the server after network-address changes to refresh its listeners. Allow Screen Recording for capture and Accessibility for remote mouse/keyboard in macOS Privacy & Security. Restart the app if macOS requests it. Without Accessibility, clients are view-only.

A client connection creates its own virtual monitor; starting the listener alone creates none. The implementation has four client slots and removes each session's monitor on disconnect while keeping the listener running. While hosting, closing the window keeps the server in the menu bar; Stop Server or Quit ends hosting.

Remote keyboard shortcuts use **Ctrl ? Command**, **Windows/Super ? Control**, **Alt ? Option**, and unchanged Shift, preserving left/right modifiers. For example, Ctrl+C/V/Z invokes Mac copy/paste/undo. The host performs this conversion before tracking pressed keys, including cleanup on disconnect. The Mac client still sends its documented physical-key mapping; this policy applies to incoming input at a Mac host.

Virtual monitors use the undocumented CGVirtualDisplay interface, checked at runtime. Future macOS versions may break it. ScreenCaptureKit captures that monitor and VideoToolbox encodes H.264; no driver installation is needed. Native touch/pen injection, audio and microphone forwarding are unavailable. The user reported the initial host session working on the M3 Pro; multiple simultaneous clients and physical shortcut delivery still require verification. [Host/client deployment packages passed CI](https://github.com/EEkebin/ExtraPane/actions/runs/41738412800), including extracted-app checks; no public release was published.

## Native implementation

- AppKit UI and Objective-C++20 platform adapter, sharing the existing C++ protocol parser/serializer.
- Network.framework TCP/TLS 1.3, port 43824, with connection-local acceptance of the automatic LAN server certificate. Traffic is encrypted but peer identity is not verified. Use a trusted LAN.
- Bounded encoded H.264 packets, Annex B to AVCC conversion, CoreMedia sample buffers and AVSampleBufferDisplayLayer. Apple controls the decoder/presentation implementation. This is not a custom Metal renderer, and hardware decode is not guaranteed on every machine/format.
- No application raw-pixel readback or upload in the receiving path. Encoded packets remain CPU memory. Samples are marked for immediate display; decoder backpressure is bounded to 500 ms before disconnecting. This bounds a stall, not a claim of measured end-to-end latency.
- Input acknowledgements are sent before submitting video for presentation. Source dimensions are checked against the negotiated offer.

## Build and package

On a Mac with Xcode command-line tools, CMake 3.28+ and Python 3.10+:

```sh
export EXTRAPANE_VERSION=2026.11.03-pre.0
cmake -S . -B out/build/macos -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build out/build/macos --parallel 3
ctest --test-dir out/build/macos --output-on-failure
python3 scripts/package_macos.py
```

The packager checks both executable architectures, creates the app icon, ad-hoc signs and verifies the bundle, and generates ZIP/DMG artifacts with SHA-256 sidecars. A DMG is a drag-to-copy distribution, not a privileged installer. To uninstall, quit and remove the app. Preferences remain in macOS defaults under `io.github.eekebin.extrapane`; no Mac driver is installed. Replace the app with a newer build to update. No in-app Mac updater is implemented.

The reusable `macos.yml` workflow builds Universal binaries and runs native checks on `macos-15` (Apple Silicon) and `macos-15-intel`. `Build and release` accepts `macos_only=true` for validation without publishing or building other platforms. Full releases require all platform jobs and include the Mac ZIP/DMG in the combined checksums.

## Verification status

The Universal app compiled and protocol/input/framing checks passed on hosted Apple Silicon and Intel runners. ZIP/DMG generation passed on Apple Silicon after correcting the architecture-check invocation. The native H.264 encode/conversion/decode color check also passed on both runners. AppKit startup/shutdown also passed on both hosted architectures. [Verified build and packages](https://github.com/EEkebin/ExtraPane/actions/runs/41729302343). Hosted checks are not evidence of physical Mac video presentation, user input delivery, display reconfiguration, network permissions or latency. On 2026-11-03 the user's M3 Pro MacBook Pro running macOS 27.2 passed the native codec check and connected to the Windows host at 3000x2000. The user confirmed the picture and mouse/keyboard work. The first connection exposed Apple TLS error -9808; explicitly disabling per-connection peer authentication, consistent with automatic LAN policy, fixed it. Global trust/network settings were unchanged. Control+Command+F fullscreen and continued input after revealing the top controls were also verified by the user. Repeated reconnect negotiations succeeded during testing. Physical Intel playback, Linux-host interoperability, live host layout transitions and latency measurement remain pending. macOS 12 compatibility is a deployment target, not a completed physical-OS test matrix.

## Apple references

- [Network.framework](https://developer.apple.com/documentation/network)
- [AVSampleBufferDisplayLayer](https://developer.apple.com/documentation/avfoundation/avsamplebufferdisplaylayer)
- [CoreMedia H.264 format descriptions](https://developer.apple.com/documentation/coremedia/cmvideoformatdescriptioncreatefromh264parametersets(allocator:parametersetcount:parametersetpointers:parametersetsizes:nalunitheaderlength:formatdescriptionout:))


For connection diagnostics, the app logs TLS state, negotiated dimensions/grants and the ending reason through NSLog; it does not log typed keys. Launch with `-server <IPv4> -connectOnLaunch YES` to connect automatically for a diagnostic run. This opt-in launch argument is not a saved auto-connect preference. The normal app still waits for Connect. Network errors include the platform error code instead of hiding a TLS failure behind a generic timeout.

## Access-code connections

Code-enabled builds add a TCP port (default 43824) and optional six-digit access code on the Client page. Use matching releases. Enable Require access code before starting the server; Generate new code admits another connection without disconnecting existing sessions. See [access-code setup and security](ACCESS-CODES.md) for expiry, retry limits and reachable internet endpoints. No-code mode is still restricted to trusted-network use.

## Resource-sharing development branch (2026-11-07)

The development branch adds native AVFoundation camera/microphone sources and repeatable AppKit resource rows with independent destinations, source selection, microphone mute and grouped disconnect. Display and capture connections reuse one protected admission session per destination rather than consuming the same one-time code repeatedly. Camera sends H.264; microphone sends PCM16 48 kHz mono. Permissions are requested before capture and resources stop independently. Native compilation and actual application delivery remain separate gates; existing display test evidence above does not validate these new paths.

This remains the native AppKit frontend, not the completed shared Qt migration. macOS virtual camera receiving requires a signed Core Media I/O system extension; virtual microphone receiving requires an installed HAL plug-in. Neither is supplied by the current ad-hoc portable ZIP/DMG, and USB remains unavailable. Camera/microphone sending does not install these receiver components or detach physical devices. Intel and Apple Silicon packages remain required. See IMPLEMENTATION-CHECKLIST.md for current build evidence.

Current development limits: the native display helper retains four simultaneous Mac-host virtual displays. Control/admission connections have separate workers and do not consume display slots. The Mac client still has one display receiver window. Camera/microphone rows use independent native captures; shared-source fanout on macOS is not implemented, and hardware may reject simultaneous captures. These limits are not removed by the new resource UI.

Review safeguards: source menus retain stable device IDs even when device names match. Microphone mute is checked per outgoing frame and against capture timestamps across unmute, so delayed callbacks cannot reclassify muted-period samples as live audio. Host handshakes and control-only sessions share a bounded budget of 32 workers; active displays use their separate display slots. These protections have isolated regression checks; physical-device delivery remains a test gate.
