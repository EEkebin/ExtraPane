package io.github.eekebin.extrapane;

import java.util.*;

/** Presentation policy, separate from Android routing so it can be checked without devices. */
final class AudioRouteCatalog {
    enum Kind {
        SPEAKER("Loudspeaker", false, true),
        EARPIECE("Earpiece", false, true),
        BUILTIN_MIC("Built-in microphone", true, false),
        WIRED_HEADSET("Wired headset", true, true),
        WIRED_HEADPHONES("Wired headphones", false, true),
        BLUETOOTH_MEDIA("Bluetooth audio", false, true),
        BLUETOOTH_CALL("Bluetooth headset (call audio)", true, true),
        BLUETOOTH_LE("Bluetooth LE headset", true, true),
        BLUETOOTH_SPEAKER("Bluetooth LE speaker", false, true),
        BLUETOOTH_BROADCAST("Bluetooth LE broadcast", false, true),
        HEARING_AID("Hearing aid", false, true),
        USB("USB audio", true, true),
        HDMI("HDMI audio", false, true),
        DOCK("Dock audio", true, true),
        LINE("Line audio", true, true),
        INTERNAL("", false, false);
        final String title;
        final boolean input, output;

        Kind(String title, boolean input, boolean output) {
            this.title = title;
            this.input = input;
            this.output = output;
        }
    }

    record Route(int id, Kind kind, String product, String address) {}

    record Entry(int id, String label) {}

    static List<Entry> entries(List<Route> routes, boolean input, String model) {
        List<Route> visible = new ArrayList<>();
        Set<Kind> builtins = new HashSet<>();
        // Audio ports are not all user-selectable hardware. Internal/telephony/
        // remote-submix and alarm-only safe-speaker ports never reach this list.
        routes.stream()
                .sorted(Comparator.comparingInt(Route::id))
                .forEach(
                        route -> {
                            if (!(input ? route.kind.input : route.kind.output)) return;
                            if ((route.kind == Kind.SPEAKER || route.kind == Kind.EARPIECE)
                                    && !builtins.add(route.kind)) return;
                            visible.add(route);
                        });
        visible.sort(
                Comparator.comparingInt((Route r) -> r.kind.ordinal()).thenComparingInt(Route::id));
        List<String> labels = new ArrayList<>();
        Map<String, Integer> totals = new HashMap<>(), seen = new HashMap<>();
        for (Route route : visible) {
            String label = route.kind.title;
            String product = route.product == null ? "" : route.product.trim();
            boolean builtin =
                    route.kind == Kind.SPEAKER
                            || route.kind == Kind.EARPIECE
                            || route.kind == Kind.BUILTIN_MIC;
            if (!builtin
                    && !product.isEmpty()
                    && !product.equalsIgnoreCase(model)
                    && !product.equalsIgnoreCase(label)) label += " · " + product;
            if (route.kind == Kind.BUILTIN_MIC) {
                String address =
                        route.address == null ? "" : route.address.toLowerCase(Locale.ROOT);
                if (address.equals("bottom")
                        || address.equals("top")
                        || address.equals("front")
                        || address.equals("back")) label += " (" + address + ")";
            }
            labels.add(label);
            totals.merge(label, 1, Integer::sum);
        }
        List<Entry> result = new ArrayList<>();
        for (int i = 0; i < visible.size(); i++) {
            String label = labels.get(i);
            int ordinal = seen.merge(label, 1, Integer::sum);
            // Distinct external devices are retained even if their product names match.
            result.add(
                    new Entry(
                            visible.get(i).id,
                            totals.get(label) > 1 ? label + " " + ordinal : label));
        }
        return result;
    }
}
