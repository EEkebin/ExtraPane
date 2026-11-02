# Project instructions

## Current phase

Implementation is authorized: after the planning and stack selection, the user asked to start. Build the feasibility slices in docs/ROADMAP.md and keep the design documents accurate. Do not claim unfinished media, input, authentication, or driver capabilities. Do not install drivers or change system network/security settings as an incidental build step.

## Sources of truth

Read README.md and docs/DECISIONS.md first, then the documents relevant to the task. Keep requirements, architecture, capability claims, and milestone status consistent. The user delegated stack selection: use C++20, C++/WinRT, WinUI 3/XAML, and native Direct3D rendering/input, with Windows 11 host and Windows receiver first. See docs/STACK.md. Additional receiver platforms are deferred, not excluded. Do not reopen the language decision without a concrete integration issue or user direction.

## Design constraints

- Direct IP over a selected network interface is required, including static IPv4 addresses over Ethernet without DHCP, DNS, internet, or multicast discovery.
- Keep custom LAN mode separate from Miracast/Windows+K mode. Do not imply a custom protocol or mDNS advert appears in Windows Cast.
- Preserve native multi-touch and pen semantics. Mouse emulation is an explicit fallback, never evidence that pen pressure or touch works.
- Negotiate capabilities across hardware, OS capture, transport, host injection, and target application. Unsupported features must be visible.
- System audio playback and reverse microphone forwarding have independent controls and permissions. A mic stream is not a system recording device without an endpoint integration.
- Distinguish existing-display capture from a true extended desktop requiring a virtual display integration.
- Use native media/input paths. Do not move per-frame pixels or high-frequency pen traffic through UI JSON IPC.
- Product LAN mode explicitly accepts clients without pairing or approval, as requested by the user. Keep TLS encryption and scope listeners/firewall rules to intended interfaces. Do not claim peer identity verification in automatic LAN mode. Paired diagnostic CLI mode remains separate.
- On disconnect, focus loss, permission withdrawal, or timeout, stop injection and release session-owned keys, buttons, and contacts. Do not release unrelated local input.
- Do not promise arbitrary vendor pen features, legacy WinTab support, elevated-window control, secure-desktop control, or Windows+K compatibility without specific evidence.

## Implementation workflow after design

Resolve the relevant decision and feasibility gate first. Prefer narrow vertical slices with observable behavior. Test meaningful state transitions and actual delivery to applications, not just successful API return codes. Pin dependencies and document build commands only when the toolchain is selected. Driver signing, packaging, and rollback are release work, not afterthoughts. Do not install drivers, change network addresses, or alter system security settings merely to validate planning documents.
