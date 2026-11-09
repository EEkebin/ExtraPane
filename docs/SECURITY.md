# Connection security

## Devices workspace (development branch)

The current workspace defaults to pairing. Generated six-digit codes rotate every
20 seconds; an optional fixed custom PIN/password replaces automatic generation.
Custom secrets accept valid UTF-8 up to 1024 bytes and use the same private storage
protections as remembered trust (Windows DPAPI; user-private files on POSIX).
They are not advertised or returned to the UI after saving. Five failed new-pair
attempts are allowed per minute across identities; pending handshakes count against
the budget. Rotation does not revoke already-paired keys or active sessions.
A password-authenticated exchange bound to the TLS channel establishes
a random remembered key. Later sessions prove that key and pin the peer identity.
Discovery supplies route candidates, not trusted identities. Pairing grants access
to advertised resources; capture starts only on a resource request. Optional
unpaired mode remains explicitly unverified.

Forget removes ordinary trust and access immediately. A durable revocation outbox
asks the other device to remove the same pairing. Its separate authentication mode
accepts only Forget and acknowledgement, never catalogs, resource requests or media.
The retained key is bound to that old pairing; delayed requests cannot delete a
new pairing. Remote removal opens a separate authenticated connection. Direct
removal needs a reachable listener; the new endpoint tunnel can carry the dedicated
revocation-only exchange through an available paired gateway. It cannot restore
ordinary access while removal is pending. A missing route or one-way port forward
without a usable gateway still prevents remote acknowledgement.
Offline/unsupported/unreachable peers remain pending rather than falsely reported
as removed. A bounded receipt cache permits acknowledgement retries after a restart.

The version-2 private trust store preserves version-1 pairs when migrated. Windows
uses user-scoped DPAPI encryption; POSIX uses private directory/file permissions,
not keychain encryption. Older app builds cannot read the upgraded store. Normal
Update preserves trust; uninstall or clearing app data does not.

The sections below describe the older Server/Client and diagnostic workflows.
Their code lifetimes and credential storage are distinct from Devices pairing.

## End-to-end encryption audit (2026-11-08)

Direct paired Devices connections authenticate the two TLS endpoints with a
TLS-exporter-bound SPAKE2 exchange. Resource control and tunneled media then travel
inside TLS 1.3. Open/unpaired mode encrypts traffic but does not authenticate the
other endpoint, so it does not protect against active impersonation.

Gateway mode through pre.109.1 must not be described as gateway-blind end-to-end encryption.
Although native media normally retains its own encrypted stream, the gateway can
read the native admission credential carried in Open/Granted RPC messages. That
secret allows an actively malicious gateway to impersonate the media endpoints.
See `core/peers/service.cpp` relay handling, `core/peers/rpc.cpp` credential encoding
and `core/access/access.cpp` native credential authentication.

The replacement in this branch authenticates the two end devices through an opaque
tunnel. It uses a separate inner TLS 1.3 connection and the existing exporter-bound
pairing/remembered-key exchange. Resource setup, native credentials, media and input
remain inside that authenticated endpoint connection. The gateway handles route
identities, tunnel IDs and encrypted bytes, not the endpoint admission secrets.
The peers negotiate tunnel support; the old credential-forwarding path is removed,
and unsupported peers receive an error rather than an insecure fallback.

Pair the end devices once using the code on the actual other device, not the
gateway's code. Both devices can offer and use resources. Reverse resource access
uses a separately authenticated tunnel with the same mutual pairing; neither end
needs a directly reachable reverse address. Pairing and catalog refresh do not
start capture. A routed pairing never saves the gateway or an internal loopback
address as the end device's direct address.

Synthetic checks cover independent endpoint pairing, wrong-code rejection without
media activation, reverse catalog access, data delivery, gateway shutdown and
bilateral Forget through a live gateway. Service, presence, asynchronous startup,
reconnect and self-connection regression checks also pass. Windows and Android
pre.110.1 packages passed isolated build and package checks. Physical multi-device gateway verification and independent
cryptographic review remain pending. This change is not in pre.109.1.

Endpoint confidentiality concerns the relayed session. A gateway that is separately
paired with a device may still request that device's advertised resources in its
own authorized session. Encryption does not remove permissions already granted to
the gateway, protect a compromised endpoint, or conceal routing metadata.

