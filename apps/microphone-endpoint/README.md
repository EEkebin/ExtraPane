# Windows microphone endpoint helper

This internal helper adapts bounded PCM from the C++ resource session to a local
USB Audio Class 2 capture device. The application remains C++/Qt. Rust is limited
to this integration because the pinned `usbip-audio` library implements the USB/IP
and UAC2 device protocol used by the signed usbip-win2 backend.

`usbip-audio` is MIT licensed, pinned to commit
`58032ab0664ffec1d0c178f40a3a69359c21f597`; its license is included here.
Do not run upstream hardware tests as part of ordinary builds.

## Interface

`ExtraPane.MicrophoneEndpoint.exe --run --slot N` creates one input endpoint,
where N is a positive 32-bit slot identifier. Only run after resource admission. It never captures a local
microphone. Each slot has its own `ExtraPane Microphone N` product/capture name,
descriptor serial, PCM ring, supervised process and owned USB/IP attachment.
The unified listener assigns one slot to each concurrent incoming microphone;
the dedicated diagnostic listener continues to reserve slot 1. Allocation is dynamic,
subject to Windows and hardware resources.
`MICROPHONE_READY slot=N` means USB/IP attach completed, not that an
application successfully recorded audio. `MICROPHONE_STOPPED` reports cleanup.
Fatal errors use `error:` and exit code 1. Allow 30 seconds for startup and 12
seconds for graceful stop. A five-second stdin stall fails closed. The owner must
close stdin if its network peer or parent session disappears.

The helper accepts only a fixed managed executable under the OS-provided Program
Files directory: `ExtraPane/Components/Microphone/usbip.exe`. It rejects reparse
points beneath that directory and verifies Authenticode trust without network
certificate retrieval. It does not search PATH, accept executable arguments,
read USBIP_EXE, elevate, install anything or attach unrelated USB devices. Managed
setup must install the trusted CLI and signed usbip-win2 driver separately; this
helper fails closed if the CLI is missing or unsigned. Whether the chosen upstream
release signs its CLI (in addition to the driver) must be verified by packaging.

The USB/IP server binds only `127.0.0.1` on an ephemeral port. A random bus ID
identifies each session. Attach uses `--once --terse`; the returned decimal port
is retained and only that port is detached. There is no global detach. If attach
succeeds but returns malformed output, its export is removed; no guessed port is
detached. The backend should remove the disconnected endpoint. Global per-slot
mutexes prevent two helpers from claiming the same slot across Windows logon
sessions; an inaccessible or occupied slot fails closed. Session-specific random
bus IDs and attach serials prevent one session from detaching another's port.
Slot names remain recognizable; reconnecting creates a fresh attachment identity.

## Local stdin framing

All frames have a 16-byte header: bytes 0..3 `EPMI`, byte 4 version 1, byte 5 type,
bytes 6..7 zero, bytes 8..11 payload length (big-endian u32), bytes 12..15 sequence
(big-endian u32). Sequence starts at zero and increments across all frame types;
wraparound is rejected. This is local subprocess framing, not a network protocol.

- Type 1: exactly 960 bytes, 480 signed 16-bit little-endian mono PCM samples at
  48,000 Hz (10 ms). Network capture packets must be accumulated/split upstream.
- Type 2: one byte, 0 unmute or 1 mute. Either transition clears buffered audio.
- Type 3: empty payload, stop and detach. EOF also stops, with an error status.
- Type 4: empty payload, discard queued audio and refill after a source discontinuity.

The receiver starts playback after accumulating 40 ms of audio. On starvation it
fades to silence and refills rather than immediately playing each isolated late
packet. Each underrun raises the reserve by 20 ms, up to 80 ms for that unmuted
session. Mute/unmute clears queued samples and resets the reserve; stop discards
all audio immediately. Two-millisecond transitions soften startup, underrun,
refill and overflow discontinuities. The queue has a hard 120 ms limit; on
overflow it discards oldest samples back to the current reserve rather than
letting delay grow indefinitely. These are local buffer delays, not an end-to-end
latency guarantee.

Capture callbacks never block on the ring lock; lock contention still produces
silence. PCM16 is converted to left-aligned signed 24-in-32 samples preserving
amplitude. Every five seconds the helper emits `MICROPHONE_BUFFER` counters for
underrun events, dropped samples, queued milliseconds, target milliseconds and
samples silenced by callback lock contention;
the receiving native session forwards them to its diagnostic output. There is
no adaptive resampling or clock-drift correction yet. A jitter reserve cannot
fix persistent bandwidth shortage or late USB/audio scheduling; multi-display
hardware validation remains required.

## Isolated validation

`cargo test --manifest-path apps/microphone-endpoint/Cargo.toml` tests only this
crate's parser, sample conversion, mute, queue limits, distinct slot identities,
independent PCM stop state, bursty arrival timing, startup/refill, overflow, mute
reset and exact attachment ownership checks.
`ExtraPane.MicrophoneEndpoint.exe --check` validates in memory without sockets,
driver access, enumeration, capture or subprocesses. It does not prove deployment
or audio delivery. Do not invoke `--run` during unattended builds.

The current descriptors use pid.codes test identity `1209:0001`, as the upstream
probe does. This is a development slice, not a claimed assigned production USB
identity. Production descriptor identity, managed backend installation, signed
CLI compatibility, real app recording, unplug/stop cleanup and concurrent real
application recording remain release gates. Synthetic checks do not establish
that four microphones are visible and usable in Windows or an application.
