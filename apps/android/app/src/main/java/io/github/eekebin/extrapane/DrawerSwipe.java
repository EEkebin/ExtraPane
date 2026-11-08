package io.github.eekebin.extrapane;

/** One-finger, rightward gesture starting on non-interactive page space. */
final class DrawerSwipe {
    private float x, y;
    private boolean eligible;
    void begin(float x, float y, boolean eligible) { this.x=x; this.y=y; this.eligible=eligible; }
    void cancel() { eligible=false; }
    boolean move(float x, float y, int pointers, float slop, float distance) {
        if (!eligible) return false;
        float dx=x-this.x, dy=Math.abs(y-this.y);
        if (pointers!=1 || (dy>slop && dy>=Math.abs(dx)) || dx < -slop) { cancel(); return false; }
        if (dx>=distance && dx>dy*2) { cancel(); return true; }
        return false;
    }
}

