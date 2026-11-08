# ExtraPane

![ExtraPane ? extend your desktop over your local network](assets/branding/banner.svg)

<img src="assets/branding/icon.png" alt="ExtraPane icon" width="96" align="right">

ExtraPane uses another device as an extra monitor over Wi-Fi or Ethernet. Windows and Linux can host; a macOS host preview is in development with an initial M3 Pro session reported working. Windows, Linux and Android clients are implemented, with a macOS Universal client preview verified for Windows-host video and mouse/keyboard on an M3 Pro. Mouse, keyboard, touch and pen availability depends on the client and host; see the platform guides below.

[Project website](https://eekebin.github.io/ExtraPane/) | [Download latest release](https://github.com/EEkebin/ExtraPane/releases/latest) | [Report an issue](https://github.com/EEkebin/ExtraPane/issues) | [Build status](https://github.com/EEkebin/ExtraPane/actions/workflows/prerelease.yml)

The desktop app contains Server, Client, Settings and About pages. A PC can perform either role. Windows servers allocate connections dynamically, subject to available hardware and OS resources; the last device-validated Linux release supports one display client. The resource-sharing development branch adds independent sessions and camera/microphone endpoints; see [Linux development status](docs/LINUX.md). Android is currently a client only. The macOS development preview adds hosting on macOS 13+, with Universal ZIP/DMG packaging verified; broader host hardware validation remains pending.

> This branch contains the device-sharing development build. Published releases
> may have an older interface or fewer features. See the [current implementation
> checklist](docs/IMPLEMENTATION-CHECKLIST.md#current-status--2026-11-08) for what
> is implemented, tested and still pending.

The development build also adds [Speakers](docs/SPEAKERS.md): a Windows server
playback device that sends audio to a selected Windows or Android client output.
This new path has isolated checks; physical playback verification is pending.

The `feat/device-discovery` branch is developing a device-oriented interface with
explicit resource offers, LAN discovery, direct-address connections and remembered
pairing. This redesign is not yet a tested replacement for the existing Server /
Client workflow. Its current gates are recorded in the [implementation
checklist](docs/IMPLEMENTATION-CHECKLIST.md#active-implementation-devices-sharing-and-gateways).

## Devices workspace (development preview)

Use the [device acceptance guide](docs/DEVICE-WORKSPACE-TESTING.md) when testing
this preview. Build checks do not replace physical media and driver verification.

Launch the desktop app with `--devices` to try the new workspace. The existing
Server/Client interface remains the default until cross-platform build and device
acceptance checks pass. Android exposes the preview through **Devices ? offer this
phone**. Do not assume a published package contains this branch's preview.

Devices are peers: a computer can offer its camera while using another device's
speakers. **Share** selects available resources and a local network address;
**Start sharing** makes those offers available without starting capture. **Devices**
finds nearby peers or connects to an entered IPv4 address and port. Select a
resource to use it; **Connections** stops individual resources independently.

A **display destination** is the device that shows an extended desktop. The
computer using that destination creates the virtual monitor and needs its native
virtual-display support. Choose the destination's advertised monitor mode or a
custom resolution/refresh rate. **Existing screen** instead captures an existing
monitor; it does not create an extended desktop.

Pairing is enabled by default. Enter the other device's six-digit code on the
first connection; remembered devices subsequently prove their saved secret.
Codes stay fixed for the app/service process lifetime, including sharing restarts.
Disabling pairing requires an explicit unverified connection. Discovery is only a
hint, never proof of identity. Direct IPv4, `localhost` and `127.0.0.1` work without
multicast; loopback alone does not bypass authentication.

For another network, manually forward the chosen TCP port to the sharing device
and enter its reachable address and code. There is no UPnP. Optional gateway
sharing is off by default: an authenticated trusted visitor can use explicitly
offered resources from devices paired with the gateway, through one relay hop.
This does not expose arbitrary network services or recursively discover networks.

Windows, Linux and macOS use native platform adapters with different endpoint
capabilities; Android currently offers supported local resources while foregrounded.
The preview does not imply that every OS can receive every resource type. See the
[implementation checklist](docs/IMPLEMENTATION-CHECKLIST.md) and platform guides
for unsupported paths and verification limits.

## Requirements

The following requirements apply to Windows. See [Linux setup](docs/LINUX.md), [Android setup](docs/ANDROID.md) and [macOS setup](docs/MACOS.md) for those platforms.

- Windows 11 x64 on each PC.
- A reachable IPv4 connection over Wi-Fi, Ethernet or a direct cable with static addresses. DHCP and internet access are not required for a connection.
- Compatible D3D11 graphics hardware and H.264 encoding/decoding support.
- The ExtraPane virtual-display driver on each PC that will act as a server. Driver setup and starting the server require administrator permission.
- A touchscreen or compatible pen on the client to use those input features.

Current Windows development packages include the Qt runtime and native helpers. Users do not need Python, SSH, Visual Studio or a Vulkan SDK.

## Install

ExtraPane was previously named SideScreen. Older releases retain that name; current packages use ExtraPane. Windows upgrades preserve the existing installation identity and driver ownership.

The release workflow produces a Windows installer and portable ZIP, a Linux x86_64 AppImage with dependency notices, a signed Android client APK, a macOS Universal ZIP/DMG preview, and combined `SHA256SUMS.txt`. Use matching releases on both endpoints. See [Android installation and input support](docs/ANDROID.md).

### Installer

Run `ExtraPane-<version>-Setup-win-x64.exe` and select how this PC will be used:

| Setup choice | What it installs |
| --- | --- |
| **Share only** | The app and USB source support. Offer local cameras, microphones, speakers, storage and display destinations; receiving virtual-device components are omitted. |
| **Full installation** | Receive and share resources: the app, virtual-display driver, camera component, audio/USB receiving components and USB source support. Driver installation requires explicit certificate consent and administrator permission. |

When ExtraPane is already installed, setup offers **Update**, **Restore**, and
**Uninstall**. Update is available only when the installer version is newer and
keeps your settings. Restore removes ExtraPane-owned components and current-user
settings, then installs the bundled version again. Uninstall removes them without
reinstalling. Both removal choices preserve independently installed shared drivers
and stop if the registered uninstaller cannot finish cleanup.

Both roles include USB source support automatically. Full installation requires administrator permission and explicit certificate consent through the driver page's **I agree - Next** button. If driver installation fails after the app is copied, setup reports failure and retains ExtraPane for repair or removal. Share only does not hide the Server page; to host later, rerun setup as Full installation. The main installer manages the display driver directly, and Windows Apps lists one ExtraPane uninstall entry.

The app is registered in Windows **Settings > Apps > Installed apps**. Setup offers a desktop shortcut and a checked **Launch ExtraPane** option on its Finish page. Silent setup does not launch the app. Updating requests that running ExtraPane instances shut down and waits for their server helpers to stop before replacing files.

### Portable ZIP

Extract the entire ZIP and run `ExtraPane.exe`. Keep the accompanying files together.

For a Windows server, use the matching installer with **Full installation** to prepare the managed display, camera, microphone and USB components before running a portable copy. Extracting a ZIP does not install drivers. The older standalone `ExtraPane.DriverSetup.exe` belongs to previous display-only packages.

### Driver signing and Windows policy

The current driver uses a development code-signing certificate. It is **not Microsoft release signed**. With your consent, driver setup adds the package's public certificate to the machine's Trusted Root and Trusted Publishers stores and installs the verified package in the Windows Driver Store. The signing private key is discarded during packaging and is never included in downloads.

Setup does not disable Secure Boot or Memory Integrity, enable test-signing mode, or restart Windows automatically. Windows policy may still reject the driver when it loads. Successful package installation does not prove that every Windows configuration can load it. If setup or hosting fails, see [driver installation and troubleshooting](docs/DRIVER-INSTALLATION.md).

Windows apps and installers are currently unsigned. The macOS preview is ad-hoc signed, not notarized. The bundled driver is development-signed; compatibility on a new PC depends on Windows policy and the hardware in use.

## Access codes and reachable servers

The access-code build adds an optional **Require access code** setting on Windows, Linux and macOS servers. Enter the server IPv4 address, TCP port and generated six-digit code on any supported client, including Android. Codes expire after five minutes, admit one session and have a shared five-attempt budget. Generate a new code to reconnect or admit another client. Existing sessions continue when a new code is generated.

For a connection across networks, the server address and port must already be reachable. Router forwarding and external firewall configuration are up to you; there is no relay or automatic UPnP. Always require a code for a public listener. Without a code, automatic LAN mode does not verify peer identity. See [access-code setup and security](docs/ACCESS-CODES.md) and check release notes to confirm your downloaded build includes this feature.

## Connect

1. On the receiving/hosting PC, open **Server** and select **Start server**. Approve the helper's administrator prompt when required.
2. On the other device, open **Client** and enter the server's IPv4 address and port (default `43824`). Supply its access code if required.
3. Add the resources you want to share, select their source/settings, and use each row's green power button to connect. Display extends the server desktop onto the client; camera and microphone send the client's capture to the server.
4. Use a row's red power button to stop it, or **Disconnect all** to stop every resource. Stopping the last resource also closes its remaining admission connection; idle Disconnect all controls are hidden.

**Single server** supplies one destination to all rows. With it disabled, rows
can target different servers. Resource groups expand/collapse without ending
connections. The Windows/Linux sidebar slides between labels and an icon rail;
its arrow points toward the available expand/collapse action.

A virtual monitor is created only when a display resource connects. Display rows
default to a custom 1920x1080 monitor at 60 FPS; matching a local monitor is
optional. Custom target rates up to 360 FPS must pass driver, encoder and receiver
validation. This is a requested rate, not a throughput guarantee.

Use Windows **Settings > System > Display** to arrange monitors or change
orientation. Disconnecting one display removes only its monitor; the server and
other clients stay running. **Stop server** or tray **Exit** ends owned sessions.

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
| Stop hosting | Server > Stop server, or tray > Exit |

The preview is resizable and supports maximize, minimize and fullscreen. Top-right taps go to the remote application. Pen events do not open the toolbar.

On the shared Windows/Linux desktop interface, closing the main window keeps the app in the tray while a server or client resource is active. With no active work it exits normally. Double-click the tray icon to restore the window. Normal sessions have no test pattern or timeout; Debug is off by default and enables bounded diagnostic sessions.

## Settings

Select the client renderer in the main app's **Settings** page before connecting: **D3D11**, **D3D12**, **Vulkan**, or **OpenGL**. The choice is saved on that PC and locked during reception. D3D11 is the default and fallback. Unsupported options remain visible and disabled with a yellow explanation. A renderer failure reports its reason and falls back to D3D11.

All renderers use D3D11 H.264 decoding. Server capture also remains D3D11. Vulkan uses the installed graphics driver's runtime. OpenGL requires working D3D11 texture sharing through `WGL_NV_DX_interop2`; GPU assignment matters on hybrid laptops. Additional renderers are choices, not promises of improved performance. See the [D3D12](docs/D3D12.md), [Vulkan](docs/VULKAN.md) and [OpenGL](docs/OPENGL.md) notes.

Native stream resolution is the default. The preview toolbar's Settings menu offers **75%** and **50%** stream resolution for the next connection only. These reduce transmitted resolution without changing Windows monitor dimensions. The choice is not saved and resets to Native after use, failure or app exit. There is no automatic quality switching under load.

Client/server input permissions and debug options are in the shared Settings page. Stop the corresponding role to change its options. Statistics are off by default and report local receive/decode/presentation measurements, not end-to-end latency.

## Input and current limits

Mouse, keyboard, multi-touch and exposed pen pressure, tilt, buttons, inversion and eraser data are supported. Actual behavior depends on the input device, Windows APIs and the host application. Arbitrary vendor pen features and legacy WinTab support are not guaranteed.

Windows has one shared cursor and keyboard focus. Multiple clients extend the same desktop; they are not independent user sessions. Input cleanup releases session-owned keys, buttons and contacts on disconnect.

Not implemented: system audio, Windows+K/Miracast, IPv6 transport, automatic client reconnection, Windows ARM64 builds, and DLSS/upscaling/frame generation. Microphone sharing is available in the device-sharing development branch described below. macOS packages contain Intel and Apple Silicon binaries. Custom LAN connections do not appear in Windows Cast.

## Uninstall

Uninstall **ExtraPane** from Windows Installed apps. The single uninstaller removes
its managed display, camera, microphone and USB-source components, then clears
current-account settings and the known legacy pairing. Administrator permission
is required. Independently installed/shared drivers and pre-existing certificate
trust are preserved; files and registrations with changed or unproven ownership
are not deleted.

You do not need to reconnect a missing USB card just to update the application.
The installer reuses an exact verified compatible USB backend without changing
its bindings or recovery records. During uninstall, the USB helper can retire
verified inactive ownership for an absent device, keeping an audit record; this
is not a clean-eject claim and does not write to a replacement card. A live export,
foreign registration or ambiguous ownership still stops removal with a useful
message, while keeping the cleanup helpers available. See [USB lifecycle and
setup](docs/USB.md) and [driver installation](docs/DRIVER-INSTALLATION.md).

The latest development cleanup also handles verified owned removal-pending USB
stubs and stopped, disabled orphan services whose driver image is missing.
Unrelated/shared drivers remain protected. Component-success popups are suppressed;
actionable failures and a required final restart can still be reported. A passing
removal precheck is not proof that the complete uninstall has finished.

## Build and verification

The Android client's first Pixel 8a test received a Windows extended desktop using hardware H.264 decoding; picture and physical touch were user-verified. Native stylus and physical-keyboard forwarding are implemented, with hardware-specific validation still needed. Android hosting, system-audio playback and on-screen keyboard forwarding remain unimplemented. See [Android build and validation](docs/ANDROID.md).

Linux x64 builds can receive a Windows desktop and host a Linux virtual monitor through KDE's Wayland desktop portal. Both directions, including mouse and keyboard input, have been verified in an Arch VM. The last device-validated Linux host supports one display client and uses software H.264 encoding; the portal controls monitor dimensions. Native Linux client touch/pen capture has synthetic checks, but physical devices remain unverified. AppImage packaging and extracted-app startup checks pass in GitHub builds. See [Linux setup, capabilities and validation](docs/LINUX.md).

The current desktop development interface uses Qt/QML over C++20 native engines; Android and macOS use native frontends. The earlier WinUI interface remains in the repository. Install Visual Studio C++ desktop tools, CMake, the Windows SDK, Python 3, and Inno Setup 6. Driver build scripts restore hash-pinned Microsoft SDK/WDK packages into the build directory.

The current package pipeline is [Build and release](.github/workflows/prerelease.yml),
with a [Windows shared-UI job](.github/workflows/windows-shared.yml) and separate
Linux, Android and macOS jobs. Follow the pinned prerequisites and commands in
[resource-sharing development](docs/RESOURCE-SHARING.md); the older
`build-apps.ps1` pipeline builds the historical WinUI application.

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

## Device-sharing development

The `feat/shared-resources` branch adds grouped, independent display, camera,
microphone and USB connections. These changes are not yet a published release.

- Windows includes camera/microphone receiving components and exclusive removable
  USB storage sharing. Installation, recovery and combined-device tests remain
  part of the acceptance checks.
- Linux and Android can share compatible camera/microphone capture with several
  destinations. Linux virtual receiving endpoints require the documented system
  prerequisites; USB integration remains unfinished on both platforms.
- macOS adds native camera/microphone sending and repeated resource rows. Universal
  builds cover Intel and Apple Silicon. Virtual camera/microphone receiving, USB,
  same-source fanout and multiple client display windows remain unfinished.

Windows microphone reception now uses a 40 ms startup reserve, adapting up to
80 ms after starvation, with bounded storage and short transition fades. This
benefits every source platform sending to Windows. Isolated timing tests pass;
the reported phone-microphone/three-display workload still needs a device retest.
Linux's equivalent clock-paced jitter buffer remains pending. See the
[microphone endpoint notes](apps/microphone-endpoint/README.md).

Windows and Linux use the shared Qt interface in this development work. Android
and macOS still use their native frontends; shared-UI parity is not complete.
Isolated checks and package builds do not establish physical capture or delivery
to receiving applications. See the [implementation checklist](docs/IMPLEMENTATION-CHECKLIST.md),
[resource design and development build commands](docs/RESOURCE-SHARING.md) and
[Windows verification guide](docs/WINDOWS-RESOURCE-TEST.md) for evidence and test gates.
