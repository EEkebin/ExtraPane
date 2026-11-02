# DM — interactive network display

Project for using another device as a Windows display and input surface over a local IP network.

**Status:** native feasibility prototypes run on the desktop and Surface Book 2. Encrypted **1280x720 H.264 at 30 fps** is tested with GPU capture/scaling/color conversion, NVIDIA hardware encoding, and GPU decoding/presentation on both laptop GPUs. The H264 path performs no application raw-pixel readbacks or CPU pixel uploads; compressed access units still use CPU/TLS buffers. Opt-in native mouse/keyboard/touch/pen forwarding is implemented and passes generated end-to-end tests; the user confirmed the repaired physical-pen session worked; individual pen features, physical touch and drawing-app compatibility still need acceptance checks. Audio and Windows+K are not implemented. Extended display is installed for local testing; see its validation status below. Stack: C++20, planned C++/WinRT/WinUI 3 controls, and D3D11/DXGI. `DM` is a working name.

See [build instructions and the laptop test](docs/DEVELOPMENT.md) for the current runnable slice.

**Run it:** open the **DM Host** desktop shortcut and click **Start display**, then open **DM Receiver** on the laptop and click **Connect**. Debug is off by default: no color test window and no session time limit. F8 pauses input; F9 closes the fullscreen display. See [app setup and controls](docs/DESKTOP-APPS.md). The PowerShell test launcher remains a separate diagnostic tool.

**Live Display Settings (0.9):** rearrange the DM monitor or change resolution/orientation while connected. Video briefly pauses and resumes with the chosen configuration; the host remembers it for the next session using the same startup profile. See [display changes](docs/DESKTOP-APPS.md#display-settings-changes-09).

Current apps: `dm-host` and `dm-receiver` provide native launch windows and saved pairing. Diagnostic programs: `dm-net` for reachability, `dm-session` for credential pairing and TLS checks, and `dm-view` for the media/input engine and native preview. See [GPU pipeline results](docs/GPU.md), [security](docs/SECURITY.md), [H.264 integration](docs/H264.md), and the [earlier JPEG baseline](docs/VIDEO.md).

**Native mode in validation:** 0.7 adds explicit `--width 3000 --height 2000 --fps 60`, fullscreen reception and a 3000x2000/60 Hz driver mode. Requested refresh and measured delivered fps are distinct; see [native-resolution results](docs/NATIVE-DISPLAY.md).

**Extended desktop:** the local test-signed IddCx driver is installed. `host --extend` adds an independent 1280x720 monitor while preserving both physical displays. Virtual capture, laptop streaming at 720p30, and repeated host/receiver-loss removal tests pass. See [status, setup, and rollback](docs/EXTENDED-DISPLAY.md).

## Intended experience

1. Open the host on the Windows computer and the receiver on the display device.
2. Discover a peer locally or enter its IP address and port, including over a direct Ethernet cable with static addresses.
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
