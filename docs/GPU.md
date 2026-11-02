# GPU-resident H264 raw pixels

Version 0.4 keeps H264 raw pixels on the GPU through capture, scaling, color conversion, hardware encoding, hardware decoding, and presentation. The desktop and Surface Book 2 passed 300-frame 720p30 tests on both Intel UHD 620 and GTX 1050. Compressed H264 access units still use CPU buffers for TLS/networking. GPU copies, allocations, synchronization, and possible hybrid-GPU display transfers remain; this is not a claim of literal zero-copy execution.

## Pipeline

1. Authenticate paired TLS, require explicit host screen consent, and negotiate video-only access as before.
2. Desktop Duplication supplies a BGRA texture. Copy it to an owned GPU texture before releasing the duplicated frame. No CPU map/readback is used.
3. The D3D11 video processor scales the source to the negotiated even dimensions and converts full-range RGB to limited-range BT.709 NV12.
4. Wrap that NV12 texture with `MFCreateDXGISurfaceBuffer` and submit it to the D3D11-aware hardware H264 encoder using its DXGI device manager. GPU submission precedes asynchronous encoder input; waiting for GPU completion before feeding this MFT caused a repeatable multi-second stall and was removed.
5. Send bounded compressed access units over the existing TLS channel. The wire protocol remains version 3 with a decoder-ready barrier.
6. The receiver's hardware decoder returns NV12 array slices. Copy each slice into an owned GPU NV12 texture and complete that GPU copy before releasing the pooled decoder sample. This decouples the latest-frame queue from decoder pool lifetimes.
7. The preview uses the decoder's device. A small HLSL shader samples NV12 luma/chroma planes, applies limited-range BT.709 conversion and active-image cropping, and draws to the BGRA swapchain. No raw CPU pixel upload occurs. Shader setup/draw is protected as a group on the shared immediate context.
8. GPU completion waits bound in-flight work and texture lifetime. The UI queue still stores only the latest frame; superseded frames are discarded without accumulating latency.

Reference APIs: [D3D11 video processing](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videocontext-videoprocessorblt), [Media Foundation D3D11 surfaces](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation), and [D3D11 binding restrictions](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_bind_flag). Decoder arrays are copied to regular owned textures before shader-resource views are used.

## Verification

Run from an unlocked interactive Windows desktop:

```powershell
.\scripts\build.ps1 -Configuration Release
.\out\build\windows-release\dm-view.exe gpu-check
python .\scripts\test_video_recovery.py
```

The GPU roundtrip uploads a known test image (not a screenshot), scales 1280x720 to 640x360, converts/encodes/decodes entirely on GPU, and reads back only to verify four quadrants' color, orientation, geometry, and changing frame order. Sixteen test-only pixel readbacks are expected. It passed in Debug and Release. Production H264 does not call this validation readback helper. The earlier `codec-check` remains a separate CPU-input hardware-codec diagnostic.

The four default CTest suites pass in both configurations. Process-loss tests also pass on the GPU path: kill receiver, verify host reports failure; kill host, verify receiver reports failure; start a fresh session on the same port and decode all 30 frames. These tests do not establish silent packet-loss or cable-loss behavior.

For a paired manual stream, commands remain those in H264.md. The laptop automation supports `--decoder-adapter 0` or `1` and asserts the GPU input path, all decoded GPU surfaces, zero raw-pixel readbacks, and zero CPU preview uploads. Temporary pairing records, firewall rules, tasks, and key-free helper scripts are removed after testing.

## Surface Book 2 results, 2026-11-01

| Observation | Intel UHD 620 | GTX 1050 |
| --- | ---: | ---: |
| Requested | 1280x720, 30 fps, 300 frames | Same |
| Decoded frames / GPU surfaces | 300 / 300 | 300 / 300 |
| Presented / GPU-converted frames | 295 / 295 | 296 / 296 |
| Application raw-pixel readbacks | 0 host, 0 receiver | 0 host, 0 receiver |
| CPU raw-pixel preview uploads | 0 | 0 |
| Host elapsed | 10.020 s | 10.023 s |
| Average host capture stage | 0.075 ms | 0.074 ms |
| Average conversion/encoding stage | 15.535 ms | 15.565 ms |
| Average receiver decode/copy/sync stage | 11.865 ms | 11.968 ms |
| Renderer device | Intel UHD 620 | GTX 1050 |

These are single runs with live desktop content, not controlled CPU-utilization, battery, p95, image-quality, or end-to-end latency benchmarks. Decode-side times include waits for GPU work; they are not CPU execution time. Fewer frames presented than decoded reflects the latest-frame queue, including preview/shader setup at startup. The user was not watching the latest GPU preview, so human visual verification remains pending; automatic pixel validation and native presentation checks passed.

For context, the earlier CPU-bridge Intel run reported about 12.6 ms host capture and 19.5 ms receiver decode-side work. Current stage timings indicate the removed readbacks matter, but do not establish a controlled speedup ratio. The Intel GPU still owns the laptop panel and remains the default; selecting GTX also selects its device for the H264 preview, and Windows may internally transfer presentation to the panel GPU.

Reports: ignored `out/laptop-h264-gpu-adapter-0-test.json`, `out/laptop-h264-gpu-adapter-1-test.json`, and `out/video-recovery-test.json`. No screenshot/media recordings or pairing keys are saved by these tests.

## Still pending

1080p60, bounded buffer pools instead of per-frame texture/view allocation, less blocking GPU synchronization, controlled end-to-end latency/power measurements, robust display/GPU-loss recovery, production transport, and the WinUI shell remain pending. Input/pen/audio, virtual display/microphone components, and Miracast are not implemented by this media milestone. JPEG is still an explicit CPU diagnostic path.
