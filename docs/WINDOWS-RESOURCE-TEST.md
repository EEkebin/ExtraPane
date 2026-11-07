# Windows resource-sharing verification

This is a future test plan, not an instruction to start device tests. Isolated
builds do not establish actual driver/device delivery. Use matching development
packages on both PCs. Keep unrelated USB drives disconnected during the eventual
USB test, and use expendable test data on the selected removable drive.

## Multiple connections

Use the current installer on both PCs; custom display modes need its updated
display driver. Start Server on the receiving PC. On Client, leave Single server
checked and enter that server's address and port. Add the desired resource rows,
choose their sources, then check each row to connect and start it. There is no
separate Connect button. Uncheck a row to stop just that connection.
Uncheck Single server to enter a separate destination for each row.
Expand the Displays, Cameras, Microphones or USBs group to add/edit rows.
Collapsing a group must leave its streams running. Disconnect all within one
group must leave other groups running; page-wide Disconnect all stops everything.

During authorized device testing, verify two camera/microphone rows produce
separate OS endpoints, and that stopping one leaves the other running. Reusing
one source across servers must also leave other recipients working. Windows receiving slots are dynamically allocated. During authorized testing,
try more than four resources when hardware permits, and multiple USB imports
from different source PCs. Per-physical-device USB sharing remains exclusive.

Each display row opens a separate resizable window. Custom virtual monitor mode
requires no local monitor selection. Alternatively, choose Match local monitor
for native mode and fullscreen placement. Custom even resolutions must remain within
4096 pixels per dimension, 8,847,360 pixels total and 1–360 FPS. The physical
panel's refresh rate is not changed. Check arrangement restoration, fullscreen,
the top-center Controls handle, and the Keyboard action. Automatic keyboard
opening requests Windows On-Screen Keyboard on the server monitor for remote
fields exposing writable accessibility metadata. Test a plain textbox and a
modern editor after releasing the touch. Existing server keyboards are preserved;
Windows launch/position restrictions are reported in display.log. The manual
Keyboard action opens the receiver keyboard. This is the accessibility keyboard,
not a complete modern touch-keyboard/IME bridge. Update both endpoints.

With active server/client work, close the main window and double-click its tray
icon: the main window should return and focus without disconnecting anything.
With no work it should exit. Tray Exit requests normal shutdown; USB cleanup must
finish or report an exact ownership/identity problem without killing its helper
mid-cleanup. Interrupted return must retain its incomplete-write warning.

## Camera

1. Install Server + Client on the receiving PC, open Server, then start it on
   the selected interface and port. If access codes are enabled, generate one.
2. On the sending PC open Client, add Camera, open the device list and explicitly
   select the source, enter the receiving address/port/code, then check the row.
3. Select ExtraPane Camera in a recording app or browser. Verify picture,
   orientation, permissions, stop, reconnect and device unplug. The source negotiates its supported resolution and frame rate within the documented camera limits.
4. Check whether a second local application can open the source camera. The app
   does not detach it, but hardware/application exclusivity may prevent sharing.

## Microphone

1. Install Server + Client on the receiving PC. Restart Windows if setup requests it, then start the server.
2. On the sending PC open Client, add Microphone, open its list and select a
   source, then check the row. The source must expose a supported 48 kHz
   mono/stereo shared format.
3. Select ExtraPane Microphone in a recording application. Check real recorded
   audio, mute/unmute, permission withdrawal, stop and reconnect. This build sends
   mono PCM16; it does not yet use Opus or resample other source rates.
4. Verify local recording remains available on the source PC.

## USB storage

1. Install with USB storage sharing enabled on the sending PC and Server + Client on the recipient. These components are prepared by installation; there is no USB source setup button in the client. Do not replace independently installed incompatible backends.
2. Open the USB device dropdown on the source to load its current devices. Only eligible removable storage with complete
   identity and ownership information can connect. Protected disks, uncertain
   identities and already-owned devices must remain unavailable.
3. Close source files, start the recipient, then connect the selected source.
   Confirm Windows sees the actual imported USB storage device. Use only test
   files for initial write/read/hash verification.
4. With expendable test data and a destination file busy, uncheck USB on the
   source. It must attempt eject, then end its export and reclaim the exact local
   source if eject fails or times out. It must leave USB unchecked and report
   that writes may be incomplete. Disconnect all must finish after ownership
   cleanup; a busy receiver must not silently recheck USB or cancel disconnect.
5. After confirmed eject, wait for automatic return on the source. No separate
   recovery action should be needed. Verify the disk returns and test-file hashes
   match. Restoration must refuse a
   changed identity, foreign ownership or a still-running exporter.
6. Only after clean transfer/eject succeeds, separately authorize interrupted
   connection/parent-exit tests. The helper must stop its exporter, release its
   exact binding and restore the verified source's prior online state, without
   claiming clean eject. Also test unplugging: stale absent ownership must be
   removable by uninstall without reconnecting the card or touching other disks.
   Do not equate UI close with killing the USB helper or powering off Windows;
   those cases need separate validation and may leave interrupted state.

## Current limits

The server accepts all supported resource types on the same port; there is no
server resource selector. Checking a configured resource establishes its server
connection automatically. No monitor or capture should start merely by adding a row.
Check Display, Camera and Microphone together, choose both source devices, and
verify that the server receives the camera/microphone while its extended desktop
shows on the client. Uncheck one resource and confirm the others continue. Recheck it to restart; Disconnect all must clear every checkbox after USB ownership return completes. Mute the
microphone independently. For access-code mode, enter one fresh code for the
connection; it issues fresh short-lived, single-use resource tickets when checkboxes start resources.
Windows USB receivers support independent imports, but the source backend still
allows only one exporter process per source PC. Linux/macOS new media
resources and raw USB are not covered by this Windows milestone. Android camera
and microphone source code/APK are present but require their own physical checks.
The combined portable package contains setup helpers; drivers still require
explicit machine-wide setup. The current development installer bundles receiving components; the new live-resource and unified-uninstall changes still require end-to-end user testing. Choose Server + Client on the receiving desktop and Client only on the source laptop.

## Live camera mirroring and device pickers

On the Windows client, open each camera, microphone or USB dropdown to load its
current inventory. No separate device Refresh action is needed. While sharing a
camera, toggle Mirror Camera and confirm horizontal mirroring changes in the
receiving application without reconnecting. Toggle off to restore the original
orientation. Check colored objects as well as text so UV chroma ordering is tested.
