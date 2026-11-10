package io.github.eekebin.extrapane;

import static io.github.eekebin.extrapane.AudioRouteCatalog.Kind.*;

import java.util.*;

public final class AudioRouteCatalogCheck {
    private static void require(boolean value, String reason) {
        if (!value) throw new AssertionError(reason);
    }

    public static void main(String[] args) {
        var routes =
                List.of(
                        new AudioRouteCatalog.Route(70, INTERNAL, "Pixel 8a", ""),
                        new AudioRouteCatalog.Route(23, SPEAKER, "Pixel 8a", ""),
                        new AudioRouteCatalog.Route(24, SPEAKER, "Pixel 8a", ""),
                        new AudioRouteCatalog.Route(11, EARPIECE, "Pixel 8a", ""),
                        new AudioRouteCatalog.Route(19, BUILTIN_MIC, "Pixel 8a", "bottom"),
                        new AudioRouteCatalog.Route(20, BUILTIN_MIC, "Pixel 8a", "back"));
        var outputs = AudioRouteCatalog.entries(routes, false, "Pixel 8a");
        require(
                outputs.equals(
                        List.of(
                                new AudioRouteCatalog.Entry(23, "Loudspeaker"),
                                new AudioRouteCatalog.Entry(11, "Earpiece"))),
                "Phone outputs must be physical built-ins with original route IDs and loudspeaker"
                        + " first");
        var inputs = AudioRouteCatalog.entries(routes, true, "Pixel 8a");
        require(
                inputs.equals(
                        List.of(
                                new AudioRouteCatalog.Entry(19, "Built-in microphone (bottom)"),
                                new AudioRouteCatalog.Entry(20, "Built-in microphone (back)"))),
                "Keep selectable microphones and their positions");
        var externals =
                AudioRouteCatalog.entries(
                        List.of(
                                new AudioRouteCatalog.Route(80, USB, "USB DAC", "a"),
                                new AudioRouteCatalog.Route(81, USB, "USB DAC", "b"),
                                new AudioRouteCatalog.Route(
                                        82, BLUETOOTH_MEDIA, "My headphones", "")),
                        false,
                        "Pixel 8a");
        require(externals.size() == 3, "Do not collapse distinct peripherals with identical names");
        require(
                externals.get(0).label().equals("Bluetooth audio · My headphones"),
                "Bluetooth name must describe route and product");
        require(
                externals.get(1).equals(new AudioRouteCatalog.Entry(80, "USB audio · USB DAC 1"))
                        && externals
                                .get(2)
                                .equals(new AudioRouteCatalog.Entry(81, "USB audio · USB DAC 2")),
                "Ambiguous peripherals need stable ordinal labels, not native port numbers");
        require(
                AudioRouteCatalog.entries(
                                List.of(new AudioRouteCatalog.Route(70, INTERNAL, "Pixel 8a", "")),
                                false,
                                "Pixel 8a")
                        .isEmpty(),
                "Do not fabricate speaker choices if only system ports exist");
        System.out.println("Audio route catalog checks passed");
    }
}
