# Android client

The existing Android display receiver uses protocol 9 with Windows or Linux hosts. Development on `feat/shared-resources` adds combined display, camera and microphone connections described below. Android display hosting, system-audio playback and on-screen keyboard forwarding remain unimplemented.

## Architecture

- Android 11/API 30 minimum; ARM64 and x86_64 APK libraries.
- C++20 shared video/input protocol parsers and serializers through a small JNI boundary. Invalid lengths, grants and input events use the existing native validation.
- Android platform Java UI, network/TLS APIs, SurfaceView and MediaCodec. H.264 is decoded directly to a Surface; pixels do not pass through UI JSON or bitmap copies.
- TLS 1.3 over TCP 43824. Automatic LAN mode intentionally encrypts without checking peer identity, matching Windows/Linux. Its socket-local trust policy does not change system trust. No pairing or discovery.
- Numeric IPv4, with automatic routing or a connected Wi-Fi/Ethernet network/address selection. No DHCP or DNS dependency in the protocol.
- Native phone display requests its dimensions and refresh capped at 60 FPS. Custom mode requests exact even dimensions up to 4096 per axis, 8,847,360 total pixels and 1?360 target FPS. The host/codec/hardware must support the request; this is not a promise of achieved throughput.
- Bounded input and decoder queues, input acknowledgements before decode, and same-session decoder recreation when the host sends a new display offer. A stalled decoder ends the connection rather than accumulating latency.
- Native multi-touch, mouse, physical keyboard and stylus capture. Only host-granted input families are sent. Stylus events are never converted into mouse events.
- Aspect-fit SurfaceView coordinates match input mapping. Native touch pressure/contact area is sent when available; fidelity needs physical application verification.
- Backgrounding, lock, surface loss and disconnect close the connection. Focus loss and toolbar interaction reset owned input. No foreground/background streaming service.

That background behavior describes display sessions. Camera sessions use a separate user-started camera foreground service; they do not forward input or allocate a monitor.

## Controls

Enter the server IPv4 address and port, choose an optional network route/access
code, then check Display, Camera or Microphone in its collapsible group. Each
checkbox starts/stops only its resource; Disconnect all stops all three. Selectors
refresh when opened and retain a still-present device selection. Missing/invalid
configuration receives temporary red borders and one toast. On successful
connection, a toast names the resource and destination; connected configuration
collapses into a summary with a red disconnect action. Server address fields
collapse while connected, except when validation needs attention.

The display toolbar contains Settings, Hide, Resources and Disconnect. Resources
returns to the configuration page without stopping the display or media sharing;
Show display returns to the streamed screen. Android Back opens Resources during
a display session. Ctrl+Shift+Q disconnects all; F10/F8 reveal controls and pause
input. Settings contains optional reception statistics and negotiated capabilities.
Stats are off by default and are not end-to-end latency measurements.

Physical keyboard uses Linux evdev scan codes mapped to Windows set-1 codes and the host keyboard layout. IME text, virtual keyboard composition, media/system keys and arbitrary vendor keys are not implemented. System navigation and power keys remain Android-owned.

## Build

Pinned tools: Gradle 8.13 (wrapper SHA-256 verified), Android Gradle Plugin 8.13.2, compile/target SDK 36, Build Tools 35.0.0, NDK 27.2.12479018, CMake 3.22.1; JDK 17 or 21. NDK flexible page sizes are enabled. Install those SDK components, set `JAVA_HOME` and `ANDROID_HOME`, then:

```powershell
./scripts/build-android.ps1 -Checks
adb install -r apps/android/app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n io.github.eekebin.extrapane/.MainActivity
```

The build script never installs or launches on a device. Debug APKs use a local development key. Release APKs use a persistent project signing key stored in GitHub encrypted secrets. See packaging below. No secrets or SDK installations are checked into the repo. The generated Gradle wrapper and its verification hash are tracked.

