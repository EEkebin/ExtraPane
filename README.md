# ExtraPane

![ExtraPane ? extend your desktop over your local network](assets/branding/banner.svg)

<img src="assets/branding/icon.png" alt="ExtraPane icon" width="96" align="right">

ExtraPane uses another Windows PC as an extended monitor over Wi-Fi or Ethernet. The receiving PC can send mouse, keyboard, touch and pen input back to the host.

[Download latest release](https://github.com/EEkebin/ExtraPane/releases/latest) | [Report an issue](https://github.com/EEkebin/ExtraPane/issues) | [Build status](https://github.com/EEkebin/ExtraPane/actions/workflows/prerelease.yml)

The same app contains Server, Client, Settings and About pages. A PC can perform either role. Up to four clients can connect to one server.

## Requirements

- Windows 11 x64 on each PC.
- A reachable IPv4 connection over Wi-Fi, Ethernet or a direct cable with static addresses. DHCP and internet access are not required for a connection.
- Compatible D3D11 graphics hardware and H.264 encoding/decoding support.
- The ExtraPane virtual-display driver on each PC that will act as a server. Driver setup and starting the server require administrator permission.
- A touchscreen or compatible pen on the client to use those input features.

The downloads include the app's WinUI runtime. Users do not need Python, SSH, Visual Studio or a Vulkan SDK.

## Install

ExtraPane was previously named SideScreen. Existing published binaries still carry that name until the next release. The repository and download links now use ExtraPane.

Each release provides an installer, a portable ZIP and `SHA256SUMS.txt`. Use matching releases on both PCs.

### Installer

Run `ExtraPane-<version>-Setup-win-x64.exe` and select how this PC will be used:

| Setup choice | What it installs |
| --- | --- |
| **Client only** | The per-user ExtraPane app. No driver or certificate changes. |
| **Server + Client** | The same app, plus the required machine-wide virtual-display driver. Driver installation asks for explicit certificate consent and administrator permission. |

Server + Client setup stops if the required driver installation fails or administrator permission is canceled. You can retry or return to Client only. Client only does not hide the Server page; to host later, rerun setup as Server + Client or run the bundled `ExtraPane.DriverSetup.exe`.

The app is registered in Windows **Settings > Apps > Installed apps**. Setup offers a desktop shortcut and a checked **Launch ExtraPane** option on its Finish page. Silent setup does not launch the app. Updating requests that running ExtraPane instances shut down and waits for their server helpers to stop before replacing files.

### Portable ZIP

Extract the entire ZIP and run `ExtraPane.exe`. Keep the accompanying files together.

A portable **server** must first run the included `ExtraPane.DriverSetup.exe` and complete the driver setup. A portable **client** does not need it. The driver is installed on Windows; it does not become portable just because the app runs from a ZIP.

### Driver signing and Windows policy

The current driver uses a development code-signing certificate. It is **not Microsoft release signed**. With your consent, driver setup adds the package's public certificate to the machine's Trusted Root and Trusted Publishers stores and installs the verified package in the Windows Driver Store. The signing private key is discarded during packaging and is never included in downloads.

Setup does not disable Secure Boot or Memory Integrity, enable test-signing mode, or restart Windows automatically. Windows policy may still reject the driver when it loads. Successful package installation does not prove that every Windows configuration can load it. If setup or hosting fails, see [driver installation and troubleshooting](docs/DRIVER-INSTALLATION.md).

Apps and installers are currently unsigned. The bundled driver is development-signed; compatibility on a new PC depends on Windows policy and the hardware in use.

## Connect

1. On the PC whose desktop you want to extend, open **Server** and select **Start Driver**. Approve the helper's administrator prompt.
2. On the second PC, open **Client**, enter one of the server's listed IPv4 addresses, and select **Connect**.
3. Move a window to the new display. Use Windows **Settings > System > Display** to arrange monitors or change resolution and orientation.

Start Driver begins listening. It creates an extended monitor only after a valid client connects. The client supplies its preferred resolution and refresh rate; the current driver offers 720p, 1080p and 3000x2000 modes, with streaming capped at a 60 FPS target. Actual delivered frame rate depends on hardware and network conditions.

Windows layout changes are retained. Disconnecting a client removes only that client's monitor; the server continues listening and other clients stay connected. Stop Driver or the server tray's Exit command ends its sessions and removes their monitors.

The address lists show connected physical network adapters, excluding loopback addresses. The server does not need the client's address. There is no automatic discovery or pairing step.

Connections use TLS encryption, but the automatic LAN mode does not authenticate the other PC's identity. While the server is running, reachable clients are accepted without approval and can send input if the server permits it. Use it on a trusted LAN.

## Controls

| Action | Control |
| --- | --- |
| Disconnect the client | Ctrl+Shift+Q or F9 |
| Toggle fullscreen | F11 or the toolbar's Fullscreen control |
| Show the toolbar | F10, hover briefly at the top center, or swipe down from the small top-center strip |
| Pause or resume remote input | F8 |
| Show connection statistics | Toolbar > Settings > Show statistics |
| Stop hosting | Server > Stop Driver, or tray > Exit |

The preview is resizable and supports maximize, minimize and fullscreen. Top-right taps go to the remote application. Pen events do not open the toolbar.

Closing the main app window keeps the app in the tray only while its server is running. Client-only and idle windows exit normally. Normal sessions have no test pattern or timeout; Debug is off by default and enables bounded diagnostic sessions.

## Settings

Select the client renderer in the main app's **Settings** page before connecting: **D3D11**, **D3D12**, **Vulkan**, or **OpenGL**. The choice is saved on that PC and locked during reception. D3D11 is the default and fallback. Unsupported options remain visible and disabled with a yellow explanation. A renderer failure reports its reason and falls back to D3D11.

All renderers use D3D11 H.264 decoding. Server capture also remains D3D11. Vulkan uses the installed graphics driver's runtime. OpenGL requires working D3D11 texture sharing through `WGL_NV_DX_interop2`; GPU assignment matters on hybrid laptops. Additional renderers are choices, not promises of improved performance. See the [D3D12](docs/D3D12.md), [Vulkan](docs/VULKAN.md) and [OpenGL](docs/OPENGL.md) notes.

Native stream resolution is the default. The preview toolbar's Settings menu offers **75%** and **50%** stream resolution for the next connection only. These reduce transmitted resolution without changing Windows monitor dimensions. The choice is not saved and resets to Native after use, failure or app exit. There is no automatic quality switching under load.

Client/server input permissions and debug options are in the shared Settings page. Stop the corresponding role to change its options. Statistics are off by default and report local receive/decode/presentation measurements, not end-to-end latency.

## Input and current limits

Mouse, keyboard, multi-touch and exposed pen pressure, tilt, buttons, inversion and eraser data are supported. Actual behavior depends on the input device, Windows APIs and the host application. Arbitrary vendor pen features and legacy WinTab support are not guaranteed.

Windows has one shared cursor and keyboard focus. Multiple clients extend the same desktop; they are not independent user sessions. Input cleanup releases session-owned keys, buttons and contacts on disconnect.

Not implemented: system audio, microphone forwarding, Windows+K/Miracast, IPv6 transport, automatic client reconnection, ARM64/Linux/macOS builds, and DLSS/upscaling/frame generation. Custom LAN connections do not appear in Windows Cast.

## Uninstall

- Uninstall **ExtraPane** to remove the per-user app. Its uninstaller offers **Remove saved settings on this PC**, unchecked by default. Settings are also used by portable copies.
- Uninstall **ExtraPane Virtual Display Driver** separately to remove driver packages and certificate entries that its installer added. Administrator permission is required. Stop sessions first; other users and portable servers on this PC also depend on this driver.
- Pre-existing driver packages and certificate trust are preserved. Development drivers installed with older manual scripts are not automatically adopted or removed.

The driver uninstaller keeps its files and ownership records if removal fails so you can retry. See [driver installation](docs/DRIVER-INSTALLATION.md) for logs, silent options and recovery.

## Build and verification

Linux x64 AppImage support is being developed, starting with a client for Windows hosts. The current Linux source is unbuilt and untested; published downloads remain Windows-only. See [Linux development status](docs/LINUX.md).

The implementation uses C++20, native Windows graphics/input APIs, and C++/WinRT with WinUI 3. Install Visual Studio C++ desktop tools, CMake, the Windows SDK, Python 3, and Inno Setup 6. Driver build scripts restore hash-pinned Microsoft SDK/WDK packages into the build directory.

```powershell
./scripts/build-apps.ps1
./scripts/build-driver.ps1
./scripts/package-driver.ps1
./scripts/build-driver-installer.ps1
python scripts/package_apps.py
./scripts/package-installers.ps1
```

Outputs go to `out/dist`. Driver packaging creates a new development signing identity and refuses to overwrite an existing frozen package. Build scripts do not install the driver or trust its certificate.

CI builds the app and driver, checks packages and installer behavior, and publishes a prerelease after a PR merges into `main`. Driver-store/trust integration tests run only on disposable CI machines. GPU rendering, runtime driver loading, physical input and responsiveness require real hardware checks. See [development](docs/DEVELOPMENT.md), [release maintenance](docs/RELEASING.md), and [driver installation](docs/DRIVER-INSTALLATION.md).

Tags use UTC calendar dates, for example `v2026.11.03-pre.31.1`. Stable releases use date tags and a descriptive release title. Prereleases remain available until explicitly approved for removal.

## Documentation

- [Driver installation and troubleshooting](docs/DRIVER-INSTALLATION.md)
- [Extended display implementation and development history](docs/EXTENDED-DISPLAY.md)
- [Direct-IP networking](docs/NETWORKING.md)
- [Security model](docs/SECURITY.md)
- [Touch, pen and input](docs/INPUT.md)
- [Native-resolution measurements](docs/NATIVE-DISPLAY.md)
- [Roadmap](docs/ROADMAP.md)

## Credits and licensing

Created by [EEkebin](https://github.com/EEkebin). Third-party dependencies and notices are listed in [third_party](third_party/README.md) and included in the downloads. The driver derives from Microsoft's indirect-display sample and includes its license and attribution.

A project-wide open-source license has not been selected. Third-party components retain their own licenses. ExtraPane is independent of Microsoft.
