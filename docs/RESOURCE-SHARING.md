# Shared interface and resource sharing

Track completed increments and remaining work in
[IMPLEMENTATION-CHECKLIST.md](IMPLEMENTATION-CHECKLIST.md).

Approved direction: retain ExtraPane and C++20 native engines; migrate the four
interfaces to Qt Quick/QML. This supersedes the Windows-only UI selection in
STACK.md. Existing platform apps remain the production path until migration
passes display regression checks. iOS, renaming and speaker forwarding are deferred.

## Connection and lifecycle

The client connects to a server and selects Display, Camera, Microphone and/or
individual USB devices. Display streams from server to client; other resources
travel from client to server. A media-only connection must not allocate a monitor.
One camera and one microphone per connection initially; one remote receiver per
physical resource. Desktop hosting and an outbound connection can coexist.

One installation and one Start server / Stop server control manage platform
components. Starting listens without activating resources. Disconnect cleans up
only that client; stopping the server closes all its sessions. Installed drivers
remain installed. Session-owned input, contacts, streams and USB claims must be
released, including on failures. Shared helpers must not stop another session's work.

Qt handles presentation and bounded state updates. Native capture, codecs, audio,
input and GPU surfaces remain outside UI messaging. Preserve current renderer
selection, native input, fullscreen, interface selection and display negotiation.
Use matching protocol generations with explicit rejection of old peers, preserving
the existing access-code security boundary before resource allocation. Separate
bounded channels must be bound to the admitted session and use the configured port.

## Implementation status

This development branch now contains the first Android-to-Windows camera-only
test slice. It is not a published release or a completed cross-platform
device-sharing product. Follow [CAMERA-TEST.md](CAMERA-TEST.md) for the first
authorized device test; the checks below do not establish physical interoperability.

### Resource connections

- The core/session lifecycle library handles resource ownership, capability
  intersection, independent cleanup, startup failures and admitted-session limits.
  Isolated lifecycle, lease, restart and malformed-message checks pass.
- The native Windows camera helper uses the existing TLS/access-code admission
  before the bounded camera offer, capability exchange and request/grant. The
  offer selects geometry and a connection-scoped resource/generation binding.
  No monitor is allocated for a camera connection. Up to four receiving workers
  have independent local camera slots; one client's failure does not stop the
  listener.
- Camera frames use bounded H.264 framing, contiguous sequence numbers and
  capture timestamps. Broken transport, stale bindings, sequence gaps, format
  changes and decoder failures release that camera resource. The first frame
  allows camera startup time; socket shutdown interrupts waits when the app stops
  or its launcher exits. Workers own their native resources until cleanup finishes.
- A Windows-session-local listener lease prevents duplicate camera helpers; a
  hashed source-device lease prevents another helper in that Windows session from
  sharing the same physical source. Cross-Windows-session coordination is still
  unfinished. Remote device strings are never interpreted as local hardware paths.
- The initial live flow is camera-only. Display mode retains the legacy engine;
  display and camera listeners cannot occupy the same address/port simultaneously.
  Multiplexing display, camera, microphone and USB in one connection remains
  unfinished. See [CAMERA-SESSION-PROTOCOL.md](CAMERA-SESSION-PROTOCOL.md).
- Protocol checks cover bounded offers and version rejection. In-memory encrypted
  checks cover admission, capability/grant exchange, wrong codes and tampering.
  Live socket cancellation and network/device interoperability remain unverified.

### Windows camera path and setup

- Native enumeration, asynchronous NV12 capture and the hardware H.264 source
  adapter compile. Encoded frames use a three-frame queue; overflow ends the
  resource instead of dropping encoded reference frames. The Windows sender is
  available through the native helper; camera-source selection is now
  integrated into the shared Windows Client page. Refresh is explicit and asynchronous; startup and isolated UI checks do not enumerate devices.
- The receiving resource connects framed messages to the existing hardware
  decoder, packs negotiated NV12 pixels without a BGRA conversion, and forwards
  them over the native pipe. Its worker owns decoder, endpoint and COM lifetimes.
  Isolated tests substitute the decoder and registration backend; they validate
  actual serialization, pipes, MF sample consumption, stride handling and cleanup.
