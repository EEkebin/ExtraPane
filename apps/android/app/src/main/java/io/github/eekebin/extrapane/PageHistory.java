package io.github.eekebin.extrapane;

import java.util.ArrayDeque;

/** Page visits only: rerenders and fullscreen changes must not create Back entries. */
final class PageHistory {
    private final ArrayDeque<String> previous = new ArrayDeque<>();
    private String current = "Devices";

    String current() {
        return current;
    }

    void visit(String page) {
        if (current.equals(page)) return;
        previous.addLast(current);
        current = page;
    }

    String back() {
        if (previous.isEmpty()) return null;
        current = previous.removeLast();
        return current;
    }
}
