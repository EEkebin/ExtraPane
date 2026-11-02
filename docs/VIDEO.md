# First encrypted video baseline

Historical JPEG baseline. The current default codec is hardware H264; see H264.md for current commands and evidence. Use `--codec jpeg --frames 60 --fps 10` to repeat this baseline with current executables. Video protocol v3 requires rebuilding both endpoints.

On 2026-11-01, the desktop streamed its primary monitor to the Windows laptop. The laptop decoded and presented all 60 frames at 1280x720 in approximately six seconds using the native D3D11 preview. The user confirmed the desktop image appeared.

This is a **view-only feasibility prototype**. It uses a temporary CPU JPEG path at a requested 10 fps, not the planned hardware H.264 path or a 1080p60 product implementation.

## Why D3D11

D3D11/DXGI was selected in the stack plan and retained after comparing Vulkan and D3D12. Windows Desktop Duplication yields D3D11-compatible desktop textures, and Media Foundation supports D3D11 device-manager integration. D3D12 offers explicit resource/queue/synchronization control; that additional control does not itself reduce capture/codec/network latency. Vulkan can serve future receivers on other platforms without replacing Windows-specific capture and media APIs.

The receiver renderer is separate from TLS and the wire protocol. Platform receivers can implement a different renderer later. A D3D12 or Vulkan backend should be driven by a measured integration need, not a version-number comparison.

Primary references: [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api), [D3D11 media decoding](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation), [D3D12 synchronization responsibilities](https://learn.microsoft.com/en-us/windows/win32/direct3d12/important-changes-from-directx-11-to-directx-12).

## Implemented path

1. Require a provisioned pairing and explicit host `--allow-screen` consent.
2. Accept the connection on the chosen IPv4 address/interface and authenticate TLS 1.3 PSK-DHE.
3. Accept only the defined video-only request before opening desktop capture.
4. Capture an active output on the default hardware adapter, preferring its primary monitor. Rotation is currently rejected.
5. Read back D3D11 pixels, scale within 1280x720, and encode JPEG through WIC.
6. Send bounded versioned metadata and sequenced length-limited frames inside TLS.
7. Validate dimensions and lengths before allocation/decode. Decode WIC BGRA frames on the receiver worker.
8. Maintain one latest-frame UI slot and upload/present through D3D11 in a native diagnostic window. No frame queue can grow without limit.
9. Receiver acknowledges completed decoding; both processes finish and close the encrypted stream.

Video offer fields and frame headers use explicit big-endian serialization, not native C++ layouts. JPEG payloads are limited to 2 MiB, dimensions to 1280x720, and each run to 600 frames. The receiver rejects unsupported grants/formats, wrong sequence, backwards sample times, and mismatched JPEG dimensions. Per-I/O deadlines prevent indefinite stalls. The preview is a diagnostic Win32 window; the selected production WinUI shell has not been built.

## Running

Use `dm-session` to provision the same random pair key through a trusted channel first; see SECURITY.md. From folders containing the Release executables:

```powershell
# Host: explicit consent, six-second nominal test.
.\dm-view.exe host --name PAIR_NAME --bind HOST_IP --allow-screen --codec jpeg --frames 60 --fps 10 --timeout-ms 60000

# Receiver: native preview.
.\dm-view.exe receive --name PAIR_NAME --bind RECEIVER_IP --peer HOST_IP
```

Default port is TCP 43824. The receiver's `--headless` option validates decoding without opening a preview. `dm-view info` prints the default GPU and registered hardware H.264 encoders; registration is not a successful encoder test.

The helper `scripts/test_video_laptop.py` automates transfer/hash validation, disposable pairing in the user's interactive session, tightly scoped temporary firewall rules, the video run, and cleanup. It prompts for SSH credentials and records non-secret results in ignored `out/laptop-video-test.json`.

## Measured test evidence

| Observation | Result |
| --- | --- |
| Desktop GPU | NVIDIA GeForce RTX 4090 |
| Desktop H.264 registrations | NVIDIA H.264 Encoder MFT |
| Laptop GPU | Intel UHD Graphics 620 |
| Laptop registrations | Intel Quick Sync and NVIDIA entries reported; actual usable encoders still need testing |
| Requested run | 60 frames, 1280x720, 10 fps |
| TLS cipher | TLS 1.3 ChaCha20-Poly1305 / SHA256 with ephemeral PSK exchange |
| Sent JPEG bytes | 3,927,749 |
| Host elapsed time | 6.007 seconds |
| Decoded frames | 60 |
| Presented frames | 60 |
| User-visible check | User confirmed the desktop image appeared |

These counts and elapsed times are a single feasibility run, not glass-to-glass latency, quality, or throughput benchmarks. No input, microphone, or audio was captured. Pairing records and test-port firewall rules were removed afterward; test executables remain in the user's local app-data test directory.

## Remaining work

Hardware H264 encode/decode has subsequently passed; see H264.md. GPU-resident capture/conversion/presentation and production transport still need work. JPEG readback/scaling/encoding/decoding incurs CPU work and copies. TCP can accumulate latency under loss; this prototype does not decide the final media transport.

Monitor selection across adapters, rotation, DPI-aware aspect-correct resizing, cursor composition, color/HDR, lock/resume, GPU loss, prompt active-session cancellation/revocation, adaptive bitrate, and reconnect remain incomplete. Closing the preview closes the window immediately but the worker may take up to its bounded network timeout to exit. Hardware input, audio, virtual monitors, microphone endpoints, and Miracast remain separate milestones.