- ExtraPane.CameraSource.dll implements the COM factory, activation object and
  receiving MF source. Consumers receive native sample events and request tokens.
  The source retains one pending raw image and at most four sample requests.
  Stop/restart reconnects its pipe; transport failure reports a native media error.
  Unregistered DLL checks cover delivery, detach/shutdown and unload eligibility.
- The local pipe rejects remote clients and restricts access to the current user,
  Local Service and System, checking the connected reader's identity before sending
  pixels. Tests cover the current-user path; actual Frame Server service-account
  activation still requires validation.
- The registration backend uses session-lifetime/current-user virtual cameras,
  starts only after a valid request and removes partial startup on failure.
  Registering its COM component alone does not create a camera.
- The setup helper embeds the matching DLL, installs it under a protected
  machine location, checks registration ownership, and implements update rollback
  and owned removal. Installer integration adds a camera-host role without the
  virtual-display driver and an explicit machine-component removal option.
  The shared Windows UI exposes Camera setup. See
  [CAMERA-SETUP.md](CAMERA-SETUP.md) for paths, ownership and rollback limitations.
  Isolated setup policy/payload checks are distinct from elevated installation,
  upgrade/removal and actual Frame Server validation, which have not been run.
- No physical Windows camera or GPU codec has been exercised for this work.
  Real capture, permissions, color, latency, hot unplug and application delivery
  remain unverified. Initial Windows camera encoding includes a CPU NV12 copy;
  it is not a zero-copy claim.

### Android camera source

- The existing Android app now contains camera selection and permission handling,
  Camera2 capture into MediaCodec H.264, bounded packets and the shared camera
  protocol. It opens the selected camera only after the server grants the request.
- A user-started camera foreground service provides ongoing status and a Stop
  notification. It uses the selected route/address, port and optional access code.
  Camera sharing does not deliberately detach the physical camera from Android;
  OS policy or another application's camera ownership can still prevent capture.
- The Android development APK builds. This remains the native Android interface,
  not the completed shared Qt UI.
  Actual camera permission prompts, foreground/background restrictions, encoder
  compatibility and Android-to-Windows picture/reconnect behavior require a
  physical-device test. Built-in microphone forwarding is not included.

### Shared UI and packaging limits

- The Linux QML shell is shared through apps/shared/ui, with native video remaining
  platform-specific. The Windows Qt adapter retains the existing display engine,
  secure code input, bind selection and tray hosting.
- Windows Server mode now selects Display or Camera (preview). Camera mode uses
  the internal resource helper and displays installation, listener, connection and
  per-client failure status. Camera-only hosting does not start the display driver.
  Installed camera-component presence is checked before hosting. General multi-resource UI and macOS/Android Qt
  migration remain unfinished.
- Development staging includes the shared app/runtime and can include resource
  and camera-setup helpers. Offscreen UI checks do not launch a connection or
  install anything. Compiling an installer does not verify installed behavior.
  Windows installer compilation is currently blocked; the development portable
  package and its embedded camera-setup helper are the initial test path.
  The camera helper does not automatically change firewall rules; an existing
  display-engine rule may not cover the new executable.
- Windows microphone source, PCM session, local UAC2 endpoint helper and source/mute UI are implemented in development; see MICROPHONE.md. Native and isolated checks do not prove driver installation or application recording. Android microphone work is in progress.
- Linux/macOS camera endpoints and sources, other microphone platform adapters and
  USB integration remain unimplemented. Windows audio-driver and macOS extension
  signing/distribution gates remain open.

The lifecycle API is not an authentication boundary. Product automatic LAN mode
accepts reachable peers without pairing/approval, as requested; optional access
codes add the existing admission step. Always complete the configured admission
and validate explicit selections before allocation. Adapters remain inert until
prepare and must release partial setup on stop. Local process/session leases are
not evidence of machine-wide ownership across all login sessions.

## Platform and distribution decisions

