# Android client

Development is on `feat/android-client`. The first slice is an Android receiver for Windows or Linux hosts using protocol 9. Android hosting, audio, microphone, native pen and on-screen keyboard forwarding are not implemented in this slice.

## Architecture

- Android 11/API 30 minimum; ARM64 and x86_64 APK libraries.
- C++20 shared video/input protocol parsers and serializers through a small JNI boundary. Invalid lengths, grants and input events use the existing native validation.
- Android platform Java UI, network/TLS APIs, SurfaceView and MediaCodec. H.264 is decoded directly to a Surface; pixels do not pass through UI JSON or bitmap copies.
- TLS 1.3 over TCP 43824. Automatic LAN mode intentionally encrypts without checking peer identity, matching Windows/Linux. Its socket-local trust policy does not change system trust. No pairing or discovery.
- Numeric IPv4, with automatic routing or a connected Wi-Fi/Ethernet network/address selection. No DHCP or DNS dependency in the protocol.
- Client screen dimensions and refresh capped at 60 FPS are requested. The host chooses a supported mode; arbitrary phone-native virtual monitor sizes are not promised.
- Bounded input and decoder queues, input acknowledgements before decode, and same-session decoder recreation when the host sends a new display offer. A stalled decoder ends the connection rather than accumulating latency.
- Native multi-touch, mouse and physical keyboard capture. Pen is explicitly ignored rather than silently converted to mouse. Only implemented families are requested; the host grants a subset.
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

The build script never installs or launches on a device. Debug APKs use a local development key; production signing/update distribution is not configured. No secrets or SDK installations are checked into the repo. The generated Gradle wrapper and its verification hash are tracked.

On 2026-11-03 the ARM64/x86_64 build and Android lint passed. The debug APK installed on the user's Pixel 8a (API 37). A real Wi-Fi connection to the Windows server negotiated 1080x1920 and `c2.exynos.h264.decoder`, receiving approximately 59-60 FPS during the observed session. The user confirmed the picture and physical touch work. Backgrounding disconnected while the server remained listening; reconnect restored video. Back reveals the toolbar using the newer Android callback. On-device JNI checks passed for request/offer/frame bounds, two distinct touch IDs, invalid grants/events, capability gating, overflow and reset.

Physical mouse/keyboard, multi-finger application behavior, live host layout renegotiation and a Linux host still need device validation. Physical pen is outside this slice. API 37 local-network permission behavior requires reassessment when the target SDK moves to 37. This is a development APK, not a published Android release.

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