Discovery still broadcasts basic device metadata; IP addresses and traffic timing
are not concealed by media encryption. Raw USB/IP is local loopback traffic wrapped
by the network TLS tunnel. Android has no USB-sharing implementation. This audit
is source review, not independent cryptographic certification.

## Optional access-code mode

The access-code build supports short-lived, one-use six-digit admission codes on Windows, Linux and macOS hosts and all four clients. A password-authenticated exchange verifies code possession and derives session-specific encryption keys before monitor creation or input grants. See [setup, protocol and limits](ACCESS-CODES.md). This is separate from the legacy diagnostic PSK mode below. Do not substitute a numeric code directly for a TLS PSK.

## Legacy Server/Client automatic LAN mode (no code)

The product app uses `dm-view --lan`: TLS 1.3 with a fresh ECDSA server certificate and ephemeral key exchange, with no pairing, password or approval prompt. The client does not verify server identity and the server does not authenticate clients. Encryption protects against passive observation, but this mode does not resist an active intermediary. While Start Driver is running, any client that can reach the scoped listener can receive its own desktop monitor and use input when the Server input checkbox is enabled. Use this on a trusted LAN. No application-wide static key is embedded.

The listener has four bounded session slots; TLS/request deadlines release stalled slots. Each valid client receives an independent monitor and media pipeline. Invalid requests and disconnects close only their own slot. Stop Driver ends all sessions. Input still has bounds, epochs, permission negotiation and a per-session crash watchdog. A shared gesture lease prevents remote clients from releasing one another's held input.

The existing executable/interface/port firewall scope and helper identity checks remain in effect. No new system signing or driver policies are changed. Legacy saved credentials are unused by the app and can be removed by the uninstaller's settings cleanup.

## Legacy paired diagnostic CLI

The following sections document the optional `dm-session` and `dm-view --name` diagnostic mode, not the product connection workflow.

### Paired TLS prototype

`dm-session` implements a one-shot authenticated diagnostic over interface-bound TCP. `dm-view` uses the same TLS wrapper for bounded video with separately granted native input. Microphone functionality is absent. See INPUT.md for the current input permission and watchdog boundaries.

## Trust model

Each pair has a name and a cryptographically random 32-byte shared key. Provision the same name/key at both endpoints through an already authenticated channel; laptop automation currently uses the accepted SSH host key and the user's supplied SSH login. This is developer provisioning, not the final user-facing pairing flow. Human passwords or short numeric codes are not suitable replacements for the random key.

Pairing keys are stored under the current Windows user's `DM/peers/<name>` generic credential in Credential Manager. They never appear in process arguments or saved test reports. Existing entries cannot be silently overwritten; explicitly revoke before replacing. `peers` lists names only.

TLS uses the hash-pinned official Mbed TLS 4.1.1 release, including TF-PSA-Crypto 1.1.1. Configuration requires TLS 1.3 and PSK with ephemeral key exchange. It does not permit a certificate fallback or TLS 1.2. Session tickets are disabled and early data is not enabled. Windows supplies platform entropy through the dependency's supported platform backend. All socket callbacks catch exceptions at the C boundary.

Pairing trust is distinct from permissions: `dm-session` grants none. `dm-view host` requires explicit `--allow-screen`. Input additionally requires host `--allow-input`, a selected target monitor, and receiver `--control`; the host grants only intersecting input families and a fresh session epoch. Receiver input requests without the host grant are rejected. Audio/mic grants are not implemented. No plaintext media mode is implemented.

## Commands

```powershell
.\out\build\windows-release\dm-session.exe peers
.\out\build\windows-release\dm-session.exe pair --name PAIR_NAME --secret-stdin
.\out\build\windows-release\dm-session.exe revoke --name PAIR_NAME
```

The pair command reads one bounded line of exactly 64 hexadecimal characters from stdin. Generate keys with an OS cryptographic RNG and deliver them over a trusted channel; do not put keys in shell arguments/history. The automated test runners handle provisioning without printing the key.

With an existing pairing, on one computer:

```powershell
.\dm-session.exe listen --name PAIR_NAME --bind LOCAL_IP --timeout-ms 60000
```

On the other:

```powershell
.\dm-session.exe connect --name PAIR_NAME --bind LOCAL_IP --peer LISTENER_IP
```

Default secure diagnostic port is TCP 43823. Expect `SESSION_OK TLSv1.3 ... permissions=none`. It is an authenticated diagnostic, not a persistent control session.

