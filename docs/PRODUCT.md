# Product requirements

## Purpose and terminology

Make a local network display useful for desktop work and drawing, with low delay, reliable input, and clear connection controls.

The **host/source** is the Windows computer whose desktop is displayed and controlled. The **receiver** shows the video, plays host audio, captures local input, and can forward its microphone to the host. A device may eventually offer either role; simultaneous roles are not an initial requirement.

## Required product scope

| Area | Intended behavior | Important boundary |
| --- | --- | --- |
| Connection | Wi-Fi LAN, switched Ethernet, direct Ethernet, manual IP/port, selected adapter, static addresses | Reachable IP path required; DHCP and internet optional |
| Display | Mirror a selected monitor; genuine extended desktop | Extend needs a virtual monitor integration; window sharing optional later |
| Touch | Native contacts, tap, press, drag, multiple fingers, gesture delivery | Native touch and touch-as-mouse are distinct selectable modes |
| Mouse | Absolute/relative motion where supported, left/right/middle/extra buttons, wheel and drag | Pointer capture and shortcut escape must remain accessible |
| Keyboard | Key down/up, modifiers, shortcuts, repeats, text entry | Separate physical-key and text/IME paths; OS-reserved shortcuts need explicit handling |
| Pen | Contact, hover, pressure, tilt, rotation, exposed buttons, inversion and eraser | Support depends on each stage; optional vendor features need adapters |
| Host audio | Forward system playback to receiver, receiver volume and mute | Local host playback policy is a separate choice |
| Receiver microphone | Opt-in reverse stream usable as a Windows recording endpoint | Endpoint installation/integration is a distinct deliverable |
| Control | Per-session permissions, mute indicators, stop control at both ends, revoke pairing | No unattended control by default |
| Windows+K | Investigate appearing as a Miracast display | Conditional compatibility track, not a guaranteed LAN feature |

Interpret the original request's “mutable audio” as **muteable audio**, with independent speaker and mic controls. Configurable routing is also planned; confirm if another meaning was intended.

## Session experience

Use one host and one receiver per session initially. Connection setup presents device identity, connection mode, network adapter, display mode, resolution/refresh choices, and actual available input features. Pairing includes local approval and a verified identity or short authentication code. Remembered peers can be revoked.

The receiver offers a compact session bar: disconnect, host audio volume/mute, microphone mute, input permission state, and connection quality. Pen-first mode favors palm rejection; touch-first mode favors direct interaction. Accessibility and keyboard navigation apply to setup and session controls.

On loss of focus, pointer capture, network connectivity, or consent, release session-owned input promptly. Reconnection negotiates a new session and resets input state; old queued strokes and key events must not replay. Mic capture stops on revocation and session termination. Lock/sleep behavior must be visible and deterministic.

## Quality targets to validate

Initial reference configuration: Windows 11 host and a receiver to be selected, one 1920x1080 60 Hz SDR display, hardware video acceleration, and wired gigabit Ethernet. Record hardware, OS, GPU driver, codec, actual throughput, and receiver refresh rate for every benchmark.

Provisional goals: stable 1080p60; video glass-to-glass latency p95 at or below 60 ms; input-to-visible-response p95 at or below 80 ms on the reference wired setup; no stuck session input after failure; audio/video skew within 50 ms after synchronization. These are engineering targets, not measured claims. Drawing comfort requires separate human evaluation and pen latency measurements.

Adapt bitrate, resolution, and frame rate to network conditions. Prefer recent frames over accumulating delay. Show actual performance and reduced capability rather than silently degrading input to mouse events.

## Deferred scope

Internet relays, cloud accounts, file transfer, clipboard sync, multiple simultaneous receivers, HDR, advanced color workflows, 4K/high refresh guarantees, and comprehensive legacy tablet API compatibility are deferred. They must not block the initial local display and input path. Windows+K and full pen fidelity remain investigation requirements even when implementation is staged.