Camera: native capture on all four platforms, H.264 transport; Windows Media
Foundation virtual camera, Linux V4L2 loopback, macOS Core Media I/O extension.
Microphone: native capture, Opus at 48 kHz mono initially; Windows audio endpoint
based on Microsoft's reference driver architecture, Linux PipeWire source, macOS
Core Audio plug-in. No deliberate source-device detachment. OS contention must be
reported. Android uses a foreground service and explicit Stop notification.

USB first targets removable storage on Windows/Linux using usbipd-win,
usbip-win2 and Linux USB/IP, carried through ExtraPane's encrypted admission path.
No raw public USB/IP listener. Require source unmount and normal destination eject;
exclude system disks, hubs, network/input devices and uncertain ownership. Never
force a busy detach or automatically reattach storage after connection loss.
macOS/Android raw USB is unavailable in this milestone.

All required components belong to one managed setup/update/uninstall flow.
Windows uses the existing installer; macOS adds a Universal installer; Linux keeps
the AppImage with integrated system-component setup; Android keeps its signed APK.
Record versions, ownership, notices, rollback and installation state. Do not remove
independently installed dependencies. Portable apps cannot make drivers portable.

Windows microphone signing and macOS extension signing/notarization are unresolved
distribution prerequisites. No claim of release readiness, automatic test-signing,
security-policy changes, or unlicensed third-party redistribution is permitted.

## Checkpoints

1. Foundation: lifecycle, server-wide ownership, capability contracts and component manifests.
2. Shared Qt shell with existing display parity on all four platforms.
3. Authenticated resource negotiation and independently bounded channels.
4. Camera delivery into ordinary applications, Android-to-Windows first.
5. Microphone delivery into recording applications and Android background lifecycle.
6. Windows/Linux USB storage, safe ownership and integrated setup.
7. Four-platform upgrade/uninstall and release validation.

Pause after each user-visible feature for verification. Ask before disruptive
device tests; do not reinstall on the user's Mac without renewed authorization.
Test app delivery, permission withdrawal, failed preparation, reconnect, hot unplug,
independent resource cleanup, mixed traffic latency, security and packaged upgrades.

Isolated checks (do not touch devices or run the live server):

```text
cmake -S core/session -B out/session-checks -DEXTRAPANE_SESSION_TESTS=ON
cmake --build out/session-checks --config Release
ctest --test-dir out/session-checks -C Release --output-on-failure
```

Shared Windows shell (Qt 6.8.3 MSVC desktop SDK, MSVC developer shell):

```text
cmake -S apps/shared -B out/shared-ui -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt SDK>
cmake --build out/shared-ui --parallel 2
ctest --test-dir out/shared-ui --output-on-failure
```

Qt's bin directory must be on PATH for the isolated UI check. The packaging script
scripts/package-shared-windows.ps1 stages native engine/helper outputs alongside
the Qt executable and runtime. This development staging command does not install
the driver, launch the app or publish a release. The staged executable's
`--check-ui` option loads its actual controller and QML offscreen without starting
the server or receiver. It checks startup, not actual device delivery.

## Current Windows resource build (2026-11-07)

The Windows server starts once and accepts display, camera, microphone and USB
connections on one selected address/port. Start server checks the display driver
package and prepares missing or outdated camera and microphone/USB receiving
components, with administrator permission when needed. Setup failures prevent
listening and show the reason. Runtime failures affect only the requested resource. Existing clients select their resource using the current
protocol; no extra public ports or server mode selection are required. The Windows client uses a multi-select resource checklist with independent device
choices and status. One Connect action starts the selected resources, including
receiving a display while sending a camera and microphone. Separate protected
resource streams coexist; they are not multiplexed over one TCP stream.
Windows camera sharing selects a supported native mode from the client camera
before offering its dimensions and frame rate to the server. The current protocol
supports even dimensions up to 3840 x 2160 and whole-number rates up to 60 FPS;
fractional-only modes are reported as unsupported.
Windows microphone sharing accepts 48 kHz mono/stereo WASAPI sources and sends
mono PCM16 over the encrypted connection; Opus and sample-rate conversion remain
future work. Camera and microphone capture do not detach the physical source,
although applications and drivers may impose their own simultaneous-use limits.

