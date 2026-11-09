# Android to Windows camera preview

Device testing is deferred until camera, microphone and USB implementation is
complete, per the user's updated direction. This guide is for that later test.
The camera development build has passed isolated checks. It is not
a verified camera release. Isolated checks cannot establish Windows Frame Server
activation, phone capture, GPU decoding or delivery in Discord/a browser.

## Setup and first picture

1. Close other ExtraPane instances. Extract the matching Windows preview package
   and open ExtraPane.exe. Select Server, then Camera (preview).
2. Click Camera setup and approve UAC. This installs the machine-wide camera
   component; it does not install a virtual display driver. See CAMERA-SETUP.md
   for ownership, removal and rollback behavior.
3. Select a connected local IPv4 address and port, then start the server. Use a
   trusted LAN, or enable the access-code option and generate a code. Windows may
   require Private-network firewall permission for ExtraPane.Resources.exe; this
   preview has not changed your firewall rules.
4. Install the matching Android test APK. It is signed with a development key.
   If Android refuses an update over an existing release, stop and check signing
   identity rather than uninstalling your existing app and losing its settings.
5. Enter the Windows address, port and code (if enabled) on Android. Select a
   camera, choose Share selected camera, and allow camera permission.
6. In Discord or a browser camera selector on Windows, choose the ExtraPane
   camera. Confirm a moving picture and check colors and orientation.

Camera mode and the existing display mode are separate in this preview. Stop one
before starting the other on the same port. The phone sends sensor-native
landscape frames; automatic rotation and front-camera mirroring are not implemented.
It does not deliberately detach the source camera, but Android may enforce
exclusive access when another app is using it.

## Lifecycle checks after the first picture

For Windows-to-Windows camera sharing, use Camera (preview) on the receiving
computer's Server page. On the sending computer, open Client, choose Camera,
click Refresh cameras and select the device explicitly. Enter the receiving
computer's address/port/code and share. Only the receiving computer needs Camera
setup. The initial Windows sender requests 1280x720 at 30 FPS and reports an error
if the selected camera cannot supply that format. Stop sharing releases the
session; no source device is deliberately detached. Windows privacy settings and
another application's exclusive camera ownership may still prevent capture.

- Stop sharing from Android, then reconnect. The Windows server should remain
  listening, and the previous camera session should release its resources.
- Repeat using Stop in the Android foreground-service notification.
- Check backgrounding, locking, permission withdrawal and connection loss.
- Stop the Windows server and confirm the session-owned camera disappears.

Record the result and exact errors in IMPLEMENTATION-CHECKLIST.md. Physical tests
require user approval before an agent runs them. No device tests, component
installation, camera access or firewall changes were performed to create this
package. The installer source is prepared, but installer compilation is pending;
use this portable development package for the first test.
