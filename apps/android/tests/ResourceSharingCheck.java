package io.github.eekebin.extrapane;

import java.util.*;

public final class ResourceSharingCheck {
    static void require(boolean ok) {
        if (!ok) throw new AssertionError();
    }

    public static void main(String[] args) {
        Map<String, Boolean> saved = new HashMap<>();
        List<String> events = new ArrayList<>();
        ResourceSharing.Store store =
                new ResourceSharing.Store() {
                    public boolean enabled(String id) {
                        return saved.getOrDefault(id, true);
                    }

                    public void save(String id, boolean value) {
                        saved.put(id, value);
                        events.add("saved");
                    }
                };
        ResourceSharing policy = new ResourceSharing(store);
        String mic = ResourceSharing.audioKey(true, 15, "bottom", "Pixel");
        require(policy.enabled(mic) && policy.allowed(mic, true) && !policy.allowed(mic, false));
        policy.set(
                mic,
                false,
                () -> {
                    require(!policy.allowed(mic, true));
                    events.add("revoked");
                },
                () -> events.add("published"));
        require(events.equals(List.of("saved", "revoked", "published")));
        ResourceSharing afterRestart = new ResourceSharing(store);
        require(!afterRestart.allowed(mic, true));
        // Capture IDs may change without changing the hardware policy key.
        Map<Integer, String> inventory = Map.of(5, mic);
        Map<Integer, String> refreshed =
                Map.of(99, ResourceSharing.audioKey(true, 15, "bottom", "Pixel"));
        require(
                !afterRestart.enabled(refreshed.get(99))
                        && inventory.get(5).equals(refreshed.get(99)));
        require(afterRestart.enabled(ResourceSharing.audioKey(false, 2, "", "Pixel")));
        require(!afterRestart.enabled("microphone:unavailable:99"));
        events.clear();
        afterRestart.set(
                mic,
                true,
                () -> {
                    throw new AssertionError("Enable must not start or resume a stream");
                },
                () -> events.add("published"));
        require(
                events.equals(List.of("saved", "published"))
                        && afterRestart.allowed(mic, true)
                        && !afterRestart.allowed(mic, false));
        System.out.println(
                "Resource opt-out persistence, permission separation and teardown checks passed");
    }
}
