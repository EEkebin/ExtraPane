# Shared interface and resource sharing

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

- Initial local lifecycle library exists in core/session; no production app uses it yet.
- Device metadata, resource request/status and adapter ownership interfaces exist.
- Independent start/stop, startup failure cleanup and session-close cleanup are implemented.
- Isolated lifecycle, lease, server restart/capacity and capability checks pass
  locally with MSVC C++20. Shared UI/model checks pass offscreen at desktop and
  phone-sized window dimensions using Qt 6.8.3.
- Local device leases, server listener/session lifecycle and direction-aware
  capability intersection are implemented but not yet wired to platform adapters.
- Bounded resource-control envelope serialization exists separately from the
  legacy display protocol. It has not been connected to the live transport.
- Capability/request payload codecs and a bounded control-channel adapter are
  implemented. Negotiation rejects unsupported requests before adapter creation,
  requires capability exchange, and isolates malformed peers. Transport failures
  through serve_one release that session's resources and preserve the listener.
- In-memory integration with the existing access-code encrypted Stream passes
  capability exchange, grant, wrong-code and tamper cleanup checks. No sockets,
  capture devices or drivers are used by those tests. The live display transport
  has not been migrated to this protocol.
- Live transport integration, format negotiation, state notifications,
  mute/interruption behavior and worker scheduling are not implemented here.
- The existing Linux QML shell now lives in apps/shared/ui and is consumed by the
  Linux package through the same resource URL. Video.qml remains platform-specific.
  A shared resource checklist is connected to the Linux controller. The Windows
  Qt shell adapter launches the existing native receiver/helper, preserves access
  code input through pipes and supports tray hosting. It builds locally; native
  connections have not been tested. macOS/Android shell adapters remain pending.
  A staged Windows development package passes an offscreen startup check, includes
  the VC runtime and the offscreen test plugin, and refuses a headless check if
  that plugin is missing. Native host/receiver/helper targets also build after
  extracting the shared process bridge. The helper accepts a selected bind address
  and scopes its firewall rule to that interface; live operation is untested. This
  is not a release package or a completed device-sharing product.
- Camera/microphone adapters, endpoints and USB integration remain unimplemented.

The lifecycle API is not authorization. Callers must authenticate and validate
explicit client selections before constructing adapters. Adapters must be inert
until prepare, release partial preparation on stop, and run on a serialized session
executor. Use one DeviceClaims registry per local device-owning process across
sessions; the local Session alone cannot enforce that rule. Never use untrusted
remote peer IDs as local hardware claim keys.

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