On 2026-11-03 the ARM64/x86_64 build and Android lint passed. The debug APK installed on the user's Pixel 8a (API 37). A real Wi-Fi connection to the Windows server negotiated 1080x1920 and `c2.exynos.h264.decoder`, receiving approximately 59-60 FPS during the observed session. The user confirmed the picture and physical touch work. Backgrounding disconnected while the server remained listening; reconnect restored video. Explicit binding to the phone Wi-Fi interface also streamed successfully. Back reveals the toolbar using the newer Android callback. On-device JNI checks passed for request/offer/frame bounds, two distinct touch IDs, invalid grants/events, capability gating, overflow and reset.

Physical mouse/keyboard, multi-finger application behavior, live host layout renegotiation and a Linux host still need device validation. Physical stylus fidelity remains unverified; synthetic input checks do not establish hardware support. API 37 local-network permission behavior requires reassessment when the target SDK moves to 37. This is a development APK, not a published Android release.

To repeat the device protocol checks after building with `-Checks`:

```sh
adb install -r apps/android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb shell am instrument -w io.github.eekebin.extrapane.test/io.github.eekebin.extrapane.ProtocolChecks
```

These tests validate the real native library without connecting to a host or injecting input. The app must also be installed. The companion test APK can be removed with `adb uninstall io.github.eekebin.extrapane.test` without removing the client.

## References

