# Network and direct-IP design

## Implemented setup (0.11)

Client enters the server IPv4 address only. Both apps display all local IPv4/IPv6 addresses with adapter names; IPv6 transport is not implemented. Server listens on each active IPv4 address, and the client uses Windows route selection to choose its source address. TCP 43824 carries paired TLS media/control/input. No discovery, DHCP, DNS, STUN, TURN or internet service is required. Explicit CLI `--bind` remains available for interface-specific diagnostics; the UI uses `--bind auto`. Restart Driver after local address/interface changes. See DESKTOP-APPS.md and SECURITY.md for lifecycle and firewall scope.

The earlier transport/interface proposals below are design background, not the current UI or transport contract. Automatic discovery is deferred by user choice.

## Earlier proposed connection modes

| Mode | Setup | Required services |
| --- | --- | --- |
| LAN discovery | Choose a visible peer on the same reachable local network | Optional mDNS discovery plus authenticated session setup |
| Manual IP | Enter a peer address and configurable control port | IP reachability; no discovery, DHCP, DNS, or internet |
| Direct Ethernet | Configure compatible static addresses, select Ethernet, connect by IP | Ethernet link and compatible routes/subnets |
| Routed LAN/VLAN | Enter reachable peer IP; allow selected session ports | Route and firewall permission; multicast not required |

Example direct cable: host `192.168.50.1/24`, receiver `192.168.50.2/24`. Neither endpoint needs a gateway or DNS for this link. Check for subnet collisions with other adapters. Arbitrary static IPs are not automatically mutually reachable: compatible addressing or an explicit route is necessary. The application does not assign or change system addresses by default.

## Listener and connection model

Proposed custom mode: host listens for authenticated control connections; receiver connects, then media and reverse input/microphone channels are negotiated. Both peers can advertise their roles, but advertising is optional. A future reverse-connect workflow may invert initiation without changing host/receiver semantics.

Setup exposes local interface name, interface identity, address, port, and allowed peer scope. Bind to the selected local address by default, with wildcard listening as an explicit option. Selecting an interface must constrain both control and media, including ICE candidates and actual route selection. Do not quietly use a VPN or another adapter when the selected interface goes down. Report failure and let the user choose a new path.

Use IP literals without a hostname lookup. Support IPv4 initially and plan IPv6 literals, including scoped link-local addresses and scope identifiers, as an explicit tested capability. IPv4 automatic link-local is best effort, not a replacement for manual static-IP support. No hardcoded broadcast subnet or assumption that the default route is correct.

## Transport proposal

Prefer WebRTC media plus data channels for custom mode, subject to the native integration spike. Use an authenticated encrypted local signaling channel. A direct-IP connection must exchange local candidates and work without external STUN/TURN or signaling servers. Restrict candidate publication and connectivity checks to approved interfaces. If the selected implementation cannot guarantee interface constraints or offline operation, resolve that before adopting it.

Define a configurable control port and bounded configurable UDP media port range when implementing the transport. The exact defaults remain undecided. Direct-IP control success alone is not a completed connection: media connectivity and reverse-channel readiness must be verified. Diagnostics identify control, media, and firewall failures separately.

## Security and resilience

An IP address is a locator, not an identity. Manual-IP sessions require the same pairing and peer authentication as discovered sessions. Use established cryptography and bind signaling identities to negotiated media fingerprints. Never trust discovery metadata as authorization. Rate-limit pairing attempts; keep persistent secrets in the OS credential store.

Firewall changes must be narrowly scoped to selected application endpoints, intended profiles, and peer/interface ranges where supported. Do not disable the firewall. Treat media, input, and mic as separate permissions. Redact keys, pairing codes, typed text, and media from default logs.

Adapter removal, address changes, sleep, cable loss, and route changes terminate or suspend the session with input cleanup. Reconnect requires identity verification and fresh media/input epochs. Network diagnostics show local/remote addresses, selected interface, negotiated ports, round-trip time, loss, jitter, throughput, and the actual selected media candidate pair.

## Windows+K boundary

These custom IP modes do not advertise a Miracast sink. Miracast over Infrastructure is a separate protocol and its documented setup starts with Wi-Fi Direct discovery. A working Ethernet-only custom session does not establish Windows+K compatibility. See DECISIONS.md for primary sources.
