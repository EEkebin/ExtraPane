# Vulkan client presentation

Select **Settings > Client > Renderer > Vulkan** before connecting. The selection is saved locally and locked while connected. D3D11 remains the default and fallback. Vulkan is a Windows client renderer in this release; this does not add Linux support. OpenGL remains a later stage after user acceptance.

## GPU path

H.264 decoding and NV12-to-BGRA color conversion remain on D3D11. The owned decoder frame is converted into a shared BGRA GPU texture. Vulkan imports that texture on the same adapter, acquires its Windows keyed mutex, performs aspect-fit scaling with a native Vulkan image blit, then presents through a Vulkan Win32 swapchain. Black letterboxing preserves the existing input geometry. No application CPU pixel readback/upload is used in production.

The shared texture is in GENERAL layout outside Vulkan. Explicit external queue-family ownership barriers surround each Vulkan read, and the keyed mutex transfers ownership between APIs. There is one render submission in flight, a bounded completion wait and one present semaphore per swapchain image. Resize/fullscreen rebuild the swapchain after draining its work; changed stream dimensions rebuild the shared image. Minimized windows skip presentation. Server capture, encoding and protocol 9 are unchanged.

This path adds a GPU color-conversion surface and a Vulkan blit. It is not an all-Vulkan decoder and makes no automatic latency, power or performance improvement claim over D3D11/D3D12.

## Availability and packaging

The loader is loaded dynamically from System32. Missing Vulkan does not prevent the app, server or other renderers from starting. Install/update the GPU manufacturer's driver if the Vulkan runtime is missing; the Vulkan SDK is a development tool and is not required to run SideScreen.

The support probe requires Vulkan 1.1 and matches the decoder's adapter LUID. It checks Win32 surface/swapchain support, BGRA scaling, D3D11 texture import and actual keyed-mutex execution. Required device extensions are `VK_KHR_swapchain`, `VK_KHR_external_memory_win32` and `VK_KHR_win32_keyed_mutex`. It creates a hidden presentation surface to check actual formats and usage. A different, unsupported decoder GPU is never silently substituted.

An unavailable renderer stays visible and disabled with its reason in yellow. Each optional renderer has its own status, so a Vulkan failure does not hide D3D12 availability. Actual stream creation/presentation is checked again; failures report the renderer and fall back to D3D11. Underlying decoder/GPU failures can still end the session.

Builds consume hash-pinned Vulkan-Headers v1.3.290 only. No SDK, graphics driver or Vulkan loader is bundled. Packages include the upstream license notices; see [third-party dependencies](../third_party/README.md).

## Evidence

- GPU pixel checks passed on RTX 4090, Surface Intel UHD 620 and GTX 1050: color, orientation, padded NV12 crop, aspect-fit letterboxing, source-size changes and cross-API synchronization.
- Surface native 3000x2000, 360-frame runs decoded all frames and presented 318 Intel / 330 GTX with zero application raw-pixel readbacks/uploads. Startup accounts for most skipped frames; steady received rate was approximately 60 FPS. These mostly static desktop runs are functional evidence, not latency or motion-quality benchmarks.
- Vulkan window resize, fullscreen/restore, maximize/minimize, statistics, toolbar gestures and disconnect passed.
- An additional native fullscreen Intel run decoded 600 frames and presented 456 over 10.77 seconds, with zero application pixel readbacks/uploads. Received rate approached 60 FPS after startup, but presentation continued skipping frames; this backend is an optional compatibility choice, not a demonstrated performance improvement.
- Host resolution, rotation and placement changes preserved the connection; saved portrait geometry survived reconnect.
- Shared Settings passed persistence across restart, active-session locking and normal exit. A synthetic missing-runtime report verified the visible disabled option, specific yellow explanation and saved-preference fallback; it does not emulate an actual broken driver.

Manual checks (interactive desktop; session tests require the already installed driver):

```powershell
out/build/windows-release/dm-view.exe renderer-info
out/build/windows-release/dm-view.exe vulkan-check
python scripts/test_renderer_preview.py --renderer vulkan
python scripts/test_display_reconfigure.py --renderer vulkan
powershell -File scripts/test_renderer_settings.ps1 -RendererName Vulkan
powershell -File scripts/test_renderer_unavailable.ps1 -RendererName Vulkan
```

The GPU diagnostic deliberately uploads test pixels and reads its output back; this is separate from production streaming. Human pen/touch, responsiveness and visual acceptance are the next handoff.

References: [Khronos keyed-mutex synchronization](https://docs.vulkan.org/refpages/latest/refpages/source/VkWin32KeyedMutexAcquireReleaseInfoKHR.html), [external-image layouts](https://docs.vulkan.org/spec/latest/chapters/resources.html), [Windows memory import](https://docs.vulkan.org/spec/latest/chapters/memory.html).
