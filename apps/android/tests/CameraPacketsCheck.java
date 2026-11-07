package io.github.eekebin.extrapane;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
public final class CameraPacketsCheck {
    interface Action{void run()throws Exception;}
    static void check(boolean value){if(!value)throw new AssertionError("Camera packet check failed");}
    static void rejects(Action action)throws Exception{try{action.run();throw new AssertionError("Invalid camera packet accepted");}catch(IOException expected){}}
    public static void main(String[] args)throws Exception{
        byte[] avcc={0,0,0,2,0x67,1,0,0,0,2,0x68,2};
        byte[] annex={0,0,0,1,0x67,1,0,0,0,1,0x68,2};check(Arrays.equals(CameraPackets.annexB(avcc),annex));
        check(Arrays.equals(CameraPackets.annexB(annex),annex));
        rejects(()->CameraPackets.annexB(new byte[]{0,0,0,20,1}));rejects(()->CameraPackets.annexB(new byte[]{2,3,4}));
        check(Arrays.equals(CameraPackets.copy(ByteBuffer.wrap(new byte[]{8,9,10,11}),1,2),new byte[]{9,10}));
        rejects(()->CameraPackets.copy(ByteBuffer.allocate(4),3,2));
        rejects(()->CameraPackets.join(new byte[65537],new byte[]{1}));rejects(()->CameraPackets.join(new byte[10],new byte[CameraPackets.MAX_FRAME]));
        System.out.println("Camera AVC normalization, buffer bounds and packet limits passed");
    }
}
