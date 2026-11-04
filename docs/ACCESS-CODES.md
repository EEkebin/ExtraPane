# Access codes and direct internet connections

This revision adds optional access-code admission. Use matching builds on both endpoints; older releases support trusted-LAN access only.

The client enters a numeric server IPv4 address, TCP port (default 43824), and optional six-digit code. Windows, Linux and macOS hosts expose Require access code and Generate new code. Android is a client only. Port forwarding, firewall rules, public DNS, relays, discovery and UPnP automation are outside this change; an address must already be reachable.

A generated code lasts five minutes, admits one authenticated session, and allows at most five handshake attempts across all host interfaces and client slots. Generate new code revokes the prior pending code without disconnecting already authenticated sessions. Codes are generated with a cryptographic RNG, never persisted or placed in process arguments, and are consumed atomically. Server restarts invalidate them. Established sessions do not expire with their admission code. Failure/expiry requires a newly generated code; no automatic rotation that would allow unlimited guesses.

The existing automatic LAN TLS certificate is not an authenticated server identity. Code mode therefore uses BoringSSL SPAKE2 for password-authenticated key exchange and explicit role-separated key confirmation, followed by a sequence-numbered AES-256-GCM record layer covering all protocol messages, media and input inside TLS. A six-digit value is never used directly as a TLS PSK, transmitted in plaintext, or stored as a verifier enabling offline guessing. BoringSSL is pinned and its symbols are prefixed to coexist with Linux OpenSSL and Android system libraries. This is our versioned application protocol, not a claim of wire compatibility with RFC 9382.

Hosts authenticate before monitor creation, capture permission prompts, media offers, or input grants. Code-enabled clients never downgrade to automatic LAN. Code-enabled servers reject old/no-code clients. Bounded handshake deadlines, record sizes, sequence checks and a shared admission budget limit unauthenticated resource use. A remote attacker can exhaust a code's budget and cause denial of admission; the host must explicitly regenerate it. Knowing the code authorizes one client and does not establish a real-world identity.

Validation checklist (see CI and release notes for completed checks): correct/wrong code, leading zeros, expiry, exhausted budget, replay, concurrent consumption, regeneration during handshake, tampered/reordered/truncated records, old-client rejection, no downgrade, authentication before display allocation, multiple established sessions, disconnect cleanup, and cross-platform wire interoperability. Physical/network testing remains separate from codec/build checks. No driver installation or firewall/network changes are incidental validation steps. Do not reinstall the app on the user's Mac.

## Using a code

1. On the server, select a connected interface (or Automatic where supported), enter a TCP port from 1 to 65535, and enable **Require access code** before starting the server.
2. The server displays a fresh six-digit code. Share it privately with the person connecting.
3. On the client, enter the reachable server IPv4 address, the external port that maps to that server listener, and the code. Select Connect. A valid code is required before a virtual monitor or input session is created.
4. To admit another client or reconnect after disconnecting, choose **Generate new code**. Existing authenticated sessions continue.

The address and port must already be reachable. Configure router forwarding and any external firewall yourself; ExtraPane does not configure NAT, UPnP, DNS or a relay. Windows' app-owned firewall rule stays restricted to the executable, connected local addresses and selected port. With Require access code enabled, it accepts remote source addresses beyond private LAN ranges; without it, the existing LAN ranges remain. Linux and macOS do not create firewall rules. Disabling code mode is suitable only for a trusted LAN, never a public forwarded listener.

Ports and codes are not saved as connection preferences. Code expiry, successful use and attempt-budget exhaustion are shown by the host UI. Restarting the server generates a new code. If the connection fails after admission, that code may already be consumed; generate another rather than retrying it indefinitely. A correct code can still be rejected if another client used it first, it expired, or the host replaced it.

A six-digit code is a short-lived admission secret, not an account, device pairing or permission to bypass operating-system security. Anyone who obtains it can connect once. Internet latency, denial-of-service resistance at the network edge, external firewall configuration and delivery to remote devices are not guaranteed by the authentication protocol.
