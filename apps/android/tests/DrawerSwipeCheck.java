package io.github.eekebin.extrapane;
public final class DrawerSwipeCheck {
    private static void expect(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args){
        DrawerSwipe swipe=new DrawerSwipe();
        swipe.begin(0,0,true); expect(!swipe.move(20,2,1,8,64)); expect(swipe.move(70,3,1,8,64)); expect(!swipe.move(90,3,1,8,64));
        swipe.begin(0,0,false); expect(!swipe.move(100,0,1,8,64));
        swipe.begin(0,0,true); expect(!swipe.move(0,20,1,8,64)); expect(!swipe.move(100,0,1,8,64));
        swipe.begin(0,0,true); expect(!swipe.move(-20,0,1,8,64)); expect(!swipe.move(100,0,1,8,64));
        swipe.begin(0,0,true); expect(!swipe.move(100,0,2,8,64));
        swipe.begin(0,0,true); swipe.cancel(); expect(!swipe.move(100,0,1,8,64));
        System.out.println("DrawerSwipeCheck passed");
    }
}
