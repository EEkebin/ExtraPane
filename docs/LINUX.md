# Linux client and AppImage work

Status: initial source implementation, **not built or tested**. The user authorized development but explicitly prohibited testing while gaming. No Linux application, build, GPU probe, CI run, connection test, driver operation, or display change has been run for this work. There is no downloadable Linux build yet. The current published release remains Windows-only.

The first target is Linux x86_64, with KDE Plasma on Wayland as the first intended acceptance environment. Windows hosts the extended desktop; Linux receives its video and returns input. Linux hosting is a later milestone. macOS is deferred.

## Source implementation

- A Qt 6 / Qt Quick app with Server, Client, Settings and About pages. Server is disabled with an explanation. Closing the app exits; this receiver does not create a tray service.
- Direct numeric IPv4 over TCP port 43824, with optional source-address binding and `IP_UNICAST_IF` selection of a connected adapter. Static-IP Ethernet is supported by the connection design; DHCP, DNS and discovery are not used. The address list filters for active Ethernet/Wi-Fi interfaces and excludes loopback and interfaces classified as virtual by Qt. Routing behavior with competing VPN/Wi-Fi routes still needs validation.
- OpenSSL TLS 1.3, interoperating with the Windows automatic-LAN mode. This mode encrypts the connection but does not authenticate peer identity. No saved pairing, shared secret or diagnostic PSK mode is introduced.
- The existing `core/protocol` serialization and bounds checks, unchanged. Requests, offers, frame headers, input epochs, input packets and ready/completion acknowledgements use the same protocol 9 format as Windows.
- The client sends its current screen's pixel dimensions and refresh preference (capped at the existing 60 FPS protocol limit). Windows still chooses from its driver-supported display modes; the client cannot invent new driver modes.
- A worker thread handles connection, TLS and compressed-frame transfer. Network waits check cancellation every 50 ms. GStreamer presents OpenGL textures in Qt using `qml6glsink`; raw frames are not sent through Qt UI messages.
- GStreamer H.264 decoding through `h264parse` and `decodebin`. The installed decoder is reported in optional statistics. Software fallback is possible; hardware decoding, GPU interoperability, copy counts and latency are **not verified**. This is not a zero-copy or performance claim.
- Bounded compressed-data queues and bounded native mouse/keyboard event queues. Consecutive pointer moves can coalesce; key/button transitions are not silently dropped. Queue overflow or a stalled decoder ends the connection so Windows can release session-owned input.
- One input packet per compressed video frame, including empty packets, sent before decoding to match the Windows host's ordering. Focus loss and lost pointer grabs clear locally owned input and enqueue a reset. Disconnect closes the session; Windows owns remote watchdog cleanup.
- Mouse move, buttons, wheel and drag source paths. Keyboard mapping translates common Qt Wayland/XKB physical codes to Windows set-1 scan codes. The host's layout applies. IME text, Pause, Print Screen and multimedia keys are not implemented. Native touch/pen capabilities are not advertised, and synthesized pointer input is suppressed.
- Display renegotiation preserves the TCP/TLS session, clears old input, rebuilds the video pipeline, acknowledges the new offer and continues with the replacement input epoch. Live rotation/resolution/layout behavior still needs hardware validation.
- Resizable/windowed/fullscreen presentation, a top-center toolbar, F10 to reveal controls, F11 for fullscreen, and Ctrl+Shift+Q to disconnect. Statistics are off by default and only shown through the toolbar Settings menu. They measure compressed-frame reception, not decoded/presented FPS or end-to-end latency.
- Native resolution by default. Optional 75%/50% stream-size choices apply once on the next connection and are not saved. There is no adaptive quality controller.

## Build boundary

The root CMake configuration now branches to `platform/linux` before including any Windows libraries, compiler flags, RC resources or native dependencies. The Windows source targets retain their existing build path. No Windows renderer, capture path, driver or wire format was changed.

