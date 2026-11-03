# Linux hosting, client and AppImage work

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

