# Linux hosting, client and AppImage work

## Devices workspace on the development branch

The Devices workspace opens with Nearby devices and starts local discovery
automatically. Expand **Connect by address** for IPv4/localhost, port, pairing
code and interface selection. Direct connections remain available when multicast
is unavailable or when connecting through a manually forwarded address.

On Share, supported resources are available by default. Pairing alone does not
start capture. Red power marks actual use of the exact resource and identifies
its consumers; stopping one resource preserves the others. Disconnecting all
active shares leaves the listener/catalog available. Gateway sharing remains
opt-in. See [workspace testing](DEVICE-WORKSPACE-TESTING.md)
for the current flow; older server/client instructions below describe the legacy
mode. macOS/Linux validation of this latest increment remains pending.


## Resource-sharing development branch (2026-11-07)

The current development branch adds the shared grouped client UI, independent
display windows, camera/microphone sending and receiving, and tray lifetime while
hosting or connected. The listener handles independent resource sessions; actual
display creation still depends on the desktop portal. These changes supersede the
single-client and close-to-exit limits in the historical validation below.
See [Linux resource prerequisites and limits](LINUX-RESOURCES.md) and
[platform parity status](PLATFORM-PARITY.md). Camera/microphone sources can share
one capture pipeline across destinations; USB remains unimplemented. Native builds and packaging are checked in
GitHub; combined resources and virtual endpoints still require device validation.

## Current implementation and validation (2026-11-03)

The user authorized Linux hosting, native input and packaging work, and confirmed Linux-host to Windows-client **video and mouse/keyboard delivery**. Windows-host to Linux-client video, input, reconnect, fullscreen and repaired toolbar contrast were also confirmed.

Linux hosting now creates a real session-owned virtual output through KDE's Wayland desktop portal, captures through PipeWire and encodes with software OpenH264. Start server binds TCP 43824 to the selected IPv4 address; the output is created only after connection and KDE desktop-sharing consent. Disconnect returns the server to listening. Shutdown removed the session output while preserving the VM's original monitor.

Current host limits: one client at a time; portal-owned dimensions (1920x1080 on tested KDE); client refresh target capped at 60 FPS; software encoding. Native-resolution control, multiple Linux-host clients, hardware encoding and Linux tray hosting are not implemented. Closing the Linux app exits and stops its host. The observed VM session received roughly 54-60 FPS, including idle streaming; this is not a physical hardware performance claim.

Linux client native touch and pen capture are implemented. Synthetic Qt checks pass for distinct/stable contact IDs, contact cancellation, pressure, tilt, rotation, eraser/barrel flags, capability gating, wire bounds, focus cleanup and overflow reset. Physical touchscreen/pen delivery remains unverified. The Linux host grants portal mouse, keyboard and touch capabilities; the portal has no pen injection API, so pen is not granted. No mouse emulation is counted as native input evidence.

Hosting additionally requires Qt DBus and `gst-plugin-pipewire`; OpenH264 is supplied by the tested Arch `gst-plugins-bad`. The PipeWire source must expose `keepalive-time`. Tested runtime: KDE/KWin 6.8.2, PipeWire 1.6.10. Optional x264/libav are not bundled. The source executable and AppImage use the same host/client UI.

Build optional synthetic checks with `cmake --preset linux-release -DEXTRAPANE_LINUX_TESTS=ON`, build with two jobs, then run `QT_QPA_PLATFORM=offscreen out/build/linux-release/platform/linux/extrapane-linux-input-check` when testing is authorized.

AppImage `2026.11.03-pre.0.1` launches outside the build tree, loads its bundled Qt/GStreamer libraries, and passed Linux-host to Windows-client video and input with user confirmation. The generated launcher and missing Wayland integration were repaired. Disconnect returned the packaged server to listening. The SHA-256 check passes. This development artifact is in the VM's `/home/user/Applications/` directory. At the user's request, release workflows now build and include the AppImage and dependency notices for both channels, gated on a successful Linux job; hosted build, synthetic input checks, extracted-bundle startup/shutdown and artifact upload passed in run 41724190850. An Arch AppImage does not establish compatibility with older distributions.

For portable use, place the AppImage in a stable directory, mark it executable and run it. FUSE 2 is needed for normal mounting; `--appimage-extract-and-run` is the fallback. Delete the artifact to uninstall. Delete `~/.config/ExtraPane/ExtraPane.conf` separately to clear the saved server address. No Linux driver is installed.

## Setup and controls

Use matching protocol-9 builds. On the host, select a connected IPv4 interface and start the server. On the client, enter that address and connect. DHCP, DNS and discovery are not required. Linux hosting requires a Wayland portal advertising virtual outputs; other desktops are not assumed compatible. Automatic LAN connections use TLS 1.3 without peer identity verification. Use a trusted network. The app does not alter firewall or security settings.

F10 pauses forwarding and keeps the toolbar visible; F8 resumes/pauses input, F11 toggles fullscreen, and Ctrl+Shift+Q disconnects. Forwarding pauses while toolbar/menu controls are active. Statistics are off by default under toolbar Settings and report compressed-frame reception, not end-to-end latency. The optional 75%/50% stream-size choices apply to the next connection and are not saved.

