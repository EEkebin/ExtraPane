# Extended desktop development history

For current installer behavior, use [Server driver installation](DRIVER-INSTALLATION.md). This document records earlier prototypes and local development installs; statements about bounded sessions and missing runtime features below describe those historical milestones.

The goal is an independent Windows monitor on the source computer. This host has two active physical monitors; a DM monitor would provide a third desktop surface, streamed to the Surface Book 2. Windows assigns its display number.

## Implemented and installed for local development

- `drivers/idd`: one-monitor UMDF/IddCx driver derived from the pinned Microsoft sample. Synthetic EDID identifies **DM LAN Display** (`DMM0001`), with stable serial/container identity. Preferred mode: 1280x720 at 30 Hz. 720p60, 1080p60 and 3000x2000/60 are also enumerated driver modes. The host explicitly selects the requested DM mode; physical display modes are not changed. Monitor and target mode lists agree; short buffers are checked.
- `dm-display list`: read-only topology, GDI names, friendly names, bounds, primary status, and DM identity. Cloned sources are not accepted as independent extension.
- `dm-display attach <1..600 seconds>`: explicit device-lifetime test; requires a staged signed driver and administrator. Closing its handle/process initiates device removal, which completes asynchronously.
- `dm-view host ... --allow-screen --extend`: authenticates, creates the software device, waits for an independent active DM source, and captures it. Capture closes before the device handle on completion/errors. Driver arrival/removal and virtual capture pass installed-driver tests.
- `dm-view host ... --allow-screen --display '\\.\DISPLAY1'`: captures a selected existing monitor through Windows Graphics Capture. Missing sources fail without falling back. Default remains primary-monitor DXGI capture. Selection/extension require H264.

WGC uses the hardware D3D11 device, a free-threaded two-frame pool, and an owned GPU texture, with Windows' capture indicator retained. Resolution changes require a new session. No application raw-pixel readbacks occur. H264 now accepts up to 4096 pixels per axis, 8,847,360 total pixels and 60 fps. Default capture remains 1280x720 unless width/height are specified. JPEG remains limited to 720p. Sessions are bounded diagnostics.

No global topology reset is performed. On this desktop, Windows automatically extends the DM monitor to the right of the two physical monitors. If another host does not activate it independently, the prototype asks for **Extend** in Windows Display settings. General automatic activation/placement remains future work. This prototype requires an elevated host for Software Device APIs; the product needs a narrow privileged display broker.

