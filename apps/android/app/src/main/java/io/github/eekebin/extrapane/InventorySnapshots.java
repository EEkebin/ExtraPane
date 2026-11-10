package io.github.eekebin.extrapane;

import java.util.*;

/**
 * A failed query is unknown, not an empty device list. Null results retain the last complete
 * catalog for that kind; a successful empty result removes it.
 */
final class InventorySnapshots {
    static <T> Map<Integer, List<T>> merge(
            Map<Integer, List<T>> previous, Map<Integer, List<T>> queried) {
        Map<Integer, List<T>> next = new LinkedHashMap<>(previous);
        for (var entry : queried.entrySet())
            if (entry.getValue() != null) next.put(entry.getKey(), List.copyOf(entry.getValue()));
        return next;
    }
}
