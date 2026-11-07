# Windows resource-sharing verification

This is a future test plan, not an instruction to start device tests. Isolated
builds do not establish actual driver/device delivery. Use matching development
packages on both PCs. Keep unrelated USB drives disconnected during the eventual
USB test, and use expendable test data on the selected removable drive.

## Camera

1. On the receiving PC open Server, run Camera setup, then start it on
   the selected interface and port. If access codes are enabled, generate one.
2. On the sending PC open Client, check Camera, refresh and explicitly select the
   source, enter the receiving address/port/code, and connect.
3. Select ExtraPane Camera in a recording app or browser. Verify picture,
   orientation, permissions, stop, reconnect and device unplug. Capture currently
   requires 1280 ? 720 at 30 FPS.
4. Check whether a second local application can open the source camera. The app
   does not detach it, but hardware/application exclusivity may prevent sharing.

## Microphone

1. On the receiving PC open Server and run Receiving component setup. Restart Windows if setup requests it, then start the server.
2. On the sending PC open Client, check Microphone, refresh/select a source, and
   connect. The source must expose a supported 48 kHz mono/stereo shared format.
3. Select ExtraPane Microphone in a recording application. Check real recorded
   audio, mute/unmute, permission withdrawal, stop and reconnect. This build sends
   mono PCM16; it does not yet use Opus or resample other source rates.
4. Verify local recording remains available on the source PC.

## USB storage

1. Install the USB source component on the sending PC by checking USB on Client and clicking USB source setup. On the recipient use Server ? Receiving component setup. Do not replace independently installed incompatible backends.
2. Refresh storage on the source. Only eligible removable storage with complete
   identity and ownership information can connect. Protected disks, uncertain
   identities and already-owned devices must remain unavailable.
3. Close source files, start the recipient, then connect the selected source.
   Confirm Windows sees the actual imported USB storage device. Use only test
   files for initial write/read/hash verification.
4. With a destination file busy, try Eject USB. It must refuse while keeping the
   transfer alive. Close files and retry. Stop server also requests safe eject.
5. After confirmed eject, wait for the source session to finish. On the source
   select Refresh recovery, then the matching record and Restore selected disk.
   Verify the disk returns and test-file hashes match. Restoration must refuse a
   changed identity or missing eject/release evidence.
6. Only after clean transfer/eject succeeds, separately authorize interrupted
   connection/parent-exit tests. The source must remain offline. A bound or
   uncertain recovery record requires administrator investigation; this app has
   no force-online operation.

## Current limits

The server accepts all supported resource types on the same port; there is no
server resource selector. Clients select any combination of available resource checkboxes before Connect.
Check Display, Camera and Microphone together, choose both source devices, and
verify that the server receives the camera/microphone while its extended desktop
shows on the client. Stop one resource and confirm the others continue. Mute the
microphone independently. For access-code mode, enter one fresh code for the
whole selection; it issues short-lived, single-use resource tickets.
Windows USB is one removable-storage session per receiver. Linux/macOS new media
resources and raw USB are not covered by this Windows milestone. Android camera
and microphone source code/APK are present but require their own physical checks.
The combined portable package contains setup helpers; drivers still require
explicit machine-wide setup. The updated Inno installer has not been compiled.
