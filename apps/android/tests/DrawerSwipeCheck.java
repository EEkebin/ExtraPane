package io.github.eekebin.extrapane;

public final class DrawerSwipeCheck {
    private static void expect(boolean value) {
        if (!value) throw new AssertionError();
    }

    public static void main(String[] args) {
        DrawerSwipe d = new DrawerSwipe();
        d.begin(0, 0, true, 0, 300);
        expect(!d.move(4, 1, 1, 8));
        expect(d.move(60, 2, 1, 8));
        expect(Math.abs(d.progress() - .2f) < .001);
        expect(!d.release(0, 400));
        d.begin(0, 0, true, 0, 300);
        expect(d.move(180, 3, 1, 8));
        expect(d.release(0, 400));
        d.begin(300, 0, true, 1, 300);
        expect(d.move(240, 2, 1, 8));
        expect(Math.abs(d.progress() - .8f) < .001);
        expect(!d.release(-500, 400));
        d.begin(0, 0, true, 0, 300);
        d.move(30, 1, 1, 8);
        expect(d.release(500, 400));
        d.begin(0, 0, false, 0, 300);
        expect(!d.move(100, 0, 1, 8));
        d.begin(0, 0, true, 0, 300);
        expect(!d.move(0, 20, 1, 8));
        expect(!d.move(100, 0, 1, 8));
        d.begin(0, 0, true, 0, 300);
        expect(!d.move(-20, 0, 1, 8));
        expect(!d.move(100, 0, 1, 8));
        d.begin(0, 0, true, 0, 300);
        expect(!d.move(100, 0, 2, 8));
        d.begin(0, 0, true, 0, 300);
        expect(d.move(400, 0, 1, 8));
        expect(d.progress() == 1);
        expect(d.move(20, 0, 1, 8));
        expect(d.progress() < .1);
        d.cancel();
        expect(!d.move(100, 0, 1, 8));
        System.out.println("Drawer drag checks passed");
    }
}
