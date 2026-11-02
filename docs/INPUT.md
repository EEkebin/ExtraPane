# Native input prototype (0.6)

Mouse, physical keyboard scancodes, native touch, and native pen now have a receiver-to-host path. This is an opt-in diagnostic prototype. The user confirmed the physical pen worked after the burst fix; individual pen features, physical multi-touch and drawing-app compatibility still need acceptance checks. Synthetic integration tests are identified below.

## Try it

Keep the Surface Book 2 signed in and unlocked. From administrator PowerShell in the desktop project folder:

```powershell
.\scripts\try-display.ps1 -Control
```

The launcher waits for readiness, prompts for the laptop's SSH password, uploads matching binaries, creates disposable pairing entries and scoped test firewall rules, and starts a two-minute native 3000x2000 extended-display session targeting 60 fps. This uses the updated 0.7 display driver; input itself needs no additional driver. Without `-Control`, the launcher remains view only. Use `-Fps 30` to request 30 fps, `-Seconds 300` for five minutes, and F9 to close the fullscreen preview. See NATIVE-DISPLAY.md for measured rates.

Click or tap the laptop preview to focus it, then use its mouse/keyboard, touchscreen, or pen on the host test window shown in the video. Mouse drag draws a line; typing appears in the test window; fingers draw independent touch strokes; pen line thickness follows pressure. The status text displays pressure, tilt, masks, and pen flags exposed to the host. Its black eraser stroke is a diagnostic indication of native eraser flags, not a production drawing app's erase implementation. Try the pen's eraser end/button if available, then test in the intended drawing app separately.

**F8 pauses/resumes forwarding.** Focus loss resets held input; closing the preview disconnects. The virtual monitor is removed after the bounded session. Driver and test certificate remain installed. Audio is absent. Repeatable launchers remain diagnostic scripts, not the final app UI.

## Permission and transport

Host requires `--allow-screen --allow-input` plus `--extend` or an explicit `--display` target. Receiver requires `--control`; headless mode cannot request interactive control. No input is sent/injected in view-only sessions. Authentication precedes monitor creation, capability grants, and broker startup.

Video protocol **4** negotiates input families and a fresh random 64-bit session epoch. Receiver hardware families come from Windows' digitizer flags; host families reflect successful synthetic-device creation. Optional pen/touch fields retain per-sample validity masks. This does not prove every advertised hardware axis is present or accepted by a particular application. Older protocol 3 binaries are rejected; both endpoints must be updated.

One bounded reverse batch follows each decoded video access unit on the same TLS 1.3 connection. Header includes session epoch, video sequence, event count, and exact byte length. Limit is 128 fixed 64-byte events per batch. Invalid permissions, family, epoch, sequence, counts, reserved fields, coordinate/axis ranges, or sample ordering terminate the session. No concurrent calls share a TLS object. Video pacing and network/decoder stalls therefore affect input latency; a separate low-latency channel and persistent session controls remain future work.

The UI queue coalesces adjacent mouse moves only. Pen updates preserve up to 64 actual coalesced Windows samples in chronological order; pressure-changing samples are not deliberately merged. Queue overflow resets and pauses input instead of dropping a release and continuing a stroke. Touch currently forwards current pointer samples, not full touch frame history. Packet timestamps describe receiver enqueue order; synchronized hardware-sample timing is not implemented.

## Native delivery and geometry

- Mouse: absolute virtual-desktop coordinates, five buttons, both wheel axes, capture-aware dragging, and pointer-promoted mouse suppression.
- Keyboard: scancodes, E0 extended flag, down/up/repeat and modifiers. F8 stays local. Committed Unicode/IME composition and complete reserved-system shortcut handling are not implemented. Host layout controls physical scancode interpretation. Keys are accepted only while the foreground host window belongs to the selected monitor.
- Touch: up to ten stable IDs, contact lifecycle, optional contact size/orientation/pressure, and full active-contact synthetic frames. Each contact's capture loss cancels that contact. No gesture-to-mouse fallback is used as evidence of native touch. Target apps implement pinch/zoom/manipulation themselves.
- Pen: one native synthetic pen, hover/contact, pressure 0..1024, tilt X/Y -90..90, rotation 0..359 when valid, barrel, inversion, eraser, and cancellation. Synthetic pen injection needs inversion during hover and eraser during contact; the adapter handles that distinction and clears eraser on up to avoid unintended contact. Vendor axes, Bluetooth shortcuts, haptics, multiple simultaneous pens, and legacy WinTab are unsupported/unverified.

The receiver is per-monitor DPI aware. It maps the preview's full client rectangle back to normalized display coordinates; the existing swapchain stretches the image to that rectangle, so mapping follows that stretch. Active drags clamp at its edges. Host mapping includes physical target dimensions and negative virtual-desktop origins. Geometry/identity changes terminate input and release state; there is no mid-stroke remapping. Rotation and mixed-DPI hardware tests remain pending.

