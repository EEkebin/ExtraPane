package io.github.eekebin.extrapane;

public final class PageHistoryCheck {
    private static void expect(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void main(String[] args) {
        PageHistory history = new PageHistory();
        expect(history.back() == null);
        history.visit("Devices");
        expect(history.back() == null);
        history.visit("Resources"); history.visit("Resources");
        history.visit("About");
        expect("Resources".equals(history.back()));
        history.visit("About");
        expect("Resources".equals(history.back()));
        expect("Devices".equals(history.back()));
        expect(history.back() == null);
        System.out.println("PageHistoryCheck passed");
    }
}