- [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)
- [Low-latency decoder support](https://source.android.com/docs/core/media/low-latency-media)
- [MotionEvent native input](https://developer.android.com/reference/android/view/MotionEvent)
- [Local network permissions](https://developer.android.com/privacy-and-security/local-network-permission)

## Stylus and hardware keyboards

Android owns USB/Bluetooth device pairing and drivers. ExtraPane forwards the resulting native events while the remote display has focus. Physical scan codes preserve key positions using the host layout; an Android key-code fallback handles drivers that omit scan codes. Unsupported media/vendor keys and IME composition are not forwarded. Disconnecting or changing an input device resets session-owned input so held keys cannot remain stuck.

Compatible active styluses send tip contact, hover, pressure, tilt, primary barrel button and eraser when reported. Pressure is clamped to the protocol range; absent axes stay absent. Android tilt and direction are converted into host X/Y tilt. Direction is not barrel rotation. True barrel rotation and the secondary stylus button are not forwarded. A passive capacitive stylus remains a finger contact. The Settings dialog explains limitations and host grants. Linux hosting currently does not grant pen input.

On-device synthetic tests passed for pressure/tilt, barrel/eraser flags, cancellation, absent axes, host capability rejection, keyboard scan-code fallback, two keyboards holding one key, and reset/orphan-event handling. The Pixel's prior picture/touch test remains the physical evidence; active pen and physical external keyboard testing are still pending.

## APK packaging and updates

The release workflow builds Android alongside Windows and Linux. PRs build/lint debug and release variants without release secrets; trusted release runs sign and verify an ARM64/x86_64 APK named `ExtraPane-<version>-android.apk` and include its SHA-256 in release checksums. Android-only manual workflow runs validate packaging without publishing. Android 11 or newer is required. Allow installation from your downloading app when Android requests it.

The displayed version follows project CalVer. Android's numeric versionCode uses the parent Build and release workflow run number times 100 plus run attempt. Keep this workflow's numbering continuous and attempts below 100; a replacement workflow must begin above the last shipped code. Local development builds default to code 1. Install a newer signed APK over the existing release to retain preferences. A debug-key installation must first be uninstalled to switch to release signing; that removes its preferences. No in-app Android updater is implemented.

Signing secrets are `ANDROID_KEYSTORE_BASE64` (PKCS12, alias `extrapane`) and `ANDROID_KEYSTORE_PASSWORD`. The public certificate SHA-256 is tracked in `packaging/android-signing-certificate.sha256`. Never regenerate the release key for an ordinary update. The initial local key backup is in ignored `out/android-signing`; its password is Windows-user DPAPI protected. Preserve an independent secure backup before deleting local build directories or migrating machines. No private key or password is tracked.

For local release packaging, build `assembleRelease` with `EXTRAPANE_VERSION` and `EXTRAPANE_ANDROID_VERSION_CODE`, then run `python scripts/package_android.py` with those variables plus `ANDROID_HOME`, `JAVA_HOME`, `ANDROID_KEYSTORE` and `ANDROID_KEYSTORE_PASSWORD`. The packager checks signing identity, package/version metadata, minimum SDK, non-debuggable status, native ABIs and 16 KB ZIP alignment. Normal release APKs contain no Android server.

## Android hosting scope

A future Android host could share the device screen or one app through MediaProjection, with Android's consent prompt and foreground-service lifecycle, then encode H.264 using MediaCodec. This is screen sharing, not a portable independent extended Android desktop. Remote control would need separately authorized Android input/accessibility integration; ordinary applications cannot inject arbitrary global touch and pen input. Hosting is not part of the current Android client milestone.

## Access-code connections

Code-enabled builds add a TCP port (default 43824) and optional six-digit access code on the Client page. Use matching releases. Android remains a client only. See [access-code setup and security](ACCESS-CODES.md) for expiry, retry limits and reachable internet endpoints. No-code mode is still restricted to trusted-network use.

## Camera-sharing development slice

The Cameras group lists supported camera choices. Check Camera to send the
selected source to the same unified server address/port used for display and
microphone. Capture stays on Android and requires its permission. A camera
connection does not allocate a monitor; other selected resources may run alongside
it when hardware and permissions allow. Current Android camera capture still
selects one predefined candidate mode per camera (30 FPS at 720p, 480p or 1080p).

Camera permission is requested before starting a non-exported `camera` foreground service from the visible Activity. The service provides a Stop action in its persistent notification and is `START_NOT_STICKY`: Android/process restarts never automatically reopen the camera. Notification permission is requested on Android 13+; if denied, Android's foreground-service task manager and ExtraPane's in-app Stop control remain available. Background operation is subject to Android's permission, privacy and device policies. Physical background/lock behavior has not been verified.

Camera2 sends images directly to a MediaCodec input surface. Native hardware H.264 encoding is preferred when available; MediaCodec software encoding may be selected if necessary. Camera/encoder-supported 1280×720 at 30 FPS is preferred, followed by 640×480 or 1920×1080. The first implementation transmits sensor-native landscape orientation, without automatic rotation or front-camera mirroring. It does not detach hardware; another application's exclusive camera use can make opening fail and is reported.

The camera opens only after TLS/access admission, camera capability exchange and the server's resource grant. The shared C++ serializers produce the EPCM format preamble, EPRS control messages and EPMF media headers. Encoded H.264 buffers stay on worker threads, with a three-frame queue and 2 MiB packet limit; queue overflow closes the connection rather than dropping reference frames. Each connection uses fresh resource/generation identifiers and sequence zero. Codec configuration is attached to key frames, AVCC four-byte NAL lengths are converted to Annex B when encountered, and presentation timestamps use 100 ns units on the wire. TLS admission and writes have absolute deadlines; cancellation closes the socket to interrupt I/O.

Camera, session, codec and surface cleanup belongs to native API workers. A vendor codec that fails to return within the cleanup deadline retains its own buffers until its drain thread exits; the app reports pending cleanup and blocks another encoder start rather than releasing memory still in use. Real vendor-codec cancellation remains a device-test requirement.

On 2026-11-07 the ARM64/x86_64 debug APK, instrumentation APK and lint build passed without installation or device access. Host-side Java checks passed for buffer bounds, AVCC-to-Annex-B normalization and packet size limits. These are compile/synthetic checks only: actual capture, H.264 interoperability, color/orientation, foreground-service backgrounding, revocation, reconnect and Discord/browser camera use remain unverified. The new slice is not a released feature.

Android lifecycle references: [camera foreground-service requirements](https://developer.android.com/develop/background-work/services/fgs/service-types#camera) and [while-in-use permission restrictions](https://developer.android.com/develop/background-work/services/fgs/restrictions-bg-start).

## Microphone-sharing development slice

The Microphones group lists local audio inputs. Check Microphone to send the
selected input; the live per-row Mute checkbox controls its
audio. Display, camera and microphone can run together against a unified server;
individual failures and disconnects release only their own resource.

The visible Activity requests `RECORD_AUDIO` before launching a non-exported
`microphone` foreground service. Its persistent notification stops all microphone
connections; it never restarts capture after process death or reboot. Android 13+
notification permission is requested separately. If notifications are denied,
the in-app controls and Android foreground-service task manager remain available.
These rules follow [Android's microphone foreground-service requirements](https://developer.android.com/develop/background-work/services/fgs/service-types#microphone).

After TLS/access admission and the server's resource grant, `AudioRecord` captures
48 kHz mono PCM16 from the explicitly selected input. It checks the actual routed
device and fails if Android silently switches inputs. A disconnected device,
permission withdrawal, Android-silenced recording, a two-second read stall, or
an eight-packet queue overflow stops the session. Source devices are not detached;
concurrent recording depends on Android and other applications. Bluetooth inputs
are listed when Android exposes them, but automatic Bluetooth communication-route
activation is not implemented and an unavailable route fails explicitly.

The C++ core serializers produce the same `EPMO`/`EPRS`/`EPMD` messages as the
Windows sender. Audio is fixed 480-sample/960-byte little-endian PCM, with a fresh
resource generation and contiguous sequence numbers. This is **not Opus**.
Mute clears queued audio and transmits zeros while retaining capture cadence.
Microphone access/cleanup stays on its worker; a vendor cleanup that does not
return blocks a new capture generation instead of racing a second recorder.
There is no system-audio playback or Android virtual microphone receiver.

ARM64 and x86_64 APK compilation, instrumentation APK compilation and lint passed
without installing or running the app. Actual Android-to-Windows recording,
selected-device routing, lock/background behavior, privacy revocation and
notification actions remain unverified on physical devices. See
[microphone implementation](MICROPHONE.md) for endpoint and native session details.


## Combined-resource admission and remaining parity work

Protected connections share one authenticated control connection per exact server,
port and selected network. The native control codec validates requests/replies;
resource-specific one-use tickets admit the independent display/camera/microphone
streams. Heartbeats retain the control session; its loss cancels its resource
sessions. Releasing the last resource closes the broker. Automatic LAN mode uses
independent encrypted resource connections and does not claim peer verification.
Access codes are held only in memory while related resources are pending/running,
not saved to preferences. Each foreground service keeps its own notification and
requires visible-activity permission before capture starts.

The app now supports repeatable camera and microphone rows, with independent
server addresses, ports and access codes, or one shared destination. The selected
network at the top applies to new capture connections; each active row freezes
its own route and destination. Different physical sources may connect to different
servers concurrently, subject to Android's actual camera/audio hardware support.
One display receiver remains supported. Same-source camera/microphone fanout is
implemented as described below; a shared Qt Android frontend remains unfinished. USB/IP export, Android virtual receiving devices,
display hosting and camera mirroring are not implemented. USB is visible but
unavailable with an explanation; USB/Bluetooth keyboard input is separate from
USB device export. No storage is detached by these Android changes.

2026-11-07 verification: ARM64/x86_64 APK and instrumentation APK compilation and
Android lint pass; pure Java configuration and production admission-pool lifecycle
checks run on the build host. The broker checks use a fake transport with no
Android/JNI/network and cover exact-target reuse, route isolation, last-lease
cleanup, canceled pending acquisition, initial code rejection and established
control loss. Added
custom-mode JNI checks compile but have not run on a device. Combined capture,
control-session reuse, app lifecycle/permission revocation and the new UI still
need authorized device testing. Existing physical display/touch evidence predates
this increment and does not validate these changes.

To repeat the host-only Android checks in this checkout (no device needed):

```powershell
./apps/android/tests/run-host-checks.ps1
```

Android connection review: stopping camera/microphone leaves the checkbox unchecked and temporarily disabled until teardown finishes. Canceled admission requests release queue capacity and authentication errors discard the temporary code cache. After activity recreation, surviving capture sessions retain their own destination summaries; destination fields stay visible for new connections until their complete address/port/interface/network matches a known active target. Host checks cover this target-visibility rule, including unknown restored routes. Isolated APK/instrumentation compilation and lint passed again; no device tests were performed.


## Repeatable capture connections

Camera and microphone groups have Add and Disconnect all controls. Each row has
its own source selector, enable checkbox, compact active summary and disconnect;
microphone rows also have live mute. Disabling **Use one server for all resources**
shows address, port and optional access-code fields on each capture row. The top
address remains the display destination, and its selected network supplies new
capture routes. Source inventories refresh when opened. An unavailable previously
selected device becomes unselected rather than silently choosing a replacement.

One camera foreground service and one microphone foreground service own all their
respective workers. Their notifications stop all connections of that kind; closing
the Activity does not implicitly end foreground capture. No capture starts merely
by binding the service, restoring a row or restarting the process. Runtime camera,
microphone and notification permissions remain separate. Permission prompts freeze
the chosen device and destination, and canceling an attempt invalidates its reply.

Service snapshots restore active rows without retaining access codes. Attempt
identities reject delayed callbacks and old snapshots, including a previous failed
attempt arriving during a new permission prompt. The shared native producer retains source ownership
through worker teardown. A separate per-device microphone lease remains held if
vendor cleanup hangs or throws; other microphone devices can still be attempted.
Android route changes and silenced recording remain explicit failures. Camera
encoder cleanup restrictions also remain in place. No claim is made that every
Android device supports simultaneous capture from multiple physical cameras or
microphones.

Isolated verification: configuration, admission lifecycle and capture ownership/
generation host checks passed. The latter covers stopping one/all rows, ownership
across service replacement, late completion, distinct microphone leases, stale
permission replies and repeated terminal snapshots. ARM64/x86_64 APK compilation,
instrumentation compilation and lint passed. Repeated capture rows, concurrent
hardware access, foreground notifications and lifecycle behavior still require
authorized Android-device testing. No device was connected or installed for these
checks.


## Sharing one source with several servers

Several camera or microphone rows can select the same physical device. Compatible
rows share one native capture worker while keeping separate admission, TLS,
sequence numbers and destination queues. Camera mode must match the existing
producer's resolution and frame rate; an incompatible mode fails explicitly.

Each camera destination has at most three queued encoded frames. New subscribers
wait for a fresh access unit containing SPS, PPS and an IDR frame; old predictive
frames are not replayed. The existing encoder requests a one-second keyframe
interval. A destination that fills its queue disconnects without stopping the
other destinations. Microphone destinations have eight queued 10 ms PCM packets.
Mute applies only to that destination's copy, and changing mute clears its queued
audio. Packets queued while muted retain their muted flag even if unmute occurs
before transmission. Each destination rebases its sample positions to its own
start; dropped backlog and mute transitions set the wire discontinuity flag.
Shared source packets are never modified by a destination.

The final subscriber starts producer cleanup and waits up to six seconds on its
connection worker, keeping foreground-service ownership until cleanup completes
or an explicit pending-cleanup failure is reported. A new connection to that source
waits for cleanup to finish by failing explicitly with a retry message. If native
cleanup reports failure, the source remains unavailable until the app process is
restarted; it is not reopened while ownership is uncertain. Producer failures or
permission/privacy failures end all subscribers of that physical source. A source
that provides no usable media for 15 seconds fails the affected subscription.
Different sources remain independent, subject to Android's hardware restrictions.
The existing camera encoder guard still blocks new camera producers globally if
a vendor codec is stuck finishing cleanup; existing other producers continue.

Host-only fanout checks cover one producer for multiple subscribers, keyframe
joining, incompatible modes, slow-destination isolation, last-subscriber cleanup,
cleanup-failure ownership, per-destination microphone mute and SPS/PPS/IDR detection.
Configuration, admission and capture lifecycle checks also pass. Both APK
architectures, instrumentation compilation and lint pass without device access.
Actual camera/audio fanout, encoding cadence, multiple servers and Android
background/privacy behavior still need authorized physical-device testing.