Windows USB sharing is restricted to eligible, uniquely identified removable
storage. The source requires the managed USB host component; the recipient uses
the managed USB/IP client component also used by the microphone endpoint. These
are internal components of the same app package. USB controls request explicit
administrator permission; ordinary camera/microphone capture does not.
Use **Eject USB** on the recipient, stop the source session, then use **Refresh
recovery ? Restore selected disk** on the source. The restore helper verifies the
recorded disk, binding release and durable eject evidence. Busy eject keeps the
transfer running. Interrupted transfers remain offline and may require manual
administrator investigation; there is no force-online button. See [USB](USB.md).

The isolated checks validate compilation, bounded protocols, synthetic lifecycle
and offscreen startup. They do not prove real camera delivery, audio recording,
USB device import or driver installation. Physical testing is deferred until this
implementation milestone is complete. Linux/macOS resource adapters and Android
USB remain outside this Windows-to-Windows milestone.

## Unified Windows server

The product `dm-view host --serve` path accepts TLS and optional access-code
admission once per incoming connection, then examines eight decrypted bytes to
select the existing display, camera, microphone or USB adapter. A bounded prefix
buffer preserves those bytes for the adapter's full protocol validation. Unknown
protocols, component errors and client disconnects end that connection; the
listener remains available. Limits are four display sessions, four cameras, one
microphone and one USB session, with a bounded pending-connection pool.

Starting the server does not allocate a virtual monitor, camera or microphone.
Adapters prepare those resources after the corresponding client request. The
server page shows component availability without manual camera/receiving setup
buttons or a resource selector. Normal Stop requests safe USB eject before shutting down; a busy eject
keeps the server running. Process termination still follows the documented USB
quarantine/recovery rules. Optional six-digit codes remain single-use. For a multi-resource selection, a
short admission connection exchanges that code for one ticket per selected
resource. Tickets expire after 60 seconds, can be used once, and are bound to the
resource and server code generation. Regenerating the code revokes unused tickets.
Credentials pass through private child-process stdin/stdout rather than command
arguments or saved preferences. Canceling admission launches no resources.

The client retains the checkbox list while connected. Camera and microphone have
separate source pickers; no source is selected automatically. Each resource has
its own status and Stop button, microphone has Mute, and Disconnect all stops the
selected resource processes. A missing/failed resource does not stop the other
resources. Selection and source changes are locked while connected; disconnect
before choosing a different combination. USB retains its explicit server-side
eject and source recovery requirements. These changes have passed isolated UI,
protocol and authorization checks; simultaneous physical delivery is unverified.

## Consolidated Windows installation

The development installer now uses `C:\Program Files\ExtraPane` for the app
and `Components\Display`, `Components\Camera`, `Components\Microphone`, and
`Components\UsbHost` beneath that directory. The app has a machine-wide Windows
Apps entry. Its internal display manager retains its stable upgrade identity but
is hidden from Windows Apps; the main uninstaller invokes it for owned driver removal.
Per-user settings remain in the Windows user profile.

Display migration copies verified legacy ownership records before changing the
manager location. The old `SideScreen Driver` folder currently remains as a rollback
copy: automatic approval review blocked implementation of its retirement. Do not
manually run that stale uninstaller or delete its records during migration testing.
Native camera, microphone and USB helper migrations validate owned paths and files.
Full upgrade/uninstall testing is still pending. Independently installed USB/IP
backends remain owned by their original installers and are not relocated.

USB source setup can reuse the reviewed, Oracle-signed 7.2.2 monitor already shipped
with usbipd-win. Its running protocol must report the supported 5.x interface;
unknown binaries remain rejected. A read-only protocol query passed on the development
desktop. This does not verify USB storage transfer, eject or recovery.

The Windows client camera card includes Mirror Camera. It can be changed before
connecting or while sharing; the sender mirrors native NV12 pixels before encoding,
so receiving camera applications see the change. The setting defaults off and is
kept only for the running app session. Camera, microphone and USB device dropdowns
load inventory on opening; source component installation remains in the installer.
