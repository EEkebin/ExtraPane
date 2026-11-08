# Devices workspace: acceptance checks

This guide is for the development preview on `feat/device-discovery`. It is not
evidence that these checks have passed on physical devices. Keep the existing
Server/Client workflow available until this preview has been accepted.

## Start the preview

Install the matching build on each test computer when you are ready. On Windows,
**Full installation (receive and share)** includes the managed receiving components.
The computer whose desktop will be extended needs the virtual-display driver.
**Share only** can offer its physical devices and serve as a display destination.
Do not run the legacy interface and the preview against the same resources together.

Launch the desktop executable with `--devices`. For example, from the Windows
installation directory:

```powershell
.\ExtraPane.exe --devices
```

Linux accepts the same argument on its AppImage. On macOS:

```sh
open -a ExtraPane --args --devices
```

Android exposes **Devices — offer this phone** from its existing interface. This
preview shares only while its activity is in the foreground; backgrounding stops
sharing and capture. Android cannot consume virtual camera/microphone endpoints.

## First connection: extend the desktop

1. On the laptop that will show the extra screen, open **Share**. Select a display
   destination, choose its connected local network address, and start sharing.
   Keep **Require pairing** enabled. Offering the destination alone must not create
   a virtual monitor or start a stream.
2. On the desktop being extended, open **Devices**. Find the laptop nearby or enter
   its IPv4 address and port. Enter the laptop's displayed six-digit code. Choose
   Automatic route, or the intended local interface on a multihomed computer.
3. Connect and select the advertised display destination. Use its advertised mode
   or enter a custom resolution and refresh target. The desktop creates the virtual
   monitor; the laptop opens the receiver window.
4. Verify picture, supported input, fullscreen and monitor arrangement. Disconnect
   only that resource. Its window and virtual monitor should close, while unrelated
   resources continue.

Refresh targets up to 360 FPS are requests, not guaranteed throughput. Native OS,
driver, encoder and display limits still apply. **Existing screen** is a separate,
currently view-only offer; it must not create a virtual monitor.

## Independent resources and lifecycle

- Add camera, microphone or speakers where both devices support the required native
  integration. Verify the resulting device in a receiving application, not just an
  ExtraPane status label. Advertising a camera or microphone must not activate it.
- Stop one resource while another is active. Pending connections must be individually
  cancellable. Disconnect all must disappear after all pending/active resources end.
- Select **Refresh resources** after attaching/removing hardware. Existing unchanged
  connections should survive a label or mode refresh; removed sources must stop.
- Close the main window during active work and restore it from the tray. Use Exit
  for full shutdown. With no work active, normal close should quit.

## Pairing and network behavior

- Reconnect through the saved-device entry without a code. A uniquely matching saved
  address/port also selects the expected identity; an identity mismatch must fail.
- Stop/start sharing within the same app process: the code stays unchanged. Restart
  the app: the code changes, while remembered-device trust remains.
- Forget a peer: its current access ends and its saved trust is removed.
- Test `localhost` or `127.0.0.1` separately for self-extension. Pairing still applies;
  the same computer needs the actual virtual-display integration.
- A brief network interruption may restore authenticated non-USB resources, bounded
  to five attempts within 30 seconds. Cancel must prevent restoration. USB must never
  reattach automatically. Do not unplug storage merely to test generic reconnect.

Cross-network connections use a manually reachable/forwarded TCP port on the offering
device. There is no UPnP. Gateway testing requires a third device: enable the gateway
explicitly, verify only its directly paired resources appear, and verify disabling it
ends relayed sessions. Gateway mode is off by default and supports one hop.

USB has a separate exclusive ownership/eject path. Test it only after the simpler
resources, using expendable test data and the normal eject action. If enumeration or
return fails, preserve the exact error and logs rather than repeatedly reattaching.

Record the package version, OS, offered resource, destination address/interface,
exact error and whether other resources continued. Track results in
[IMPLEMENTATION-CHECKLIST.md](IMPLEMENTATION-CHECKLIST.md).
