# OpenGL client presentation

Select **Settings > Client > Renderer > OpenGL** before connecting. The choice is saved locally and locked during a client session. D3D11 remains the default and fallback. Unsupported OpenGL stays visible and disabled with a yellow explanation. A connection-time failure reports its reason and uses D3D11.

## GPU path and requirements

H.264 decoding and NV12-to-BGRA conversion remain on D3D11. OpenGL registers the converted BGRA texture through `WGL_NV_DX_interop2`, reads it as a framebuffer, scales it with `glBlitFramebuffer`, and presents with the Windows OpenGL driver. This is native OpenGL presentation, not an OpenGL decoder. Production frames have no application CPU pixel readback or upload.

The driver must provide hardware OpenGL 3.0+, framebuffer blitting, `WGL_NV_DX_interop2`, and `WGL_EXT_swap_control`. The capability probe creates a hidden window/context, opens the actual D3D11 device, registers and locks a texture, checks framebuffer completeness, and tries Windows presentation. Merely finding OpenGL or its extension functions is insufficient. Actual stream resources and the receiving window are checked again on connection. The OS supplies `opengl32.dll`; no extra runtime or SDK is bundled.

On hybrid-GPU systems, Windows may select an OpenGL GPU that cannot share textures with a separately selected D3D11 decoder adapter. That combination is rejected rather than introducing a CPU copy path. Support depends on the installed driver and GPU assignment, not simply the GPU model or brand. There is no automatic change to Windows graphics preferences.

## Ownership and geometry

- The D3D11 conversion runs while the registered resource is unlocked. Its commands are submitted before OpenGL locks the resource.
- WGL interop lock/unlock synchronizes access between the APIs; Direct3D never accesses the resource while OpenGL owns it. Exceptions release a held lock before cleanup.
- Resizing the source unregisters and replaces the texture. Window size changes update the blit rectangle without creating a new display or reconnecting.
- The draw clears to black, preserves aspect ratio, and flips destination Y to reconcile Direct3D's top origin with OpenGL's bottom origin.
- The preview retains its OpenGL context and window DC on the render thread. Rebinding every frame caused a severe Intel driver stall; retaining it removed that stall. Initialization and diagnostics use scoped contexts; renderer cleanup restores the previous context and uses the retained hidden window even after the visible preview has closed.
- Presentation uses swap interval 1. Driver calls can block internally; this is not a measured latency improvement over D3D11.

## Validation

Pixel checks passed on desktop RTX 4090 and Surface Intel UHD 620: 24 quadrant checks per adapter covering color, orientation, padded NV12 crop, letterboxing and changing source sizes. The Surface GTX 1050 decoder could not produce a complete shared framebuffer with the OpenGL context selected by Windows in this configuration; the diagnostic correctly rejected that combination. This does not establish that all GTX 1050 configurations lack OpenGL interop.

Desktop checks passed for saved selection across restart, locking settings during reception, returning to D3D11 on the next connection, normal close, and a synthetic unavailable-driver report with disabled options and explicit reasons. Window controls, statistics, fullscreen/restore, toolbar reveal, Ctrl+Shift+Q, live resolution/rotation/placement changes and saved portrait reconnect also passed with OpenGL active. The synthetic fixture verifies the UI, not a broken graphics driver.

The final Surface Intel fullscreen run at 3000x2000 decoded 600 frames and presented 527 over 10.10 seconds, with zero application raw-pixel readbacks/uploads. Received rate was approximately 60 FPS after startup, but some frames were skipped in presentation. These short, mostly static desktop runs are functional evidence, not a latency benchmark or a guarantee of 60 presented FPS. Profiling isolated and fixed an initial per-frame WGL context-rebinding stall before this handoff.

The final Surface GTX-decoder run reported the specific Intel OpenGL framebuffer incompatibility and continued in D3D11, decoding 600 frames and presenting 591 over 10.22 seconds. No crash or CPU pixel bridge occurred. Installed applications and Windows GPU preferences were left unchanged.

Commands (interactive tests require an idle desktop and the existing virtual-display driver):

```powershell
out/build/windows-release/dm-view.exe renderer-info
out/build/windows-release/dm-view.exe opengl-check
python scripts/test_renderer_preview.py --renderer opengl
python scripts/test_display_reconfigure.py --renderer opengl
powershell -File scripts/test_renderer_settings.ps1 -RendererName OpenGL
powershell -File scripts/test_renderer_unavailable.ps1 -RendererName OpenGL
```

For opt-in stage timing, set `EXTRAPANE_GL_PROFILE=1` in the diagnostic process environment. It prints average conversion, interop lock/unlock, draw, context and swap costs at exit. Normal runs do not print these timings.

The pixel diagnostic deliberately uploads known input and reads back the result. Neither operation belongs to production streaming. Physical pen/touch, perceived responsiveness and visual acceptance remain the user's handoff.

References: [WGL D3D11 interoperability](https://registry.khronos.org/OpenGL/extensions/NV/WGL_NV_DX_interop2.txt), [resource registration and lock/unlock semantics](https://registry.khronos.org/OpenGL/extensions/NV/WGL_NV_DX_interop.txt).

A later user-requested test assigned the temporary receiver to the high-performance GPU. Windows then enumerated the GTX 1050 as adapter 0. With both decoding and OpenGL on NVIDIA, pixel checks passed and native fullscreen presented 566/600 frames over 10.51 seconds, with no CPU pixel bridge or fallback. The temporary GPU preference and executable were removed afterward. The earlier rejection was a cross-adapter configuration, not a general GTX 1050 incompatibility.
