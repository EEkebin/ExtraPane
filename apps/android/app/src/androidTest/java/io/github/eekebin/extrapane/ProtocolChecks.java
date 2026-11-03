package io.github.eekebin.extrapane;

import android.app.Instrumentation;
import android.os.Bundle;
import java.nio.ByteBuffer;

/** Runs on the device against the real JNI library, without connecting or injecting input. */
public final class ProtocolChecks extends Instrumentation {
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    private void require(boolean condition,String message){if(!condition)throw new AssertionError(message);}
    private void rejected(Runnable action){try{action.run();}catch(IllegalArgumentException expected){return;}throw new AssertionError("Invalid packet accepted");}
    @Override public void onStart(){Bundle result=new Bundle();try{
        byte[] request=Protocol.request(1080,2400,60,7,0);
        require(request.length==32 && request[4]==9 && request[8]==7,"Request version/grant");
        require(ByteBuffer.wrap(request).getInt(16)==1080,"Request dimensions");
        rejected(()->Protocol.request(0,2400,60,7,0));rejected(()->Protocol.request(1080,2400,60,16,0));
        byte[] offer=ByteBuffer.allocate(32).put(new byte[]{'D','M','V','O',9,1,2,7}).putInt(1920).putInt(1080).putInt(60).putInt(0).putLong(1234).array();
        require(Protocol.offer(offer)[6]==1234,"Input epoch");offer[4]=8;rejected(()->Protocol.offer(offer));
        byte[] frame=ByteBuffer.allocate(24).put(new byte[]{'D','M','V','F'}).putInt(4).putInt(200).putLong(1000).putInt(0).array();
        require(Protocol.frame(frame)[1]==200,"Frame size");frame[23]=1;rejected(()->Protocol.frame(frame));
        int[] contact=new int[30];for(int i=0;i<2;i++){int o=i*15;contact[o]=3;contact[o+1]=1;contact[o+2]=3;contact[o+3]=i+1;contact[o+4]=32000;contact[o+5]=16000;contact[o+8]=512;contact[o+12]=4;}
        byte[] input=Protocol.input(1234,4,contact);require(input.length==160,"Two contacts encoded");require(ByteBuffer.wrap(input).getInt(20)==2,"Input header count");
        require(ByteBuffer.wrap(input).getInt(36)==1 && ByteBuffer.wrap(input).getInt(100)==2,"Distinct contact IDs");
        contact[2]=0;rejected(()->Protocol.input(1234,4,contact));rejected(()->Protocol.input(0,4,new int[0]));
        InputQueue queue=new InputQueue();queue.grant(1);int[] touch=new int[15];touch[0]=3;queue.add(touch);require(queue.take().length==0,"Capability gating");
        queue.grant(7);for(int i=0;i<129;i++){int[] key=new int[15];key[0]=2;key[1]=1;queue.add(key);}boolean overflow=false;try{queue.take();}catch(IllegalStateException expected){overflow=true;}require(overflow,"Queue overflow detected");
        queue.grant(7);queue.add(new int[15]);queue.reset();require(queue.take().length==15,"Reset releases owned input");
        result.putString("stream","Protocol, touch serialization, capability and overflow checks passed\n");finish(-1,result);
    }catch(Throwable error){result.putString("stream",error.toString());finish(1,result);}}
}
