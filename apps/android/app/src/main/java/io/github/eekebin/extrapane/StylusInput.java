package io.github.eekebin.extrapane;

import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;

/** One native pen, matching the host's synthetic-pen integration. No mouse emulation. */
final class StylusInput {
    interface Axes {
        boolean has(MotionEvent event, int axis);
    }

    private final InputQueue queue;
    private final Axes axes;
    private int device = -1, pointer = -1;
    private boolean contact;
    private int[] last;

    StylusInput(InputQueue queue) {
        this(
                queue,
                (event, axis) -> {
                    InputDevice d = event.getDevice();
                    return d != null
                            && (d.getMotionRange(axis, event.getSource()) != null
                                    || d.getMotionRange(axis) != null);
                });
    }

    StylusInput(InputQueue queue, Axes axes) {
        this.queue = queue;
        this.axes = axes;
    }

    static boolean isPen(int tool) {
        return tool == MotionEvent.TOOL_TYPE_STYLUS || tool == MotionEvent.TOOL_TYPE_ERASER;
    }

    void clear() {
        device = pointer = -1;
        contact = false;
        last = null;
    }

    private void cancel() {
        if (last != null) {
            int[] p = last.clone();
            p[1] = 3;
            p[2] = 0;
            p[8] = 0;
            queue.add(p);
        }
        clear();
    }

    boolean motion(View view, MotionEvent event, int index) {
        if ((queue.capabilities() & 8) == 0) return true;
        int action = event.getActionMasked();
        boolean down =
                (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)
                        && index == event.getActionIndex();
        boolean up =
                (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP)
                        && index == event.getActionIndex();
        boolean exit = action == MotionEvent.ACTION_HOVER_EXIT;
        if (action == MotionEvent.ACTION_CANCEL) {
            cancel();
            return true;
        }
        if (!down
                && !up
                && !exit
                && action != MotionEvent.ACTION_MOVE
                && action != MotionEvent.ACTION_HOVER_MOVE
                && action != MotionEvent.ACTION_HOVER_ENTER
                && action != MotionEvent.ACTION_BUTTON_PRESS
                && action != MotionEvent.ACTION_BUTTON_RELEASE) return true;
        int id = event.getPointerId(index);
        if (pointer != -1 && (device != event.getDeviceId() || pointer != id)) cancel();
        if (up && !contact) return true;
        if (down) {
            if (contact) cancel();
            contact = true;
        }
        // Ignore contact moves after focus loss until a fresh DOWN; hover is safe.
        if (action == MotionEvent.ACTION_MOVE && !contact) return true;
        if (exit && contact) {
            cancel();
            return true;
        }
        device = event.getDeviceId();
        pointer = id;
        boolean cancelled = up && (event.getFlags() & MotionEvent.FLAG_CANCELED) != 0;
        int wireAction = cancelled ? 3 : down ? 1 : up ? 2 : 0;
        if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_HOVER_MOVE) {
            for (int history = 0; history < event.getHistorySize(); history++)
                emit(view, event, index, history, 0, contact ? 3 : 1);
        }
        emit(view, event, index, -1, wireAction, exit || cancelled ? 0 : up ? 1 : contact ? 3 : 1);
        if (up) contact = false;
        if (exit || cancelled) clear();
        return true;
    }

    private static float value(MotionEvent e, int axis, int index, int history) {
        return history < 0
                ? e.getAxisValue(axis, index)
                : e.getHistoricalAxisValue(axis, index, history);
    }

    private static int normalized(float value, int size) {
        return Math.round(Math.max(0, Math.min(1, value / Math.max(1, size))) * 65535);
    }

    private void emit(View view, MotionEvent e, int index, int history, int action, int flags) {
        float x = value(e, MotionEvent.AXIS_X, index, history),
                y = value(e, MotionEvent.AXIS_Y, index, history);
        if (!Float.isFinite(x) || !Float.isFinite(y)) {
            cancel();
            return;
        }
        int[] p = new int[15];
        p[0] = 4;
        p[1] = action;
        p[2] = flags;
        p[3] = e.getPointerId(index) + 1;
        p[4] = normalized(x, view.getWidth());
        p[5] = normalized(y, view.getHeight());
        p[6] = (e.getButtonState() & MotionEvent.BUTTON_STYLUS_PRIMARY) != 0 ? 1 : 0;
        if (e.getToolType(index) == MotionEvent.TOOL_TYPE_ERASER) p[6] |= 6;
        float pressure = value(e, MotionEvent.AXIS_PRESSURE, index, history);
        if (axes.has(e, MotionEvent.AXIS_PRESSURE) && Float.isFinite(pressure)) {
            p[8] = (flags & 2) != 0 ? Math.round(Math.max(0, Math.min(1, pressure)) * 1024) : 0;
            p[12] |= 1;
        }
        float tilt = value(e, MotionEvent.AXIS_TILT, index, history),
                direction = value(e, MotionEvent.AXIS_ORIENTATION, index, history);
        if (axes.has(e, MotionEvent.AXIS_TILT)
                && axes.has(e, MotionEvent.AXIS_ORIENTATION)
                && Float.isFinite(tilt)
                && Float.isFinite(direction)) {
            // Android direction is clockwise from screen-up; Windows +X is
            // right and +Y is down/toward the user. Direction is NOT barrel rotation.
            double t = Math.max(0, Math.min(Math.PI / 2, tilt)),
                    z = Math.cos(t),
                    radius = Math.sin(t);
            p[9] = (int) Math.round(Math.toDegrees(Math.atan2(radius * Math.sin(direction), z)));
            p[10] = (int) Math.round(Math.toDegrees(Math.atan2(-radius * Math.cos(direction), z)));
            p[9] = Math.max(-90, Math.min(90, p[9]));
            p[10] = Math.max(-90, Math.min(90, p[10]));
            p[12] |= 12;
        }
        queue.add(p);
        last = p;
    }
}
