package io.github.eekebin.extrapane;

import android.app.Instrumentation;
import android.os.Bundle;
import java.nio.ByteBuffer;
import android.view.*;

/** Runs on the device against the real JNI library, without connecting or injecting input. */
public final class ProtocolChecks extends Instrumentation {
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);start();}
    private void require(boolean condition,String message){if(!condition)throw new AssertionError(message);}
    private void rejected(Runnable action){try{action.run();}catch(IllegalArgumentException expected){return;}throw new AssertionError("Invalid packet accepted");}
    private MotionEvent pen(int action,int tool,int flags){
        MotionEvent.PointerProperties p=new MotionEvent.PointerProperties();p.id=2;p.toolType=tool;
        MotionEvent.PointerCoords c=new MotionEvent.PointerCoords();c.x=50;c.y=25;c.pressure=.5f;
        c.setAxisValue(MotionEvent.AXIS_TILT,(float)Math.PI/4);c.setAxisValue(MotionEvent.AXIS_ORIENTATION,(float)Math.PI/2);
        return MotionEvent.obtain(1,2,action,1,new MotionEvent.PointerProperties[]{p},new MotionEvent.PointerCoords[]{c},0,MotionEvent.BUTTON_STYLUS_PRIMARY,1,1,10,0,InputDevice.SOURCE_STYLUS,flags);
    }
    private void inputChecks(){
        View view=new View(getTargetContext());view.layout(0,0,100,100);
        InputQueue q=new InputQueue();q.grant(15);RemoteInput r=new RemoteInput(q,(e,a)->true);r.enable(true);q.take();
        MotionEvent e=pen(MotionEvent.ACTION_DOWN,MotionEvent.TOOL_TYPE_STYLUS,0);r.motion(view,e);e.recycle();int[] p=q.take();
        require(p.length==15 && p[0]==4 && p[1]==1 && p[2]==3 && p[8]==512 && p[9]==45 && p[10]==0 && p[12]==13 && p[6]==1,"Native pen axes and barrel");Protocol.input(1,1,p);
        e=pen(MotionEvent.ACTION_MOVE,MotionEvent.TOOL_TYPE_STYLUS,0);
        MotionEvent.PointerCoords history=new MotionEvent.PointerCoords();e.getPointerCoords(0,history);history.x=70;history.pressure=.75f;e.addBatch(3,new MotionEvent.PointerCoords[]{history},0);
        r.motion(view,e);e.recycle();p=q.take();require(p.length==30 && p[8]==512 && p[23]==768 && p[4]<p[19],"Pen history retains pressure and ordering");Protocol.input(1,2,p);
        e=pen(MotionEvent.ACTION_UP,MotionEvent.TOOL_TYPE_STYLUS,MotionEvent.FLAG_CANCELED);r.motion(view,e);e.recycle();p=q.take();require(p[1]==3 && p[2]==0 && p[8]==0,"Palm cancellation");Protocol.input(1,2,p);
        e=pen(MotionEvent.ACTION_DOWN,MotionEvent.TOOL_TYPE_ERASER,0);r.motion(view,e);e.recycle();p=q.take();require(p[6]==7,"Eraser semantics");Protocol.input(1,3,p);
        r.reset();q.take();e=pen(MotionEvent.ACTION_MOVE,MotionEvent.TOOL_TYPE_STYLUS,0);r.motion(view,e);e.recycle();require(q.take().length==0,"No orphan pen contact");
        q.grant(7);e=pen(MotionEvent.ACTION_DOWN,MotionEvent.TOOL_TYPE_STYLUS,0);r.motion(view,e);e.recycle();require(q.take().length==0,"Pen never emulates mouse without grant");
        q.grant(15);r=new RemoteInput(q,(event,axis)->false);r.enable(true);q.take();e=pen(MotionEvent.ACTION_DOWN,MotionEvent.TOOL_TYPE_STYLUS,0);r.motion(view,e);e.recycle();require(q.take()[12]==0,"Absent axes omitted");
        r.reset();q.take();
        r.key(new KeyEvent(1,2,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_A,0,0,20,30));p=q.take();require(p[3]=='A' && p[6]==30,"Physical scan code");Protocol.input(1,4,p);
        r.key(new KeyEvent(1,2,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_A,0,0,21,30));q.take();
        r.key(new KeyEvent(1,2,KeyEvent.ACTION_UP,KeyEvent.KEYCODE_A,0,0,20,30));require(q.take().length==0,"Another keyboard still holds key");
        r.key(new KeyEvent(1,2,KeyEvent.ACTION_UP,KeyEvent.KEYCODE_A,0,0,21,30));require(q.take()[1]==2,"Last keyboard releases key");
        r.key(new KeyEvent(1,2,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_CTRL_RIGHT,0,0,22,0));p=q.take();require(p[6]==29 && p[7]==1,"Zero scan fallback preserves right control");Protocol.input(1,5,p);
        r.reset();require(q.take()[0]==0,"Disconnect/focus/device reset");
        r.key(new KeyEvent(1,2,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_CTRL_RIGHT,1,0,22,0));require(q.take().length==0,"No orphan repeat");
    }
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
        runOnMainSync(this::inputChecks);
        result.putString("stream","Protocol, touch, stylus, physical keyboard, capability and cleanup checks passed\n");finish(-1,result);
    }catch(Throwable error){result.putString("stream",error.toString());finish(1,result);}}
}
