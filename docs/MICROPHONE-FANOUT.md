# Windows microphone recipients

The unified Windows listener can allocate four distinct receiving microphone
slots. Each native helper receives its assigned slot, exposes the corresponding
`ExtraPane Microphone N` product/capture name, and owns its own USB/IP attachment.
Global slot leases reject duplicate slot ownership, including across Windows
logon sessions. The standalone microphone diagnostic listener still reserves
slot 1. Actual simultaneous recording from these endpoints in Windows apps
requires device testing; isolated descriptor/queue checks do not prove delivery.

## One source, several recipients

`ExtraPane.Microphone.exe broker-microphone --device-stdin [--parent-pid N]` is a
persistent native source helper. The controller launches one broker per locally
selected physical microphone. It reads the source device ID from the first stdin
line, then accepts the following tab-separated commands:

```text
ADD<TAB>row-id<TAB>local-ipv4-or-auto<TAB>server-ipv4<TAB>port<TAB>credential
REMOVE<TAB>row-id
MUTE<TAB>row-id<TAB>0-or-1
Q
```

`credential` is the freshly scoped admission credential, or `-` for automatic LAN
mode. It travels only through the local input pipe; it is not a process argument,
status message or saved setting. Row IDs are 1–128 ASCII letters, digits, hyphens
or underscores. The broker limits one source to 16 recipient rows. The receiving
host's independent four-slot capacity can reject additional incoming streams.

The broker emits `BROKER_READY` after initialization. Per-row events are:

```text
RESOURCE_EVENT<TAB>row-id<TAB>STARTED<TAB>message
RESOURCE_EVENT<TAB>row-id<TAB>STOPPED<TAB>message
RESOURCE_EVENT<TAB>row-id<TAB>ERROR<TAB>message
```

`STARTED` is emitted after that recipient's TLS/admission, endpoint negotiation,
and first PCM send succeed. It does not prove that a receiving application has
recorded audio. EOF, `Q`, or launcher exit stops all recipients.

One native capture worker supplies all admitted recipients. Capture starts when
the first recipient is ready and stops when the last active recipient leaves.
Each recipient has its own TLS connection, negotiation, network worker and bounded
60 ms PCM queue. Slow recipients drop their oldest queued audio and receive a
discontinuity indication; capture and other recipients continue. Muting clears
only that recipient's queue and sends silence to that recipient. Removing or
losing a recipient closes only its stream. Source capture/permission failure
stops every subscriber because they depend on the same source.

PCM stays in native C++; it never travels through the Qt UI or JSON. This path
retains the existing 48 kHz mono PCM16 wire format and source mix-format limits.
It does not add resampling, compressed audio or drift correction.

## Isolated verification

The native microphone check covers slot argument/readiness matching, slot limits,
independent queue/mute/close behavior, and slow-recipient discontinuities. Rust
endpoint tests cover all four descriptor identities, independent PCM buffers and
exact attachment cleanup ownership. These tests do not activate capture, create
USB endpoints or install drivers. Simultaneous source capture, multiple server
destinations and recording through several receiving OS endpoints remain hardware
verification steps.
