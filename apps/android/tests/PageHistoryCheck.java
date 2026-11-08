package io.github.eekebin.extrapane;

public final class PageHistoryCheck {
    private static void expect(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void main(String[] args) {
        PageHistory history = new PageHistory();
        expect(history.back() == null);
        history.visit("Devices");
        expect(history.back() == null);
        history.visit("Share"); history.visit("Share");
        history.visit("Connections"); history.visit("About");
        expect("Connections".equals(history.back()));
        expect("Share".equals(history.back()));
        history.visit("About");
        expect("Share".equals(history.back()));
        expect("Devices".equals(history.back()));
        expect(history.back() == null);
        System.out.println("PageHistoryCheck passed");
    }
}
