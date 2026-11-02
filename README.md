# SideScreen

Project for using another device as a Windows display and input surface over a local IP network.

**Status:** native development apps run on the desktop and Surface Book 2. Encrypted **3000x2000 H.264 targeting 60 fps** is tested with GPU capture/scaling/color conversion, NVIDIA hardware encoding, and GPU decoding/presentation on both laptop GPUs. The H264 path performs no application raw-pixel readbacks or CPU pixel uploads; compressed access units still use CPU/TLS buffers. Opt-in native mouse/keyboard/touch/pen forwarding is implemented and passes generated end-to-end tests; the user confirmed the repaired physical-pen session worked; individual pen features, physical touch and drawing-app compatibility still need acceptance checks. Audio and Windows+K are not implemented. Extended display is installed for local testing; see its validation status below. Stack: C++20, C++/WinRT/WinUI 3 controls, and D3D11/DXGI. Internal driver and settings identifiers retain `DM` for compatibility.

See [build instructions and the laptop test](docs/DEVELOPMENT.md) for the current runnable slice.

**Run it (0.11):** open **SideScreen Server** on the desktop and click **Start Driver** (waits for a client before creating a display). Open **SideScreen Client** on the laptop, enter a server IPv4 address from its address list, then click **Connect**. Both apps list their own network addresses automatically; the server needs no client address. Closing a setup window hides it to the tray. Use the tray menu to reopen or Exit. Client disconnects leave the monitor and its layout intact; **Stop Driver** removes it. The client supplies its screen resolution and refresh target; saved Windows layout changes are retained. The stream currently caps at 60 fps and chooses a supported driver mode if an exact match is unavailable.

The display opens as a resizable window. Its small toolbar provides minimize, maximize/restore, fullscreen and close; it auto-hides and reappears after hovering at the top center or swiping down from the top center. **F10** reveals controls, **F11** toggles fullscreen, **F8** pauses input and **Ctrl+Shift+Q** disconnects; **F9** also closes. Normal sessions have no color test or time limit.

**Portable apps:** build with `scripts/build-apps.ps1`, then `python scripts/package_apps.py`. The two ZIPs in `out/dist` include the WinUI runtime. Extract the entire Client/Server folder and run `Client.exe` / `Server.exe`. The server still requires the separately installed DM display driver; these development bundles are not a production signed-driver installer. See [setup, packaging and controls](docs/DESKTOP-APPS.md).

**Live Display Settings (0.9):** rearrange the DM monitor or change resolution/orientation while connected. Video briefly pauses and resumes with the chosen configuration; the host remembers it for the next session using the same startup profile. See [display changes](docs/DESKTOP-APPS.md#display-settings-changes-09).

Current apps: `Client.exe` and `Server.exe` provide WinUI 3 launch windows and saved pairing. The earlier `dm-host` / `dm-receiver` Win32 launchers remain developer diagnostics. Diagnostic programs: `dm-net` for reachability, `dm-session` for credential pairing and TLS checks, and `dm-view` for the media/input engine and native preview. See [GPU pipeline results](docs/GPU.md), [security](docs/SECURITY.md), [H.264 integration](docs/H264.md), and the [earlier JPEG baseline](docs/VIDEO.md).

**Native mode in validation:** 0.7 adds explicit `--width 3000 --height 2000 --fps 60`, fullscreen reception and a 3000x2000/60 Hz driver mode. Requested refresh and measured delivered fps are distinct; see [native-resolution results](docs/NATIVE-DISPLAY.md).

**Extended desktop:** the local test-signed IddCx driver is installed. `host --extend` adds an independent 1280x720 monitor while preserving both physical displays. Virtual capture, laptop streaming at 720p30, and repeated host/receiver-loss removal tests pass. See [status, setup, and rollback](docs/EXTENDED-DISPLAY.md).

## Intended experience

1. Open the host on the Windows computer and the receiver on the display device.
2. Enter the server IP address, including over a direct Ethernet cable with static addresses.
3. Pair explicitly, choose a display, and grant input/audio/microphone permissions.
4. Mirror a display or, with the virtual display component installed, extend the Windows desktop.
5. Control the host with touch, dragging, mouse, keyboard, and the pen features available end to end.
6. Independently mute received audio and microphone forwarding, or disconnect at either device.

Windows+K compatibility is a separate feasibility track using Miracast. It is not provided by custom LAN discovery and must not be advertised before hardware testing.

## Planning documents

- [Product requirements](docs/PRODUCT.md)
- [Architecture and selected stack](docs/ARCHITECTURE.md)
- [Stack decision and engineering rules](docs/STACK.md)
- [Direct IP and network interfaces](docs/NETWORKING.md)
- [Capabilities and input contract](docs/CAPABILITIES.md)
- [Milestones and acceptance checks](docs/ROADMAP.md)
- [Evidence and pending decisions](docs/DECISIONS.md)

Start with the development guide for current behavior and the roadmap for remaining work. Product and capability documents describe intended behavior unless explicitly marked implemented.
