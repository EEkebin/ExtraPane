# Host and receiver apps (0.8)

Open **DM Host** on the desktop, click **Start display**, then open **DM Receiver** on the laptop and click **Connect**. The shortcuts point to `%LOCALAPPDATA%\DM\app`. Normal use does not involve SSH, Python or PowerShell. Host elevation is still required by the current virtual-display integration; the receiver runs as the normal signed-in user. The 0.7 display driver remains installed separately.

## Normal sessions

Debug is unchecked on every app launch. The host creates a real extended Windows desktop at the selected resolution (3000x2000/60 by default on this setup). No four-color test window is launched. Move an application onto the extra monitor and use the laptop normally. A blank desktop background before you move a window there is expected.

There is no frame count or elapsed-session limit. Disconnect from either app, close the receiver preview with **F9**, or close the app to stop. **F8** pauses/resumes receiver input. Connection loss, a frozen peer, unrecoverable GPU loss or deliberate display removal still end a session and release held input. Continuous does not mean automatic reconnection across sleep, Wi-Fi loss or reboot; click Start/Connect again after a disconnect.

The host Debug checkbox limits a session to 60 seconds and opens the color/motion/input diagnostic window. Debug on either endpoint displays connection details in that app. Debug is a runtime option independent of Release/Debug compiler configuration.

## Pairing and LAN settings

The two authorized test computers are provisioned with the same random 256-bit `dm-lan` pairing key, retained in each user's Windows Credential Manager. The key is never stored in Git, settings, logs or process arguments. For another setup, generate a code in either app, copy it to the other app, and click Save pairing on both. Copy before saving because Save clears the field. Forget pairing explicitly revokes the local credential; an existing pairing is never silently overwritten.

Choose the local LAN IPv4 address on each computer and enter the other computer's IPv4 address. Settings persist under `HKCU\Software\DM\Host` or `Receiver`. DHCP is not required; the selected address must actually belong to an active interface. Static Ethernet addresses work through the same path.

Start on the elevated host creates a Windows Firewall rule scoped to the engine executable, the selected local IPv4, receiver IPv4 and TCP port 43824. Normal disconnect/close removes it. If the UI crashes, the engine stops when its launcher exits; the narrowly scoped rule may remain until the next Start/Disconnect. An existing rule pointing to a different executable is not overwritten. The receiver initiates an outbound connection and needs no inbound rule.

Input remains explicitly controlled by each app's mouse/keyboard/touch/pen checkbox. Neither audio nor mic passthrough is implemented yet. No SSH password field exists. `scripts/deploy_test_apps.py` is development deployment/testing infrastructure, not a runtime dependency or part of the distributed app folder.

## Implementation and protocol

These first launchers use native C++ Windows controls around the existing C++/D3D11 engine. The planned C++/WinRT/WinUI 3 product shell remains a later UI migration. UI actions launch a hidden engine process and consume bounded status output; no pixels or pen samples go through UI text controls. Per-user settings exclude secrets. Controls scale with system DPI and fullscreen preview retains per-monitor DPI awareness.

Protocol version 6 retains `Offer.frames == 0` as continuous. Both endpoints must be updated; old offers/requests are rejected. Pixel, payload, input-batch, capability and authentication bounds remain enforced. Media timestamps and counters use 64-bit frame indices; the 32-bit wire sequence wraps explicitly. Default `dm-view host` is continuous; `--frames N` opts into a bounded diagnostic. `--timeout-ms 0` on the host waits for a receiver without an accept timeout. Network-operation and input-watchdog deadlines remain active.

The engine monitors its launcher's process lifetime. Disconnect terminates the owned engine; the separate input watchdog observes pipe closure and releases contacts. No kill-on-close job includes the input watchdog. The virtual monitor is session-owned and disappears when the engine ends. Host display setup reads actual pixels with per-monitor DPI awareness, places DM to the right of the physical layout and restores physical origins if Windows moves them during virtual-monitor arrival. It does not change physical resolutions or refresh rates.

## Validation

Release and Debug builds pass the five protocol/network/security/codec suites. Both app window-state smoke checks pass: visible window, enabled Start, disabled Disconnect, empty code field, Debug off and details panel toggling correctly. Native-mode host/receiver-loss and restart checks pass. Independent OpenSSL checks pass input permissions, epoch, sequence, size and pen-pressure rejection.

Automatic screenshot and mouse-click verification was unavailable because the Computer Use native pipe was missing. Window-state checks do not replace visual acceptance on each screen.

A real two-device GUI session (both apps invoking their normal Start/Connect action, Debug off) remained connected at the 30-, 80- and 135-second checkpoints, with an independent 3000x2000 monitor and `--frames 0`. No diagnostic pattern was started. Terminating the receiver launcher stopped its engine through the parent-process watch; the host released its session and the exact original two-monitor topology returned. The persistent pairing and shortcuts remain for normal use. This verifies continuous operation beyond the prior test durations, not an indefinite soak or leak-free operation over days. Evidence: ignored `out/gui-continuous-laptop.json`.

## Display Settings changes (0.9)

While connected, open Windows Settings > System > Display on the host. Select DM LAN Display, drag it into position, or change its resolution/orientation and click Apply/Keep changes. The session retains the same virtual monitor and encrypted connection while rebuilding capture, encoding and decoding for the actual Windows mode. Expect a brief frozen/black image during recovery. It does not reset Windows to the startup resolution on every change. Recovery waits up to 20 seconds for the selected display to stabilize; network loss and unsupported modes can still end a session.

The host launcher remembers the resulting resolution, refresh, rotation and position under `HKCU\Software\DM\Display`. Restarting with the same launcher resolution profile restores that layout. Selecting a different launcher resolution profile explicitly starts that profile instead. CLI users opt into this with `--keep-layout`. Only the most recent layout is retained, not separate layouts for multiple receivers.

Portrait video preserves its aspect ratio with black bars. Mouse, touch and pen map to the visible image; new pointer contacts in the bars are ignored. Held input is released during geometry changes, queued samples are discarded, and a fresh input epoch prevents stale replay. Native pointer injection accounts for negative virtual desktop origins.

Protocol v6 reserves a zero-payload frame header for a replacement offer and decoder-ready acknowledgement. Both endpoints must be updated together. The separately installed 0.7 display driver is unchanged. These changes do not add sleep/network reconnection or support disabling/cloning the DM monitor.

WinUI 3 and distributable portable Client/Server folders remain later work. A portable app bundle will still need a separately installed, signed virtual-display driver for extended desktop mode.

Validation on 2026-11-02: Release and Debug each pass all five suites. Continuous local sessions survived six changes covering every orientation, negative placement, 720p and 1080p, with the same monitor identity; restarting restored portrait geometry and placement. The native portrait test verified mouse/key/two-contact touch, pressure/tilt/rotation/barrel/eraser/inversion, watchdog cleanup and held key/button release after movement. Independent TLS input rejection checks pass with protocol v6. On the Surface Book 2, one 7,200-frame session survived four live changes, decoded every frame on the Intel GPU, and delivered generated mouse/key/two-contact touch/pen (pressure 897) after portrait recovery. Physical pen/touch acceptance through manually applied Settings changes remains a user check. Evidence: `out/display-reconfigure-test.json` and `out/laptop-reconfigure-test.json`.
