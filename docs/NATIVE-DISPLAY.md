# Native Surface Book 2 display (0.7)

The laptop reports a 3000x2000 internal panel on Intel UHD 620, with integer refresh 59 (nominal 60 Hz). The DM driver now exposes 3000x2000 at 60 Hz alongside its earlier modes. The host changes only the independently identified DM monitor, verifies the resulting mode, then starts capture. The previous physical-display layout is checked after each laptop test.

Run from an administrator PowerShell on the desktop:

```powershell
.\scripts\try-display.ps1 -Control
```

The launcher waits for Enter before setup, requests the SSH password without saving it, and opens a two-minute fullscreen session at native resolution with a 60 fps target. F8 pauses/resumes input; F9 closes the local preview. Early closure releases input and removes the virtual monitor, although this bounded test harness reports incomplete frame delivery as a failed test. Use `-Seconds 300` for five minutes. Audio is not implemented.

For direct CLI use, host options are `--extend --width 3000 --height 2000 --fps 60 --frames 7200 --allow-screen --allow-input` in addition to pairing and network options. Receiver options are `--control --fullscreen`. Existing selected physical displays are scaled within the requested width/height without changing their modes; extended DM modes must be supported by the installed driver. Default CLI geometry stays 1280x720.

## Pipeline changes

H264 accepts even dimensions up to 4096 per axis and 8,847,360 pixels, 1?60 fps, and 1?36,000 frames. JPEG remains a 720p diagnostic. H264 target bitrate scales with pixels and fps, bounded to 4?60 Mbps (45 Mbps at native60); actual compressed traffic depends on content. Per-frame encoded payload remains bounded to 2 MiB.

High-resolution per-thread waitable timers replace coarse codec/GPU polling and frame pacing. No system timer-resolution setting is changed. The receiver returns its authenticated input batch before decoding, allowing host encoding and receiver GPU work to overlap. TLS contexts remain single-thread owned and protocol v4 retains one reverse-input batch per video frame. Network round-trip and decode time can still limit fps. The preview keeps only the latest frame and no longer adds 10 ms after each presentation.

Fullscreen provides a 3000x2000 client area on the laptop panel, preserving native pixel geometry. The input map continues to use the actual client rectangle. The host diagnostic window maximizes to cover the larger virtual desktop and includes a moving bar for motion tests.

## Measurements

Initial native60 view-only run: 600 decoded in 12.483 s (~48 fps), 504 presented (~40 fps). After timing and input-order changes, a 20-second animated native60/input run decoded 1,200 frames in 20.268 s (~59.2 fps) and presented 1,188 (~58.6 fps). Native host delivery observed mouse=1, characters=1, touch=2, pen=1 and pressure_max=897. The user reported local mouse use during that run, so it is not an isolated input benchmark.

These tests use the desktop NVIDIA hardware encoder, Intel hardware decoding and fullscreen D3D11 presentation over the existing routed Ethernet/Wi-Fi LAN. Application raw-pixel readbacks and preview CPU pixel uploads remain zero. They are diagnostic workloads, not proof of sustained full-screen game/video performance, battery use or input-to-photon latency. Native60 is the requested mode, not a guarantee that every frame reaches the screen at precisely 60 fps.

Driver version, certificate identities and rollback records are documented in EXTENDED-DISPLAY.md. The driver update preserves the 0.5 package for rollback and does not alter boot security or reboot Windows.

The final one-minute run decoded 3,600/3,600 frames in 60.486 s (~59.52 fps) and presented 3,585 (~59.27 fps). The host counted 2,475 newly acquired capture surfaces; other transmitted frames reused the last surface when WGC had no newer frame. Thus the measured 59.5 rate describes encoded/delivered frames, not 59.5 distinct desktop updates per second. The GDI diagnostic animation and WGC scheduling are part of this workload. Input observed mouse=1, chars=1, touch=2, pen=1, pressure_max=897, with 77 forwarded events. Exact baseline topology returned and disposable pairing/firewall state was removed. Report: `out/laptop-h264-gpu-adapter-0-extend-3600frames-60fps-3000x2000-input-test.json`.

Validation completed: Release and Debug each pass five CTest suites; GPU color/geometry/order roundtrip passes; native-resolution receiver-loss, host-loss and restart restore topology; watchdog/parent-crash, pen/touch burst and native pointer checks pass; independent OpenSSL input permission/epoch/sequence/bounds checks pass. No DM session remains after tests.