## Evidence from 2026-11-01

- Debug/Release integration tests pass for successful TLS, invalid/duplicate key provisioning, plaintext rejection, stalled peers, revocation, and bounds. Python 3.14/OpenSSL additionally verified same-identity/wrong-key rejection, wrong-identity rejection, TLS 1.2 rejection, and malformed application payload rejection inside a valid TLS session.
- On the laptop, TLS sessions pass in both directions. Same identity with different keys fails at handshake; revoked pairing fails before a listener opens.
- Temporary test credentials are revoked at both ends after checks. Temporary program/address/peer/port-scoped firewall rules are removed. Development SSH access is separate.

The test harness found Windows error 1312 when Credential Manager was accessed from the SSH logon context. Secure test processes now run as temporary tasks in the already signed-in user's desktop session. Pairing data travels in memory through an owner-only local named pipe. Uploaded helper scripts contain no keys and are removed after the task; the scheduled task is also removed. This requires a signed-in laptop user and administrative setup rights in the SSH test runner; the app itself runs with the interactive user's normal rights.

## Remaining lifecycle work

Diagnostic processes can be bounded; the normal server accepts sequential sessions continuously. Revocation prevents new sessions; live-session revocation/permission withdrawal, explicit device identity management, friendly verified pairing UX, reconnect epochs, rate limiting, and product privilege separation still need implementation. The current input prototype has explicit grants, bounded/epoch-checked reverse batches, and a separate watchdog child for crash cleanup; see INPUT.md. The current prototype must not be described as a complete remote-control authentication system.

Dependency review and update cadence remain release responsibilities. The chosen 4.1 branch is maintained according to [upstream branch policy](https://github.com/Mbed-TLS/mbedtls/blob/development/BRANCHES.md). The source archive URL and SHA256 are recorded in `cmake/Dependencies.cmake`; dependency license material is documented in `third_party/README.md`.

## Desktop app lifecycle

The 0.11 launchers use a persistent `dm-lan` pairing in Windows Credential Manager. LAN addresses and display settings are stored separately under HKCU Software/DM; pairing keys are never settings values. Start Driver grants an inbound rule for the engine executable, active non-loopback local IPv4 addresses and TCP 43824 on all profiles. Remote scope is LocalSubnet plus 10/8, 172.16/12, 192.168/16, 169.254/16 and 100.64/10. This accommodates routed LANs without configuring a client address; every connection must still authenticate before capture or input. Public remote ranges outside LocalSubnet need an independently configured rule.

Stop Driver or tray Exit removes the owned rule and virtual monitor. Closing the setup window only hides it. Client disconnects leave the driver listening. A crashed launcher stops its engine via a parent-process handle; the input watchdog remains separate to release contacts. A stale firewall rule after a crash retains its scope and is replaced on the next Start. An existing rule for a different engine path is not overwritten. Continuous streaming does not remove authentication, message bounds, failure deadlines or input cleanup. Restart the driver to refresh listeners and firewall scope after local address changes.

## Unified app privilege boundary (0.13)

ExtraPane.exe is asInvoker. Server startup elevates ExtraPane.Service.exe with fixed options; that helper manages the firewall and launches the fixed sibling media engine. The randomized named pipe is restricted to the launching user and SYSTEM, rejects remote clients, and both endpoints verify peer process IDs. The helper additionally checks the launcher executable path. The only inbound control is stop; media/input never flow through this IPC. The helper monitors launcher death and removes its owned firewall rule after stopping the engine. Pairing remains in the interactive user's credential store; elevation must use that same Windows account. Apps remain unsigned development builds.

## Development driver installation

The complete Windows setup includes a machine-wide virtual-display driver. Setup explicitly asks to trust its development code-signing certificate in machine Root and TrustedPublisher. The app itself remains per-user; there is no limited-role installer choice. The build discards its ephemeral private signing key and ships only the public certificate. This is not Microsoft release signing.

The manager and records live under Program Files. Package hashes, allowed filenames, certificate identity and catalog signer are verified before trust changes; Windows SetupAPI validates catalog membership when staging. The unified ExtraPane uninstaller removes its managed driver components after verifying ownership, preserving unrelated third-party components. No boot security policy is changed and staging is not proof of runtime loading under every Windows policy. See DRIVER-INSTALLATION.md for consent, rollback and logs.
