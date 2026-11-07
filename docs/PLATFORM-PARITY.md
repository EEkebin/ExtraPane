# Linux and Android resource parity

Implementation order: finish shared connection feedback, then Linux and Android,
then macOS. iOS remains deferred. Keep the ExtraPane name for now.

The target is one app per platform with grouped, independently enabled resources,
shared or per-resource destinations, compact connected summaries, temporary field
validation and connection toasts. A successful connection notification must follow
actual resource readiness, not the button click. Native media stays outside UI IPC.

## Integration gates

- Shared admission: authenticate once per destination and issue resource-scoped
  tickets; cancel pending requests, retain ownership until cleanup, and isolate
  failures between destinations. Do not reuse a one-time code independently.
- Linux: shared Qt connection model; separate display receiver windows; native
  GStreamer camera/microphone capture; session-owned virtual microphone; camera
  output only to a verified dedicated virtual device. Portal and endpoint
  prerequisites are resource-specific, not a reason to stop all hosting.
- Android: grouped resource selection, simultaneous display/camera/microphone
  where device permissions and hardware allow, foreground service lifecycle,
  native MediaCodec/Camera2/AudioRecord and shared C++ protocol/admission code.
  The Java screen is an interim frontend, not completed Qt UI consolidation.
- USB: never substitute file transfer for a real USB device. Linux export/import
  needs privileged backend ownership, unmount/eject/recovery and packaging work.
  Android USB export is not implemented and must remain visibly unavailable.
- Packaging: Windows installer regression, Linux AppImage dependencies and
  notices, Android signed APK workflow. No device connection, install or live
  GPU/camera/microphone test without user approval.

## Acceptance

Isolated validation, admission and lifecycle checks first; compile all supported
architectures; inspect UI rendering; then request physical-device testing.
Display + camera + microphone must connect together to a Windows server with a
single access code, stop independently and restore local resources. Linux normal
camera/microphone endpoints must be visible to consumer applications. Do not mark
these physical checks complete based only on compilation or API return values.

See IMPLEMENTATION-CHECKLIST.md for incremental evidence and remaining limits.

## Feasibility decisions (2026-11-07)

Android same-source camera/microphone sharing is implemented in the development branch using a shared producer and independent destinations. Linux already uses this model. Physical multi-server delivery remains a validation gate.

Linux USB is possible, but requires a narrowly scoped privileged helper, installed kernel USB/IP support, exact device ownership, unmount/eject/rebind and crash recovery. The AppImage-only deployment does not implement that installation lifecycle. Defer this separate integration rather than presenting file transfer as USB. See the Linux kernel USB/IP guide: https://docs.kernel.org/usb/usbip_protocol.html .

An ordinary Android APK can request access to selected USB peripherals, but mounted-storage export needs safe filesystem detachment; Android's MOUNT_UNMOUNT_FILESYSTEMS permission is not available to third-party apps. Generic imported USB devices also require a system virtual-host integration not provided here. USB remains unavailable in the stock APK; no rooting or security changes are proposed. References: https://developer.android.com/develop/connectivity/usb/host and https://developer.android.com/reference/android/Manifest.permission#MOUNT_UNMOUNT_FILESYSTEMS .

Proceed with native macOS camera/microphone sources and resource selection. Receiving a normal virtual camera needs a signed Core Media I/O system extension; a virtual microphone needs an installed Core Audio HAL plug-in. These are deployment gates beyond the current ad-hoc ZIP/DMG, not evidence that source capture is impossible. References: https://developer.apple.com/documentation/coremediaio/creating-a-camera-extension-with-core-media-i-o and https://developer.apple.com/documentation/coreaudio/creating-an-audio-server-driver-plug-in . Shared Qt frontend migration and complete resource parity remain unfinished.