## Build and reviewable package

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\build-driver.ps1
.\scripts\prepare-display-test-package.ps1
```

Matching Microsoft SDK/WDK NuGet packages **10.0.28000.2526** are restored under ignored `out/driver-tools`, with SHA256s pinned in `scripts/restore-driver-tools.py`. DLL/INF/CAT output: `out/driver/DMIndirectDisplay`. WDK package checks and catalog generation pass; that does not establish driver loading.

Preparation copies the package into `out/package/dm-display-0.7-test`, signs its catalog using an ephemeral 3072-bit RSA key, writes the public certificate and hash manifest, and discards the private key. No certificate is installed or trusted. Re-running refuses to overwrite an existing frozen package. The manifest records thumbprint, expiration, and hashes. Before trust, PowerShell labels the signature `UnknownError` with an untrusted-root message. SignTool recognizes the catalog member/signer and rejects its untrusted root as expected.

This is local development signing, not Microsoft release signing. Derived source carries the Microsoft Public License in `drivers/idd/LICENSE`; attribution is in `UPSTREAM.md`.

## Separate installation and rollback

Review the package certificate and hashes before changing Windows. Proposed test: trust this public certificate in machine Root and TrustedPublisher, stage the exact DM INF with PnPUtil, then test with existing boot/security settings. If loading fails, collect device/Code Integrity errors and stop. Do not automatically change Secure Boot, test-signing, Memory Integrity, or reboot.

Record the exact published `oemNN.inf`. Rollback: end DM processes, verify removal and the original monitors, then run `pnputil /delete-driver oemNN.inf /uninstall` for that recorded package only. Remove its exact certificate thumbprint from Root/TrustedPublisher only if this test added it. Preserve unrelated drivers/certificates. A requested reboot requires a separate decision.

Installed acceptance so far: Windows exposes a third independent monitor, physical bounds/primary remain identical, virtual-monitor frames encode/decode over TLS, and normal completion plus host/receiver/process loss restore the original topology. Reconnect retains the EDID/container/software-device identity; Windows GDI source numbers can change between arrivals. Human window visibility and full driver uninstall/reinstall remain separate checks.

## Evidence before installation

2026-11-01: primary LG ULTRAGEAR+ at `(0,0)-(2560,1440)`; MSI NXG251R at `(2560,104)-(4480,1184)`. No DM driver/monitor installed.

Selected MSI capture passed local H264/TLS receiver-loss, host-loss, and fresh-session recovery checks (30/30 decoded after restart). Laptop Intel UHD 620 decoded 300/300 and presented 293 at 720p30 over LAN. Both ends reported zero raw-pixel readbacks; preview reported zero CPU pixel uploads. These prove selected physical-monitor capture/transport, not virtual-monitor operation. Reports: ignored `out/video-recovery-selected-display-test.json` and `out/laptop-h264-gpu-adapter-0-selected-display-test.json`. Disposable credentials/firewall rules were removed.

References: [IddCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview), [pinned sample](https://github.com/microsoft/Windows-driver-samples/tree/f4c1ff842b6a0001152f44cb61788b216a1c7627/video/IndirectDisplay), [WGC targeting](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor), [software devices/admin requirement](https://learn.microsoft.com/en-us/windows/win32/api/swdevice/nf-swdevice-swdevicecreate), [removal](https://learn.microsoft.com/en-us/windows/win32/api/swdevice/nf-swdevice-swdeviceclose), [matching SDK/WDK](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk).

## Local installation result

The user explicitly approved installation on 2026-11-01. Installed driver: `oem37.inf`, provider `DM Project`, version `0.5.0.0`. Certificate thumbprint: `69510ACF562D595C0540DDBD031FA4CF0FA310AC`, trusted in machine Root and TrustedPublisher for this test. Catalog trust now reports Valid. No reboot, test-signing, Secure Boot, or Memory Integrity change was made. The software device `SWD\DM\DM-LAN-1` reports OK while active.

Windows placed the virtual surface at `(4480,0)-(5760,720)`, preserving LG and MSI geometry/primary. Four consecutive recovery groups after the capture exception-boundary fix passed normal completion, receiver loss, forced host termination, and a new 30-frame session on the same TCP port. Removal was polled until the exact original two-monitor list returned. Initial runs exposed a WinRT exception that escaped the CLI's standard-exception handler; capture now translates WinRT failures into reported errors. These bounded repetitions do not establish all startup races or sleep/GPU-reset recovery.

Install helper: `scripts/install-display-test.ps1 -ExpectedThumbprint <approved-thumbprint>`. It verifies package hashes and certificate identity, records existing trust, and stores the assigned OEM INF in ignored `out/display-install-record.json`. It refuses duplicate installs. Exact rollback helper: `scripts/remove-display-test.ps1 -ExpectedThumbprint <recorded-thumbprint>`; it checks driver models/provider and refuses active DM processes. The rollback helper has been reviewed and its identity checks read against the installed package, but full uninstall has not been executed. Keep the record for later removal.

`dm-display pattern <seconds>` creates a temporary colored counter window on the active virtual monitor, waits up to 35 seconds for it, and closes on monitor removal. `test_video_laptop.py --extend --show-pattern` uses it for a visible laptop check. Test helper SSH/task timeouts now scale to stream duration; the first one-minute preview exceeded the old 45-second SSH limit, and its cleanup returned the original topology.

The corrected one-minute laptop run passed: 600/600 decoded and 600/600 presented at 1280x720, 10 fps, using the actual session-owned virtual monitor and diagnostic counter window. Both ends reported zero application raw-pixel readbacks, preview zero CPU pixel uploads, and the exact original two-monitor topology returned afterward. Report: `out/laptop-h264-gpu-adapter-0-extend-600frames-10fps-test.json`. Human visual confirmation is still pending.

The extended laptop 720p30 check also passed: 300/300 decoded, 293 presented, with zero raw-pixel readbacks/uploads and exact baseline restoration. Report: `out/laptop-h264-gpu-adapter-0-extend-300frames-30fps-test.json`. The driver/certificate remain installed for subsequent sessions; no virtual monitor remains after the bounded tests. Persistent session controls, input/audio, and full uninstall/reinstall validation remain pending.

## Native-mode driver update

Version 0.7.0.0 is staged as `oem38.inf`; certificate `209A99ED26DDE6C402E44D931547F4BF67593F17` was added to machine Root and TrustedPublisher. Current rollback identity is in ignored `out/display-install-record.json`. The previous 0.5 driver (`oem37.inf`) and certificate were retained, with their record archived in `out/display-install-record-0.5.json` and the original package unchanged. No boot policy or reboot was needed. Removing 0.7 leaves the previous driver available; complete removal must explicitly process both records, not delete unrelated packages or certificates.

For explicit uninstall, `remove-display-test.ps1 -ExpectedThumbprint 209A99ED26DDE6C402E44D931547F4BF67593F17` targets only 0.7. `-Record Previous -ExpectedThumbprint 69510ACF562D595C0540DDBD031FA4CF0FA310AC` targets only the archived 0.5 installation. These removal paths have not been executed in this update.

Version 0.8 reads monitor bounds in actual pixels, places its extended monitor to the right of the physical layout, and restores physical origins if Windows repositions them while recalling an older virtual layout. This corrected an observed 60-pixel movement of the secondary display during an arrival test. No physical resolution/refresh change or global display reset is performed; native arrival/loss/restart checks pass after the correction.
