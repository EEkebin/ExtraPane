# Speaker resource protocol

Speakers append resource kind **4**. Display=0, camera=1, microphone=2 and USB=3
retain their meanings. Audio flows from a server-side virtual playback device
into the selected client output; it is independent of display and microphone
sessions. Capability checks therefore use server source + client destination.

## Compatibility

Existing capability serialization defaults to four entries, byte-compatible with
resource envelope v1. The decoder accepts exactly the complete four-kind set or
all five kinds; missing, duplicate, unknown and truncated entries are rejected.
Only speaker sessions opt into `wire::CapabilityFormat::with_speakers` for both
`encode_capabilities` and `Negotiation`. A legacy negotiation refuses a speaker
request even if a peer sends extended capabilities.

The existing grouped-admission v1 message remains four-resource-only, including
its 520-byte reply. Speakers use dynamic control `add(kind=4)`, with unchanged
16-byte request, 128-byte reply, and 105-character EPCT1 credential layout.
Credentials remain kind-bound and one-use. An older peer rejects the new kind;
other existing resource formats have not changed.

## Audio framing

The 32-byte offer starts `EPSO`, u16 big-endian version1, PCM16 format1, channels2,
48,000 Hz u32, 480 sample frames u16, two zero reserved bytes, then nonzero u64
resource and generation bindings. The eight-byte prefix routes the admitted
session. Audio uses interleaved signed little-endian stereo PCM16, 10ms per packet
(480 stereo frames, 1,920 bytes).

Each frame has a 40-byte header: `EPSD`, u16 version1, a zero byte, flags (bit0 mute,
bit1 discontinuity), resource/generation u64, sequence u64, sample-frame position
u64. Header integers are big-endian; only PCM samples are little-endian. Sequence
starts at zero and never wraps. Positions advance by480 except an explicitly
marked forward discontinuity. Partial reads/writes, a wrong binding, backwards
positions or replay permanently poison that Channel instance. Muted PCM is zeroed
both before send and after receive.

## Playback reserve

`speaker_wire::JitterBuffer` is single-audio-worker-owned. A separate bounded
transport queue delivers Frames to that worker. The helper allocates no memory
and acquires no locks during accept/render. It prefills40ms, refills after
starvation (target grows to80ms), and caps queued audio at120ms. Overflow discards
oldest frames back toward the current target. Both channels have matching2ms
transitions. Mute/discontinuity/reset clears old queued audio. Counters expose
starvation, discarded frames and discontinuities. This does not implement clock
drift resampling or prove actual endpoint scheduling under load.
