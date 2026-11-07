# Windows resource-sharing verification

This is a future test plan, not an instruction to start device tests. Isolated
builds do not establish actual driver/device delivery. Use matching development
packages on both PCs. Keep unrelated USB drives disconnected during the eventual
USB test, and use expendable test data on the selected removable drive.

## Multiple connections

Use the current installer on both PCs; custom display modes need its updated
display driver. Start Server on the receiving PC. On Client, leave Single server
checked and enter that server's address and port. Connect to server may be used
with no resource selected. Add the desired resource rows, choose their sources,
then check each row to start it. Uncheck a row to stop just that connection.
Uncheck Single server to enter a separate destination for each row.

During authorized device testing, verify two camera/microphone rows produce
separate OS endpoints, and that stopping one leaves the other running. Reusing
one source across servers must also leave other recipients working. Receiving
limits are four displays, four cameras, four microphones and one USB storage
connection per server. USB sharing remains exclusive.

Each display row opens a separate resizable window. Choose a local monitor for
native mode and fullscreen placement, or use a custom even resolution within
4096 pixels per dimension, 8,847,360 pixels total and 1–60 FPS. The physical
panel's refresh rate is not changed. Check arrangement restoration, fullscreen,
the top-center Controls handle, and the Keyboard action. Automatic keyboard
opening uses Windows On-Screen Keyboard for remote fields exposing supported
accessibility metadata; it is not a complete modern touch-keyboard/IME bridge.

With active server/client work, close the main window and double-click its tray
icon: the main window should return and focus without disconnecting anything.
With no work it should exit. Tray Exit requests normal shutdown; a USB eject
blocker must remain visible and prevent forced storage return.

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
4. With a destination file busy, uncheck USB on the source. It must report the
   blocker, recheck USB and keep sharing alive. Close files and retry. Also test
   Disconnect all: other resources stop, but a busy USB device keeps its session
   until safe return succeeds. Eject USB on the receiver requests the same return.
5. After confirmed eject, wait for automatic return on the source. No separate
   recovery action should be needed. Verify the disk returns and test-file hashes
   match. Restoration must refuse a
   changed identity or missing eject/release evidence.
6. Only after clean transfer/eject succeeds, separately authorize interrupted
   connection/parent-exit tests. The source must remain offline. A bound or
   uncertain recovery record requires administrator investigation; this app has
   no force-online operation.

## Current limits

The server accepts all supported resource types on the same port; there is no
server resource selector. Clients can select resources before or after Connect. Also test Connect with nothing checked: the status should show connected without creating a monitor or capturing media.
Check Display, Camera and Microphone together, choose both source devices, and
verify that the server receives the camera/microphone while its extended desktop
shows on the client. Uncheck one resource and confirm the others continue. Recheck it to restart; Disconnect all must clear every checkbox after any USB return completes (a busy USB veto leaves USB checked). Mute the
microphone independently. For access-code mode, enter one fresh code for the
connection; it issues fresh short-lived, single-use resource tickets when checkboxes start resources.
Windows USB is one removable-storage session per receiver. Linux/macOS new media
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