Keyboard forwarding uses physical scan codes and the host layout. IME text, Pause, Print Screen and multimedia keys are not implemented. In a VM console running on the same Windows machine receiving injected input, mouse movement can feed back into the console; use F10 to pause. This setup does not measure physical-client latency reliably.

## Reproduce the Arch build

Verified dependencies: GCC 16.2.1, CMake 4.4.5, Ninja 1.13.2, Qt 6.12.0, OpenSSL 3.6.5 and GStreamer 1.28.8. Synchronize and upgrade Arch together to avoid partial upgrades:

```sh
sudo pacman -Syu --needed base-devel cmake ninja pkgconf git python \
  qt6-base qt6-declarative qt6-wayland qt6-tools openssl gstreamer \
  gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugin-qml6 gst-plugin-pipewire
EXTRAPANE_BUILD_JOBS=2 bash scripts/build-linux.sh
```

Output: `out/build/linux-release/platform/linux/ExtraPane`. The build script does not launch the app or run tests. The Qt 5 `gst-plugin-qmlgl` package is not a substitute for `gst-plugin-qml6`.

## Package a development AppImage

Install packaging prerequisites `patchelf`, `fuse2` and `squashfs-tools`. Supply the three locally downloaded tools and reviewed SHA-256 identities using `packaging/linux/tools.example.json` as the schema. Placeholder hashes are rejected. Supply license notices and any applicable source materials for the actual dependencies; a nonempty directory is not an automated compliance review.

```sh
python3 scripts/package_linux.py \
  --tools-lock out/linux-tools/tools.lock.json \
  --licenses-dir out/linux-notices \
  --qmake /usr/bin/qmake6 \
  --plugins-dir /usr/lib/gstreamer-1.0 \
  --helpers-dir /usr/lib/gstreamer-1.0
```

Set the same `EXTRAPANE_VERSION` for configure/build and packaging. Artifacts and SHA-256 files go to `out/dist-linux`. The packager refuses to overwrite an existing version and does not launch or publish it. The bundle includes selected Qt modules, GStreamer plugins and scanner, OpenH264 and supplied notices. System graphics drivers and the desktop's PipeWire runtime remain host dependencies. Package inputs are recorded inside the image.

CI uses the pinned Arch snapshot described in RELEASING.md, records package versions/notices/source references, and checks portable startup under Xvfb. That does not validate streaming or older distributions. Packaged receiving, physical touch/pen, real GPUs, fractional scaling, live layout changes and direct static-IP cabling remain separate validation gates. Audio and reverse microphone remain unimplemented across this Linux slice.

## Integration references

- [Qt 6 GStreamer presentation](https://gstreamer.freedesktop.org/documentation/qml6/qml6glsink.html)
- [ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [RemoteDesktop portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)
- [linuxdeploy Qt](https://github.com/linuxdeploy/linuxdeploy-plugin-qt)
- [AppImage runtime boundary](https://docs.appimage.org/introduction/concepts.html)


## Access-code connections

Code-enabled builds add a TCP port (default 43824) and optional six-digit access code on the Client page. Use matching releases. Enable Require access code before starting the server; Generate new code admits another connection without disconnecting existing sessions. See [access-code setup and security](ACCESS-CODES.md) for expiry, retry limits and reachable internet endpoints. No-code mode is still restricted to trusted-network use.

## Resource UI integration (development branch, 2026-11-07)

The development Linux app now uses the same grouped, repeatable connection model
as Windows. A row retains its effective destination while active; each display
has its own receiver window, input queue and decoder. Custom width/height and
1?360 target FPS are requested through the shared display protocol. Native mode
uses the selected local monitor (the receiver's default target remains capped at
60 FPS). These are requested rates, not measured delivery guarantees.

Camera and microphone rows use native GStreamer source workers. Enumeration runs
outside the UI thread and selectors report missing sources/plugins. A shared
native capture pipeline feeds bounded queues for each destination. Microphone
mute is per destination; camera mirror applies to the shared source. USB rows
remain unavailable rather than exporting storage without an ownership backend.

Protected destinations maintain one authenticated control connection and request
a separate kind-bound ticket for each resource. Canceled requests and stale
callbacks cannot be applied to a later connection generation. The pending ticket
queue is bounded to64; established resources are not limited by that queue.
Use matching server builds that support the control protocol.

The main window stays available through the tray while hosting or sending/receiving
resources. Double-click restores it. If a desktop has no system tray, closing the
last window while active restores the app instead of silently stopping its work.
Receiver windows have touch-accessible controls, F11 fullscreen, F10 controls,
F8 input pause, and Ctrl+Shift+Q to disconnect only that window.

Isolated Qt tests under `tests/linux_ui` cover receiver-window controls and the
admission worker's cancellation, queue bounds and malformed control replies using
a fake transport and the real credential parser. They pass on the Windows build
host with Qt6.8.3. Native Linux compilation, AppImage startup and actual media
interoperability remain separate gates; these checks do not open devices.

Native source and receiving endpoint prerequisites are detailed in [Linux resources](LINUX-RESOURCES.md).

The Windows microphone receiver now has a jitter-buffer implementation for all
source platforms. Linux microphone receiving still uses its bounded PipeWire
queue; clock-paced jitter buffering with safe mute invalidation remains a
separate implementation and live-validation gate. See the receiving microphone
section of [Linux resources](LINUX-RESOURCES.md) for the current limits.
