package io.github.eekebin.extrapane;

/** Tracks a drawer continuously; only the release chooses an open/closed target. */
final class DrawerSwipe {
    private float x, y, start, width, progress;
    private boolean eligible, dragging;
    void begin(float x,float y,boolean eligible,float progress,float width){
        this.x=x;this.y=y;this.eligible=eligible;this.start=this.progress=progress;
        this.width=Math.max(1,width);dragging=false;
    }
    boolean move(float x,float y,int pointers,float slop){
        if(!eligible)return false;
        float dx=x-this.x,dy=Math.abs(y-this.y);
        if(pointers!=1){cancel();return false;}
        if(!dragging){
            if(dy>slop&&dy>=Math.abs(dx)){cancel();return false;}
            if(Math.abs(dx)<=slop)return false;
            if(start==0&&dx<0){cancel();return false;}
            dragging=true;
        }
        progress=Math.max(0,Math.min(1,start+dx/width));
        return true;
    }
    float progress(){return progress;}
    boolean release(float velocity,float flingThreshold){
        boolean open=Math.abs(velocity)>=flingThreshold?velocity>0:progress>=.5f;
        cancel();return open;
    }
    void cancel(){eligible=false;dragging=false;}
}
