# Capabilities and input contract

## Truthful feature negotiation

Effective support is the intersection of physical device features, receiver OS/API access, receiver adapter, negotiated protocol, host injection adapter, and target application behavior. Report available, unavailable, and unverified separately, with reasons. Receiving an API success code is not proof of delivery to a drawing application.

Every session negotiates protocol version, display geometry and epoch, codecs, input families, contact limits, optional pen axes/buttons, audio directions, mic endpoint readiness, and permission grants. Unknown optional fields may be ignored safely; unknown required features reject negotiation with a useful reason. Bound message sizes, rates, contact counts, histories, and queued work.

## Pen

| Feature | Contract expectation | Compatibility check |
| --- | --- | --- |
| Position and contact | Pointer identity, tool identity where exposed, enter/leave, down/move/up/cancel | Native pen at target app |
| Hover and proximity | In-range and in-contact tracked independently | Device and capture API expose hover |
| Pressure | Preserve source range/resolution metadata, normalized value, explicit validity | Host API may quantize; Windows POINTER_PEN_INFO normalizes to 0–1024 |
| Tilt | X/Y angles with documented units, signs, and coordinate transforms | Receiver orientation mapped to host display |
| Rotation/twist | Optional angular axis and validity | Hardware/API must expose it |
| Buttons | Exposed button identities, press/release state, negotiated host mapping | Do not collapse all buttons into right-click |
| Inversion and eraser | Distinct tool/state semantics | Test actual erase behavior in selected apps |
| History | Timestamped actual samples; preserve ordering within a stroke | Avoid throwing away pressure changes when batching |
| Tangential pressure and vendor axes | Extensible descriptors and explicit support status | Baseline Windows pointer API may have no destination field |
| Squeeze, haptics, Bluetooth shortcuts | Separate optional feature adapters, including reverse commands where needed | May be handled by receiver OS or unavailable to apps |

Do not invent missing values. Optional axis absence differs from a valid zero. Predicted samples may be used for local visual feedback, but must be distinguishable and must not be injected as actual measurements. Palm rejection is device/platform dependent; offer pen-first touch suppression as an explicit fallback. Multiple pen devices need negotiated behavior; the baseline synthetic pen injection path is not an arbitrary multi-pen HID stack.

## Touch, mouse, and keyboard

Touch frames carry stable contact IDs, lifecycle, position, and optional pressure/contact geometry. Deliver native multi-touch where possible. Gesture-to-mouse behavior is a separate user mode to avoid duplicate native touch and emulated clicks. Cancellation must be supported for every contact.

Mouse input distinguishes absolute position, relative motion, wheel axes, buttons, and capture loss. Dragging preserves button state across movement and out-of-bounds transitions; session teardown releases held session buttons.

Keyboard separates physical scancodes from committed text and composition. Track down/up/repeat and modifier state; handle layouts, dead keys, Unicode, and IMEs deliberately. Define local escape and reserved shortcuts. Ctrl+Alt+Delete and secure-desktop interaction are outside the baseline synthetic-input path.

## Geometry and timing

Use explicit source/target display IDs, content rectangle, physical pixels, logical scaling, crop/letterbox transform, rotation, and desktop origin (including negative coordinates). Transform input through the inverse rendered-content mapping. Ignore letterbox input unless an explicit policy says otherwise. Display changes create a new geometry epoch; cancel active contacts before remapping rather than stretching an existing stroke across monitors.

Events include session epoch, device/pointer identity, sequence, monotonic sample time, validity fields, and the applicable geometry epoch. Clock synchronization estimates offset and uncertainty; never compare raw clocks as if they were shared. Transport must reject stale or wrong-epoch events.

## Reliability

Control, permissions, key/button transitions, contact lifecycle, and resets require reliable ordered handling. High-rate mouse/hover updates may use a bounded, sequenced channel. Initial pen/touch strokes should favor ordered batched samples; unreliable motion for drawing needs a demonstrated reconstruction strategy first. Different channels require per-device sequencing and barriers so delayed moves cannot occur after up/cancel or a reset.

Use heartbeats and input state reconciliation. If reliable event delivery stalls, stop accepting input after a bounded timeout and clear session-owned state. Never replay buffered input after reconnect. Permission checks apply at the injection boundary, not only in the UI.

## Audio semantics

Host audio mute affects receiver playback; stopping host capture/transport is a separate control if offered. Receiver mic mute must stop forwarding immediately and prevent buffered speech from arriving after mute. Both ends show mic state. Unmute requires an active grant. On endpoint loss, stop reverse audio and report it; do not silently redirect to a different recording device. Plan echo cancellation/noise suppression with a usable playback reference and test feedback when speakers and microphone are both enabled.