## Watchdog and ownership

The host starts a separate injection child with explicitly inherited anonymous pipes, no network listener, and no credentials in its arguments. It validates packets again, checks target geometry, and owns its synthetic pen/touch devices. Pipe loss, malformed input, target change, or 1500 ms without a complete heartbeat batch releases state and exits. Synthetic contacts are refreshed every 50 ms while held so Windows does not expire stationary contact. A host process crash closes its pipe and leaves this child able to release input.

Keyboard/mouse injection tracks session-owned presses. Low-level hooks distinguish physical local events from injected events; pre-existing or subsequently held local keys/buttons are not deliberately released by remote cleanup. Simultaneous physical-local/remote ownership still needs hardware validation. Secure desktop/elevated-window control is not promised. The current host inherits administrator rights for virtual-display activation; a product should separate that narrow privilege from the interactive engine.

## Verification

Debug/Release CTest includes a fifth input protocol suite for bounds, signed axes, masks, permissions, and epoch serialization. Hardware-dependent checks are separate:

```powershell
.\out\build\windows-release\dm-input-check.exe
# Use Python 3.13+ with TLS PSK support:
python .\scripts\test_input_transport.py
python .\scripts\test_video_laptop.py --laptop-ip 10.0.20.119 --desktop-ip 10.0.0.210 --user kebinBook --frames 300 --fps 10 --decoder-adapter 0 --extend --show-pattern --control --exercise-input
```

Native host test: a diagnostic window receives mouse/key down/up, two touch contacts, pen pressure 800, tilt -35/+40, rotation 270, barrel/inversion/eraser flags, held-contact refresh, reset, watchdog timeout, and cleanup after forced termination of the injection parent's process. This verifies app messages, not only API returns.

Independent OpenSSL/Python peer tests pass permission rejection, wrong epoch, wrong sequence, oversized batch, invalid pen pressure, and valid reset batches. Each case restores the original physical-monitor topology. These use the real TLS/video/input parser and watchdog; valid packets send reset only.

Surface Book 2 end-to-end generated-input test: normal-user native input is generated inside the laptop preview, captured through receiver window messages, carried over TLS, injected on the desktop, and observed by the host test window. The successful 720p10 run decoded/presented 300/300 frames, processed 26 input events, and observed mouse=1, chars=1, touch=2, pen=1, pressure_max=900. Digitizer flags were 197 and all four input families were negotiated. Raw-pixel readbacks/uploads remained zero; original topology returned and temporary credentials/firewall rules were removed. Report: ignored `out/laptop-h264-gpu-adapter-0-extend-300frames-10fps-input-test.json`.

An earlier idle input-enabled laptop run decoded/presented 600 frames but recorded zero input events; it is not evidence of physical device capture. The generated test subsequently exposed and fixed contact-enter duplication/capture-loss handling. Physical pen pressure ramp, tilt, hover, barrel, eraser, two-finger interaction, mouse drag, keyboard layout, F8/focus-loss behavior, and application-specific drawing remain user acceptance checks.

Windows references: [pointer pen fields](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_pen_info), [synthetic pointer injection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-injectsyntheticpointerinput), [pointer capture/lifecycle](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerdown), [capture loss](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointercapturechanged), [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

## 0.6.1 pen burst correction

The first user physical-pen tap closed the session with `Windows rejected pen frame`, Win32 error 87. Simple synthetic tap/release cases passed, but a new 62-sample pen batch reproduced the same error (a contact update with pressure 209). The earlier generated test used spaced samples and missed this case.

The injection adapter now spaces synthetic pointer calls by at least 250 microseconds using a high-resolution waitable timer. Pen/touch samples, pressure values and order are preserved. Pacing also covers hold refresh and cancellation. Pen and touch burst regressions now pass, along with idle-leave, first-contact without hover, release-coordinate drift, cancellation with residual pressure, contact-to-hover, eraser/axes, held touch, reset, watchdog and parent-crash checks. Both build configurations pass five CTest suites.

The expanded laptop exercise now generates 50 closely spaced pen updates rather than one slow movement. The post-fix laptop run processed 76 events, decoded 300 frames and presented 299, and the host diagnostic received mouse=1, chars=1, touch=2, pen=1, pressure_max=897. It restored the original display topology and removed disposable credentials/firewall rules. The exact physical-pen case still needs user retesting. An intervening manual run timed out with zero events and is not evidence of the fix on physical hardware.

Windows documents timestamp-spacing requirements for [touch injection](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-injecttouchinput); the synthetic-pen error-87 burst behavior above is our observed result, not an assertion that error 87 always means a timing problem.

## Physical retest

The user subsequently confirmed the repaired session worked perfectly. That run processed 1,632 input events and decoded/presented 600/600 frames at 720p10. This confirms the reported physical-pen failure did not recur in that session; it does not establish every axis/button, physical multi-touch or drawing-app compatibility.
