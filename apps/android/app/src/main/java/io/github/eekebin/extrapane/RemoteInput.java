package io.github.eekebin.extrapane;

import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import java.util.HashMap;
import java.util.HashSet;

final class RemoteInput {
    final InputQueue queue;
    boolean enabled;
    private final HashSet<Integer> contacts = new HashSet<>();
    private final HashMap<Long, Integer> keys = new HashMap<>();
    private final StylusInput stylus;
    private int buttons;

    RemoteInput(InputQueue queue) {
        this.queue = queue;
        stylus = new StylusInput(queue);
    }

    RemoteInput(InputQueue queue, StylusInput.Axes axes) {
        this.queue = queue;
        stylus = new StylusInput(queue, axes);
    }

    void reset() {
        contacts.clear();
        keys.clear();
        buttons = 0;
        stylus.clear();
        queue.reset();
    }

    void enable(boolean value) {
        reset();
        enabled = value;
    }

    private int coordinate(float value, int size) {
        return Math.round(Math.max(0, Math.min(1, value / Math.max(1, size))) * 65535);
    }

    boolean motion(View view, MotionEvent event) {
        if (!enabled) return true;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            reset();
            return true;
        }
        boolean allPen = true;
        for (int i = 0; i < event.getPointerCount(); i++) {
            if (StylusInput.isPen(event.getToolType(i))) stylus.motion(view, event, i);
            else allPen = false;
        }
        if (allPen) return true;
        if (event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE) {
            int x = coordinate(event.getX(), view.getWidth()),
                    y = coordinate(event.getY(), view.getHeight());
            if (action == MotionEvent.ACTION_SCROLL) {
                for (int axis = 0; axis < 2; axis++) {
                    int delta =
                            Math.max(
                                    -32767,
                                    Math.min(
                                            32767,
                                            Math.round(
                                                    event.getAxisValue(
                                                                    axis == 0
                                                                            ? MotionEvent
                                                                                    .AXIS_VSCROLL
                                                                            : MotionEvent
                                                                                    .AXIS_HSCROLL)
                                                            * 120)));
                    if (delta != 0) {
                        int[] p = new int[15];
                        p[0] = 1;
                        p[1] = 4;
                        p[4] = x;
                        p[5] = y;
                        p[6] = axis;
                        p[7] = delta;
                        queue.add(p);
                    }
                }
                return true;
            }
            int current = event.getButtonState();
            int[] masks = {1, 2, 4, 8, 16};
            for (int i = 0; i < masks.length; i++)
                if (((current ^ buttons) & masks[i]) != 0) {
                    int[] p = new int[15];
                    p[0] = 1;
                    p[1] = (current & masks[i]) != 0 ? 1 : 2;
                    p[4] = x;
                    p[5] = y;
                    p[6] = i + 1;
                    queue.add(p);
                }
            buttons = current;
            int[] p = new int[15];
            p[0] = 1;
            p[4] = x;
            p[5] = y;
            queue.add(p);
            return true;
        }
        if ((queue.capabilities() & 4) == 0) return true;
        int first = action == MotionEvent.ACTION_MOVE ? 0 : event.getActionIndex();
        int last = action == MotionEvent.ACTION_MOVE ? event.getPointerCount() : first + 1;
        for (int i = first; i < last; i++) {
            if (event.getToolType(i) != MotionEvent.TOOL_TYPE_FINGER) continue;
            int id = event.getPointerId(i) + 1;
            boolean down =
                    action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN;
            boolean up = action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP;
            if (down) {
                if (contacts.size() >= 10 || !contacts.add(id)) continue;
            } else if (!contacts.contains(id)) continue;
            if (!down && !up && action != MotionEvent.ACTION_MOVE) continue;
            int[] p = new int[15];
            p[0] = 3;
            p[1] =
                    down
                            ? 1
                            : up
                                    ? ((event.getFlags() & MotionEvent.FLAG_CANCELED) != 0 ? 3 : 2)
                                    : 0;
            p[2] = up ? 0 : 3;
            p[3] = id;
            p[4] = coordinate(event.getX(i), view.getWidth());
            p[5] = coordinate(event.getY(i), view.getHeight());
            // Android pressure can be calibrated above 1; normalize to protocol bounds.
            if (event.getDevice() != null
                    && event.getDevice()
                                    .getMotionRange(MotionEvent.AXIS_PRESSURE, event.getSource())
                            != null) {
                p[8] = Math.round(Math.max(0, Math.min(1, event.getPressure(i))) * 1024);
                p[12] |= 4;
            }
            float major = event.getTouchMajor(i), minor = event.getTouchMinor(i);
            if (major > 0 && minor > 0) {
                p[13] = coordinate(minor, view.getWidth());
                p[14] = coordinate(major, view.getHeight());
                p[12] |= 1;
            }
            queue.add(p);
            if (up) contacts.remove(id);
        }
        return true;
    }

    private static final int[] VK = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0xbd, 0xbb, 8, 9, 'Q', 'W', 'E',
        'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0xdb, 0xdd, 13, 0xa2, 'A', 'S', 'D', 'F', 'G', 'H', 'J',
        'K', 'L', 0xba, 0xde, 0xc0, 0xa0, 0xdc, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0xbc, 0xbe, 0xbf,
        0xa1, 0x6a, 0xa4, 32, 0x14, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x90, 0x91, 0x67, 0x68, 0x69, 0x6d, 0x64, 0x65, 0x66, 0x6b, 0x61, 0x62, 0x63, 0x60, 0x6e
    };

    boolean key(KeyEvent e) {
        if (!enabled
                || (queue.capabilities() & 2) == 0
                || (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_UP))
            return false;
        if ((e.getFlags() & KeyEvent.FLAG_SOFT_KEYBOARD) != 0) return false;
        int code = e.getScanCode(), scan = code, vk = 0, ext = 0;
        if (code == 0) {
            code = KeyboardMapping.evdev(e.getKeyCode());
            scan = code;
        }
        if (code > 0 && code < VK.length) vk = VK[code];
        else {
            int[] codes = {
                86, 87, 88, 96, 97, 98, 100, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 125,
                126, 127
            };
            int[] scans = {
                86, 87, 88, 28, 29, 53, 56, 71, 72, 73, 75, 77, 79, 80, 81, 82, 83, 91, 92, 93
            };
            int[] vks = {
                0xe2, 0x7a, 0x7b, 13, 0xa3, 0x6f, 0xa5, 0x24, 0x26, 0x21, 0x25, 0x27, 0x23, 0x28,
                0x22, 0x2d, 0x2e, 0x5b, 0x5c, 0x5d
            };
            for (int i = 0; i < codes.length; i++)
                if (code == codes[i]) {
                    scan = scans[i];
                    vk = vks[i];
                    ext = i >= 3 ? 1 : 0;
                    break;
                }
        }
        if (vk == 0) return false;
        boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        long identity = ((long) e.getDeviceId() << 32) | (code & 0xffffffffL);
        if (down) {
            if (e.getRepeatCount() > 0 && !keys.containsKey(identity)) return true;
            keys.put(identity, vk);
        } else {
            if (keys.remove(identity) == null) return true;
            if (keys.containsValue(vk)) return true;
        }
        int[] p = new int[15];
        p[0] = 2;
        p[1] = down ? 1 : 2;
        p[3] = vk;
        p[6] = scan;
        p[7] = ext;
        queue.add(p);
        return true;
    }
}