Proposed build baseline: Ubuntu 24.04 x86_64, GCC or Clang with C++20, CMake 3.28+, Ninja, Python 3.11+, Qt 6.4+ (Core, Gui, Network, Qml, Quick and QuickControls2), OpenSSL 3, GStreamer 1.24+ development libraries, and the Qt 6 GStreamer plugin. This baseline and its actual package versions must be validated before publication. Ubuntu 24.04 provides `gstreamer1.0-qt6`; a working GPU/Wayland runtime must still come from the destination PC.

When builds are authorized, the source build entry point is:

```sh
bash scripts/build-linux.sh
```

It configures `linux-release` with `BUILD_TESTING=OFF` and builds the `SideScreen` executable in `out/build/linux-release/platform/linux`. It does not launch the app or run tests. No Linux test preset or automatic release workflow has been enabled.

## AppImage packaging draft

`scripts/package_linux.py` packages an already-built executable. It does not build, test, launch or publish SideScreen. The planned output name is `SideScreen-<version>-linux-x86_64.AppImage`, with a separate SHA-256 file.

The packager requires locally supplied linuxdeploy, its Qt plugin, and its GStreamer plugin. Copy `packaging/linux/tools.example.json` to an ignored local file, replace its paths and placeholder hashes with reviewed exact tool identities, and keep that lock with the packaging inputs. Placeholders deliberately fail. No packaging tools or dependencies are downloaded by the script, and no dependency has been installed for this work.

The GStreamer plugin input is restricted to the application, parser, playback, OpenGL/Qt integration and selected decoder plugins. The scanner is included; system GPU drivers are not bundled. Qt's Wayland platform plugins are requested explicitly. The package also requires reviewed license notices and any applicable source-offer materials for its actual dependency set; a nonempty directory is only an input requirement, not an automated compliance audit.

Example invocation, **not executed**:

```sh
python3 scripts/package_linux.py \
  --tools-lock out/linux-tools.json \
  --licenses-dir out/linux-dependency-notices \
  --qmake /usr/bin/qmake6 \
  --plugins-dir /usr/lib/x86_64-linux-gnu/gstreamer-1.0 \
  --helpers-dir /usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0
```

AppImage packaging does not make every distribution, compositor or GPU compatible. The generated bundle's minimum libc, plugin loading, Qt/OpenSSL deployment, FUSE/extraction behavior and driver interoperability need validation. The tool locks, distro package versions and dependency notices must be finalized before adding Linux artifacts to releases. Do not advertise or publish this draft as a working Linux download.

## Next acceptance gates — require permission before running

1. Build and inspect the Linux target on the selected Linux machine/container. Resolve compiler, QML resource/import and GStreamer context issues before connecting to a host.
2. Verify the AppImage launches outside its build environment on the intended KDE Plasma/Wayland machine, including a clear missing-dependency path.
3. Connect to the Windows release over Wi-Fi and direct/static-IP Ethernet. Confirm real extended display behavior, native dimensions, mouse/keyboard delivery, reconnect, session cleanup, and server survival after disconnect.
4. Validate resize/fullscreen, fractional scaling, monitor rearrangement/rotation, decoder failure, bounded queues, held input on focus loss/disconnect, and concurrent Windows clients.
5. Measure actual decoder choice, GPU memory transfer and responsiveness before making performance claims. Then implement native Linux receiver touch/pen and prove delivery to Windows applications.
6. Start Linux hosting: inspect the KDE portal's advertised virtual-monitor, PipeWire capture and remote-input capabilities; implement a session-owned virtual display and H.264 encoder feeding the existing Windows receiver protocol. Do not substitute mirroring for a true extended monitor or assume every Wayland desktop supports the same APIs.

## Primary integration references

- [GStreamer qml6glsink and Qt GL context sharing](https://gstreamer.freedesktop.org/documentation/qml6/qml6glsink.html)
- [Qt Quick native item input](https://doc.qt.io/qt-6/qquickitem.html)
- [linuxdeploy Qt deployment](https://github.com/linuxdeploy/linuxdeploy-plugin-qt)
- [linuxdeploy GStreamer deployment](https://github.com/linuxdeploy/linuxdeploy-plugin-gstreamer)
- [AppImage runtime dependency boundary](https://docs.appimage.org/introduction/concepts.html)
- [ScreenCast portal and advertised virtual-monitor support](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [RemoteDesktop portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html)
