# Building the native prototypes

The project currently builds three native prototypes: `dm-net` (IPv4 reachability), `dm-session` (paired TLS diagnostic), and `dm-view` (encrypted view-only hardware H.264 preview). See SECURITY.md and H264.md for current commands, evidence, and limits. VIDEO.md preserves the earlier JPEG baseline.

`dm-net` remains deliberately unauthenticated and sends only a random correlation token. Its success is not evidence of a trusted peer or working media session. Authentication and video are separate executables; audio, input injection, drivers, and the WinUI shell remain unimplemented.

## Build and test

From PowerShell in the repository:

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -Configuration Release
```

The script locates installed Visual Studio C++ tools, enters the x64 developer environment, configures the selected CMake preset, builds, and runs CTest. Requires the Visual Studio desktop C++ and CMake components, a Windows SDK, and Python 3.10+ for integration tests. Python is a test dependency only.

Executables appear in `out/build/windows-debug/` and `out/build/windows-release/`. Release uses the static MSVC runtime and static Mbed TLS libraries so the laptop does not need a separate VC runtime or TLS installation. Initial configuration downloads the hash-pinned Mbed TLS source from its official release; runtime connections need no external server or internet. Python 3.13+ with PSK support additionally exercises independent OpenSSL wrong-key and downgrade checks; Python 3.14.6 was used locally.

Verified local tools at first build: Visual Studio Community 2026 18.11.3, MSVC toolset directory 14.51.36231 (compiler 19.51.36260), Windows SDK 10.0.26100.0, CMake 4.3.1-msvc1, Ninja 1.13.2, and Python 3.14.6. Exact outputs are in the local CMake cache/build logs. The full media/UI/WDK dependency set remains unselected. Future dependency locking must not assume this network-only build proves those integrations.

## Run locally

```powershell
.\out\build\windows-release\dm-net.exe interfaces
```

In one terminal:

```powershell
.\out\build\windows-release\dm-net.exe listen --bind 127.0.0.1 --timeout-ms 60000
```

In a second terminal:

```powershell
.\out\build\windows-release\dm-net.exe probe --bind 127.0.0.1 --peer 127.0.0.1
```

Both should report `PROBE_OK`. `connect_and_probe_ms` includes connection establishment and the diagnostic round trip; it is not video latency. Listener exits after one request or its deadline.

## Laptop test

Copy the Release `dm-net.exe` to a folder on the laptop; no build tools are needed there. Use a trusted LAN or direct Ethernet link. Run commands from the executable's folder in PowerShell.

1. Run `.\dm-net.exe interfaces` on each machine. Choose an **up** IPv4 address on the intended adapter. Avoid loopback, VPN, and virtual-machine adapters for this test.
2. On the desktop run `.\dm-net.exe listen --bind DESKTOP_IP --timeout-ms 60000`.
3. Within 60 seconds, on the laptop run `.\dm-net.exe probe --bind LAPTOP_IP --peer DESKTOP_IP --timeout-ms 5000`.
4. Save both terminal outputs. Expected: `PROBE_OK`, the selected local and peer endpoints, and `diagnostic-only (not paired)`.
5. Swap listener/probe roles to check both directions. Run a fresh listener for each probe.

Replace the uppercase IP placeholders with actual addresses. Default TCP port is 43821; override `--port` identically at both ends if necessary. If Windows prompts about network access, allow only the intended trusted network. Do not disable the firewall; a blocked connection may need a narrowly scoped inbound application/TCP rule. The application never changes firewall rules or adapter addresses.

For a direct cable with static addresses, use an unused subnet such as desktop `192.168.50.1/24` and laptop `192.168.50.2/24`; no gateway, DNS, DHCP, or internet is needed. Only use this example after checking for conflicting subnets. Do not change the adapter carrying a remote management connection inadvertently. A regular reachable LAN also works for the first check.

## Validation and limits

Tests cover packet magic/version/kind/reserved fields and length bounds, token matching, TCP fragmentation, malformed/truncated packets, refused connections, idle/stalled peers, duplicate listeners, invalid options, nonlocal addresses, and wildcard/hostname rejection. Socket ownership is RAII; connect, send, and receive use a shared monotonic deadline per operation.

Outbound interface selection uses `IP_UNICAST_IF` as well as binding the source address. Listener binds a specific IPv4 address with exclusive address use. Loopback checks cannot prove physical routing, firewall behavior, cable-loss recovery, or isolation under Windows weak-host configurations. IPv6 and media UDP candidate binding are not implemented.

## First two-device result

On 2026-11-01, the Release executable was transferred over SSH to the user's Windows x64 Wi-Fi laptop and its SHA256 matched the desktop build. Laptop loopback passed. Direct-IP TCP probes passed in both directions between the desktop Ethernet subnet and laptop Wi-Fi subnet through the existing router. No address or router changes were needed.

Observed single connect-plus-probe samples were approximately 27.5 ms laptop-to-desktop and 18.7 ms desktop-to-laptop. These are individual diagnostic exchanges, not a benchmark or video/input latency measurements. Temporary application-, address-, peer-, and port-scoped firewall rules were removed from both machines afterward. The user's separate SSH access rule remains available for future development.

The SSH access issue was a Public Wi-Fi profile with an SSH allow rule limited to Private. A separate Public-profile SSH rule restricted to the desktop enabled the connection. Cross-subnet routing itself worked in the diagnostic tests.

The optional development helper `scripts/test_laptop.py` repeats transfer verification, laptop loopback, and bidirectional probes. It requires Paramiko on the desktop, an existing accepted host-key entry, administrator rights for temporary scoped firewall rules, and a runtime password prompt. It saves no credentials. Run with `--laptop-ip`, `--desktop-ip`, and `--user`; results go to ignored `out/laptop-network-test.json`. It leaves only the diagnostic executable in the laptop user's local app-data test directory.

Static-address direct-cable operation, competing-route isolation, cable-loss behavior, and input delivery still need their own checks. Subsequent TLS and video checks are recorded in SECURITY.md and VIDEO.md.

Current hardware checks (run from an unlocked interactive Windows session):

```powershell
.\out\build\windows-release\dm-view.exe codec-check
.\out\build\windows-release\dm-view.exe gpu-check
python .\scripts\test_video_recovery.py
```

`codec-check` retains the earlier CPU-input hardware-codec check. `gpu-check` tests GPU scaling from 1280x720 to 640x360, GPU color conversion, hardware encode/decode, and all four quadrants' colors/orientation/frame order across 16 changing frames. Its 16 deliberate readbacks exist only to assert pixel results. The recovery script uses the production GPU path on the active desktop, interrupts each side, and verifies a fresh session on the same port. These require hardware and are separate from the five default CTest suites. No media or key files are saved; non-secret reports go under ignored `out/`.

`dm-view info` lists physical GPUs and display ownership. `dm-view receive --decoder-adapter INDEX` selects the H264 device; the GPU preview now shares that decoder device. JPEG still uses its CPU/default-renderer path. The laptop runner accepts the same option and records `out/laptop-h264-gpu-adapter-INDEX-test.json`, checking zero raw readbacks/uploads. See GPU.md for results and limits.

Current priority: real extended desktop; see [driver build, package, and validation status](EXTENDED-DISPLAY.md). `dm-display list` shows monitor names; `python scripts/test_video_recovery.py --display '\\.\DISPLAY1'` exercises selected-monitor WGC capture. The laptop runner accepts `--display` too. 1080p60, persistent controls, input, audio/microphone, and Miracast remain subsequent work.

Installed extended-display test (administrator, unlocked desktop, separately approved local driver/certificate):

```powershell
python scripts/test_video_recovery.py --extend
python scripts/test_video_laptop.py --laptop-ip 10.0.20.119 --desktop-ip 10.0.0.210 --user kebinBook --frames 300 --fps 30 --decoder-adapter 0 --extend --show-pattern
```

The extended recovery test asserts one DM monitor, unchanged physical topology, and full removal on peer/process loss and normal completion. The laptop test creates a temporary visible counter window when `--show-pattern` is selected and verifies baseline restoration after streaming. Extended reports include frame count and fps in their filenames. SSH/task deadlines account for stream duration. Driver installation stays separate from builds; see EXTENDED-DISPLAY.md and the installation record for removal.

Input version 0.6: see [native input and hardware acceptance](INPUT.md). `scripts/try-display.ps1 -Control` enables the laptop input test; view only remains the default. Both current video endpoints require protocol 7. `dm-input-check` verifies native app delivery/cleanup; `test_input_transport.py` requires Python 3.13+ PSK support and an unlocked interactive host with the installed driver. `test_video_laptop.py --control --exercise-input --extend --show-pattern` exercises generated receiver-to-host input and checks app-level delivery counts; it does not replace physical pen/touch validation.

## Native Surface Book 2 profile

Use `scripts/try-display.ps1 -Control` for a readiness-gated two-minute 3000x2000/60 target fullscreen session. `-Seconds 300` extends it to five minutes. F8 pauses input; F9 closes locally. This requires the separately installed 0.7 display driver. See NATIVE-DISPLAY.md for measured delivery and EXTENDED-DISPLAY.md for the recorded driver/certificate update and rollback.

## Desktop apps (0.8)

The build produces `dm-host.exe` and `dm-receiver.exe`. Keep each beside `dm-view.exe`; the host also needs `dm-display.exe` for optional debug patterns. Run the host elevated, the receiver as a normal user. Neither app uses SSH. `docs/DESKTOP-APPS.md` documents normal continuous sessions, pairing and settings. `--smoke-test` runs internal window-state assertions and exits; `--start` invokes the same Start/Connect action after opening the app.

## Live display recovery checks (0.9)

With the existing driver installed and no other DM session running:

```powershell
python scripts/test_display_reconfigure.py
out/build/windows-release/dm-input-check.exe --portrait
py -3.14 scripts/test_input_transport.py
python scripts/test_laptop_reconfigure.py
```

The local test checks continuous sessions through left placement, resolution, all orientations, unchanged monitor identity, layout restore after restart and exact physical-topology cleanup. The native input test checks delivery and held-state release after a display move. The laptop test targets the authorized development machines, prompts for SSH, uses the existing dm-lan pairing, temporarily scopes firewall access, and verifies GPU video plus generated mouse/keyboard/touch/pen delivery after portrait recovery. SSH is test infrastructure only. Evidence is stored under ignored `out/`.

## WinUI and portable builds (0.10)

Run `./scripts/build-apps.ps1` then `python scripts/package_apps.py`. This uses Visual Studio 2026 v145, Windows SDK 10.0.26100.0, locked NuGet packages and the Release CMake engine. Output is under `out/winui/{Client,Server}` and `out/dist`. The ZIPs need no NuGet/SDK/Python/SSH on the destination. Driver setup is separate.

`Client.exe --smoke-test` / `Server.exe --smoke-test` check initial control states and render the actual WinUI content to ui-check.bmp before exiting. `--start` uses the same action as the Connect/Start button. Developer-only `--test-report --test-duration N` records bounded engine status and closes the app after N seconds (1?300); ordinary launches have neither diagnostics nor a time limit. Packaging excludes these generated files.

`python scripts/test_preview_window.py` verifies client-driven fallback and window controls during streaming. `python scripts/test_portable_laptop.py` tests the extracted ZIPs, WinUI rendering, native Surface Book negotiation and generated native input after fullscreen/window changes. It prompts for development SSH, retains pairing, restores saved layout and baseline topology. Run interactive display tests sequentially. The five CTest suites run in both Release and Debug.
