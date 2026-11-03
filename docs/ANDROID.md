# Android client

Development is on `feat/android-client`. The first slice is an Android receiver for Windows or Linux hosts using protocol 9. Android hosting, audio, microphone and on-screen keyboard forwarding are not implemented in this slice.

## Architecture

- Android 11/API 30 minimum; ARM64 and x86_64 APK libraries.
- C++20 shared video/input protocol parsers and serializers through a small JNI boundary. Invalid lengths, grants and input events use the existing native validation.
- Android platform Java UI, network/TLS APIs, SurfaceView and MediaCodec. H.264 is decoded directly to a Surface; pixels do not pass through UI JSON or bitmap copies.
- TLS 1.3 over TCP 43824. Automatic LAN mode intentionally encrypts without checking peer identity, matching Windows/Linux. Its socket-local trust policy does not change system trust. No pairing or discovery.
- Numeric IPv4, with automatic routing or a connected Wi-Fi/Ethernet network/address selection. No DHCP or DNS dependency in the protocol.
- Client screen dimensions and refresh capped at 60 FPS are requested. The host chooses a supported mode; arbitrary phone-native virtual monitor sizes are not promised.
- Bounded input and decoder queues, input acknowledgements before decode, and same-session decoder recreation when the host sends a new display offer. A stalled decoder ends the connection rather than accumulating latency.
- Native multi-touch, mouse, physical keyboard and stylus capture. Only host-granted input families are sent. Stylus events are never converted into mouse events.
- Aspect-fit SurfaceView coordinates match input mapping. Native touch pressure/contact area is sent when available; fidelity needs physical application verification.
- Backgrounding, lock, surface loss and disconnect close the connection. Focus loss and toolbar interaction reset owned input. No foreground/background streaming service.

## Controls

Enter the server IPv4 address and select Connect. The top-center handle reveals Settings, Hide and Disconnect. Controls hide automatically, and touch elsewhere goes to the host. Android Back reveals controls during a session. Ctrl+Shift+Q disconnects with a physical keyboard; F10/F8 reveal controls and pause input. Settings contains optional reception statistics and negotiated capabilities. Stats are off by default and are not end-to-end latency measurements.

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
