# Stack decision

Selected 2026-11-01 under the user's instruction to choose the best fit. Native networking, TLS pairing, and a GPU-resident raw-pixel H264 path are tested on both Surface Book 2 GPUs at 720p30. WinUI 3 setup windows, native input and an installed development virtual driver now run on both machines; native 3000x2000/60 is measured. Audio and production driver distribution remain pending. Compressed network buffers still use CPU memory.

## Selected stack

| Component | Choice | Purpose |
| --- | --- | --- |
| Native engine | C++20, MSVC | Session management, media pipeline, networking adapters, and input handling |
| Windows API access | Win32 and C++/WinRT | Capture, device/input APIs, audio, and conditional Miracast integration |
| Desktop controls | WinUI 3, C++/WinRT, XAML | Native Windows setup, pairing, settings, and session controls |
| Video surfaces | Direct3D 11 / DXGI | GPU frame handling and native swap-chain presentation |
| Video codecs | Native Media Foundation hardware H264 with D3D11 device managers | NVIDIA DXGI-surface encoding, Intel/GTX GPU decode, and GPU conversion/presentation tested at 720p30 |
| Network media | Native WebRTC candidate | Encrypted real-time media/data, conditional on GPU integration, offline operation, and interface binding |
| Prototype secure channel | Mbed TLS 4.1.1 / TF-PSA-Crypto 1.1.1 | TLS 1.3 PSK-DHE checks and initial view-only video over TCP |
| Audio | WASAPI capture/playback; Opus candidate | Host system audio and receiver mic stream |
| Native input | Windows pointer APIs and SendInput | Preserve native pen/touch semantics and keyboard/mouse state |
| Extended monitor | WDK IddCx / UMDF component | True extended desktop, separately installed and signed |
| Recording endpoint | Separate virtual audio integration | Make receiver mic selectable by host applications |
| Engine build | CMake with shared presets | Reproducible native engine and integration-test builds |
| Shell/driver build | SDK-supported MSBuild projects | XAML generation, Windows packaging, and WDK integration |

Windows 11 x64 is the first host and receiver target. ARM64 follows after native media dependencies and driver availability are verified. Android/iPad receivers are later platform work; their input capabilities are not assumed to match Windows.

## Why C++ here

The deciding factor is direct access to the Windows and native media ecosystem. C++/WinRT, Direct3D, Windows driver samples, and native WebRTC interfaces let us work close to the required APIs with fewer language bridges. This is an integration judgment, not a claim that C++ is inherently faster than Rust.

Rust remains a technically sound performance option, but adding a Rust core and C++ native bridges would increase build and boundary work without a demonstrated benefit for this Windows-first scope. Go is suitable for networking services but introduces a managed runtime and more native media bridging. Zig offers explicit memory management, but would require more project-specific Windows integration than the selected route. Avoid adding secondary implementation languages without a concrete need.

WinUI gives the first release native Windows controls and accessibility. The UI is not the media engine: it sends typed commands and receives bounded state updates. GPU frames, audio callbacks, and high-rate pen samples stay outside the XAML UI thread.

## Performance rules

- Keep frames GPU-resident across capture, conversion, encode, decode, and presentation where supported. Count and measure unavoidable copies; do not assume a library provides a zero-copy path.
- Bound queues and measure their age. Drop stale video frames rather than accumulating delay; preserve ordered input lifecycle transitions and actual stroke samples.
- Avoid steady-state heap allocation, blocking locks, disk I/O, and UI dispatch in audio callbacks or high-rate input processing.
- Separate network work, media work, input delivery, and UI scheduling. Record stage timings and p95/p99 latency; optimize from traces.
- Use release builds for performance measurements and record build flags. Use debug/safety tooling for correctness checks.

## C++ safety and maintainability

Use RAII for handles, COM objects, capture sessions, sockets, GPU resources, and input-device lifetime. Prefer values and explicit ownership; use unique ownership by default and shared ownership only where lifetime requires it. Raw pointers are non-owning or confined to SDK boundaries. Do not use manual new/delete in ordinary application code.

Represent received data as bounded byte spans and validate lengths before decoding. Keep wire representations independent of C++ object layouts, packing, and native pointer sizes. Enforce permissions at the engine boundary. Catch exceptions at ABI, callback, and thread entry points; never unwind across foreign interfaces. Use explicit errors for expected failures and make cancellation/cleanup deterministic.

Keep the portable session/protocol libraries independent of WinUI and driver headers. Drivers do not link the UI or entire media engine, and follow supported WDK language/runtime restrictions. Use compiler warnings, static analysis, and sanitizers where supported; exercise parser and input lifecycle failures with meaningful tests.

## Build and release boundary

At the feasibility stage, inventory available tools and pin a compatible compiler, Windows SDK, Windows App SDK, WDK, CMake, and dependency set. Do not force XAML or driver generation into CMake if the supported SDK path uses MSBuild. Document a single orchestration entry point after these projects exist. No build commands or dependency versions are claimed to work yet.

Prefer a packaged desktop shell for application identity and updates, with separately managed signed driver packages. Validate packaging privileges, full-desktop input behavior, and the Miracast app model before choosing the final installer. Driver installation remains optional for mirror-only use; extended display and system mic availability depend on their components.

The first engineering work after design is the direct-IP/media/input feasibility work in ROADMAP.md. A successful spike must demonstrate actual behavior, not merely compile the selected libraries.

## Primary references

- [Microsoft C++/WinRT](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/intro-to-using-cpp-with-winrt)
- [Microsoft WinUI 3](https://learn.microsoft.com/en-us/windows/apps/winui/)
- [Microsoft CMake integration](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio)
- [Microsoft indirect-display driver sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/indirect-display-driver-sample/)
- [Native WebRTC APIs](https://webrtc.github.io/webrtc-org/native-code/native-apis/)
