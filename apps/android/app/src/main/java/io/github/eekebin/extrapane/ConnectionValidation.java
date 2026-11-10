package io.github.eekebin.extrapane;

/**
 * Pure configuration checks; reachability, credentials and hardware are checked by native sessions.
 */
final class ConnectionValidation {
    static boolean initialDisplayMode(boolean exact, int width, int height, int fps, long[] offer) {
        return !exact || (offer[0] == width && offer[1] == height && offer[2] == fps);
    }

    static boolean sameTarget(
            String configured,
            boolean display,
            String displayTarget,
            boolean camera,
            String cameraTarget,
            boolean microphone,
            String microphoneTarget) {
        return !configured.isEmpty()
                && (!display || configured.equals(displayTarget))
                && (!camera || configured.equals(cameraTarget))
                && (!microphone || configured.equals(microphoneTarget));
    }

    static int number(String text) {
        if (!text.matches("[0-9]+")) return -1;
        try {
            return Integer.parseInt(text);
        } catch (NumberFormatException error) {
            return -1;
        }
    }

    static boolean port(String text) {
        int value = number(text);
        return value >= 1 && value <= 65535;
    }

    static boolean code(String text) {
        return text.isEmpty() || text.matches("[0-9]{6}");
    }

    static boolean dimension(int value) {
        return value >= 2 && value <= 4096 && (value % 2) == 0;
    }

    static String normalizedAddress(String text) {
        return "localhost".equalsIgnoreCase(text) ? "127.0.0.1" : text;
    }

    static boolean ipv4(String text) {
        text = normalizedAddress(text);
        String[] parts = text.split("\\.", -1);
        if (parts.length != 4) return false;
        int first = -1;
        boolean nonzero = false;
        for (int i = 0; i < 4; ++i) {
            if (!parts[i].matches("[0-9]{1,3}")) return false;
            int value = number(parts[i]);
            if (value < 0 || value > 255) return false;
            if (i == 0) first = value;
            nonzero |= value != 0;
        }
        return nonzero && first > 0 && first < 224;
    }
}
