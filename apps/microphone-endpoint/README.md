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
where N is 1..4. Only run after resource admission. It never captures a local
microphone. Each slot has its own `ExtraPane Microphone N` product/capture name,
descriptor serial, PCM ring, supervised process and owned USB/IP attachment.
The unified listener assigns one slot to each concurrent incoming microphone;
the dedicated diagnostic listener continues to reserve slot 1. Four receiving
microphones are the current capacity, not an unlimited endpoint pool.
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

The ring holds at most 60 ms and drops the oldest samples on overflow. Capture
callbacks never block on its lock; contention, underrun and mute produce silence.
PCM16 is converted to left-aligned signed 24-in-32 samples preserving amplitude.
There is no adaptive resampling or drift compensation yet; bounded drops/silence
keep latency from growing, but long-session audio quality requires device tests.

## Isolated validation

`cargo test --manifest-path apps/microphone-endpoint/Cargo.toml` tests only this
crate's parser, sample conversion, mute, queue limits, distinct slot identities,
independent PCM stop state and exact attachment ownership checks.
`ExtraPane.MicrophoneEndpoint.exe --check` validates in memory without sockets,
driver access, enumeration, capture or subprocesses. It does not prove deployment
or audio delivery. Do not invoke `--run` during unattended builds.

The current descriptors use pid.codes test identity `1209:0001`, as the upstream
probe does. This is a development slice, not a claimed assigned production USB
identity. Production descriptor identity, managed backend installation, signed
CLI compatibility, real app recording, unplug/stop cleanup and concurrent real
application recording remain release gates. Synthetic checks do not establish
that four microphones are visible and usable in Windows or an application.
