package io.github.eekebin.extrapane;

/** UI-thread listener intent. A confirmation from a canceled start cannot revive sharing. */
final class SharingLifecycle {
    private long generation;
    private boolean listening;

    void begin(long value) {
        if (value <= 0) throw new IllegalArgumentException("Invalid sharing generation");
        if (active()) throw new IllegalStateException("Sharing is already starting or listening");
        generation = value;
        listening = false;
    }

    // Listener replacement does not own active media sessions.
    void replace(long value) {
        if (value <= 0) throw new IllegalArgumentException("Invalid sharing generation");
        generation = value;
        listening = false;
    }

    boolean active() {
        return generation != 0;
    }

    boolean listening() {
        return listening;
    }

    boolean matches(long value) {
        return value != 0 && value == generation;
    }

    boolean confirm(long value) {
        if (!matches(value)) return false;
        listening = true;
        return true;
    }

    void stop() {
        generation = 0;
        listening = false;
    }
}
