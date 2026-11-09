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

In builds after `pre.92.1`, open ExtraPane normally. From the Windows installation
directory:

```powershell
.\ExtraPane.exe
```

Open the Linux AppImage normally. On macOS:

```sh
open -a ExtraPane
```

Desktop `--legacy` opens the older interface; `--devices` remains supported.
Use a build newer than `pre.92.1`: that build defaults to the old interface and
its Windows package cannot load the new page because of a resource-path error.
Android opens device sharing normally, with **Legacy connections** for the older
workflow. This preview shares only while its activity is in the foreground; backgrounding stops
sharing and capture. Android cannot consume virtual camera/microphone endpoints.

## Navigation and resource groups

Resources are grouped into Monitors, Cameras, Speakers, Microphones and USB.
Click or tap a group header to expand or collapse it. Collapsing a group must
retain its selections and active sessions. Within Monitors, extending a desktop
and viewing an existing screen remain separate actions.
On Devices these appear under **Use as an extra monitor** and **Share an existing
screen (view only)**. The first offers this device as a receiving screen: choose
**Custom resolution** or a named monitor whose mode supplies the initial settings.
The second sends an already-existing screen's picture. A monitor may appear once
in each section because these are different uses of the same hardware. Resource
names use readable monitor labels rather than raw Windows display paths; the
resource identities and saved sharing choices are unchanged.

On Android, use the top-left menu for Devices, Resources and About.
Check portrait and landscape layouts: the toolbar and bottom controls must stay
clear of system bars, and resource labels must wrap without hiding actions.
Switch pages while multiple resources are active; navigation must not stop them.
The phone's Devices page manages pairing and saved/connected peers. Resources
shows its available resources and active streams; request those resources from
the other computer. Devices also contains provider permissions and availability settings. Android resource consumption remains limited to the
older workflow available through About's Legacy connections action.

For Android audio, expect recognizable route names such as Loudspeaker, Earpiece,
Built-in microphone and connected headset/USB names, rather than internal output
numbers. Verify the actual selected output with playback; an advertised route is
not proof the device's audio policy permits playback through it. Unplugging a
selected headset must not redirect its session to the loudspeaker.

## First connection: extend the desktop

1. On the laptop that will show the extra screen, open **Devices**, check its
   connected local network address. Supported resources are available automatically;
   no per-resource enable or separate Start sharing action is needed.
   Keep **Require pairing** enabled. Offering the destination alone must not create
   a virtual monitor or start a stream.
2. On the desktop being extended, open **Devices**. Press **Search nearby** to search for new devices. Select the laptop, or expand **Connect by address** and enter its
   IPv4 address and port. Enter the laptop's displayed six-digit code. Choose
   Automatic route, or the intended local interface on a multihomed computer.
3. Connect, open **Resources**, and select the advertised display destination. Use its advertised mode
   or enter a custom resolution and refresh target. The desktop creates the virtual
   monitor; the laptop opens the receiver window.
4. Verify picture, supported input, fullscreen and monitor arrangement. Disconnect
   only that resource. Its window and virtual monitor should close, while unrelated
   resources continue.

Refresh targets up to 360 FPS are requests, not guaranteed throughput. Native OS,
driver, encoder and display limits still apply. **Existing screen** is a separate,
currently view-only offer; it must not create a virtual monitor.

## Device cards

Click a connected device header to collapse or expand its resource groups. Collapse
one device while another stays expanded; refresh its catalog and confirm the
collapsed state stays with the same device. Active video/audio/camera/USB sessions
must continue unchanged. The header's Disconnect action remains distinct. Android
paired-device cards also collapse their details without forgetting or disconnecting
the peer.

## Independent resources and lifecycle

- Add camera, microphone or speakers where both devices support the required native
  integration. Verify the resulting device in a receiving application, not just an
  ExtraPane status label. Advertising a camera or microphone must not activate it.
- Stop one resource while another is active. Pending connections must be individually
  cancellable. Disconnect all must disappear after all pending/active resources end.
- Disconnect the last active local resource: its power returns to Available while
  the listener and advertisement remain available. Disconnect all shared resources
  stops incoming uses only, preserving unrelated outgoing uses. Pair without
  starting a resource: no camera LED, microphone capture, virtual monitor or USB
  ownership change should occur.
- Use one of two cameras and confirm only the exact camera turns red on both peers.
  Its provider must identify the consumer. Add another consumer to the same source
  where supported, then stop one session and verify the remaining use stays red.
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


## One-sided saved pairing

If one device forgets a pairing while the other still remembers it, a new-code
pairing can currently fail with a generic TLS closure. Confirm the two identities
and remove the stale pairing on the device that still remembers it, then pair
again using the current code. Do not disable pairing, clear every device's trust,
or infer that TLS/network encryption itself failed from this message. An ordinary
installer update is intended to preserve pairing data.


### Devices / Resources navigation

- Devices: press Search nearby; no search starts at launch. After pairing, the
  peer disappears from Nearby and appears in the saved/connected device list.
- A connected device has no Connect button. Disconnect restores that action.
- Resources: expand a device card, then a resource group, and use its power
  button. Moving pages or collapsing cards must preserve active resources.
- About: Running version must match the installed package version.


### Remembered peers and first camera startup

- Restart the app with a paired peer available. Its control connection and catalog
  should appear without pressing Connect; no camera or microphone should activate.
- Open a camera as the first resource on a newly connected peer. Delayed setup or
  a rejected camera must not remove the device or its other resources. A retry
  should be possible without reconnecting/re-pairing the device.
- Disconnect a device explicitly: it must stay disconnected until manual Connect
  or a fresh app start. Forget must remove it from automatic reconnect candidates.
- New-device Nearby search remains manual; automatic reconnect never pairs an
  unknown device or bypasses a changed saved identity.


### Discovery and About presentation

- Press the refresh icon next to Nearby devices. It spins with three bouncing
  dots during the explicit search. Cards should appear as results arrive, not
  only when the scan completes. Repeated clicks must not create duplicate scans.
- Search completion/failure stops the animation. Paired peers stay out of Nearby.
- Cards use a neutral computer glyph because current discovery metadata does not
  identify the OS or device class. Names are displayed as plain text.
- About shows the current version, a smoothly scaled logo, product information
  and project links. Resize the window and check wrapping/scrolling. Device
  connection status should not appear as About content.
