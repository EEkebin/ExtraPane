package io.github.eekebin.extrapane;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.camera2.*;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.*;
import android.os.*;
import android.util.Range;
import android.util.Size;
import android.view.Surface;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;

/** Camera2 surface capture into MediaCodec H.264. Ownership stays on Android. */
final class CameraCapture implements AutoCloseable {
    private static final AtomicInteger pendingCleanup=new AtomicInteger();
    record Choice(String id,String label,int width,int height,int fps) { public String toString(){return label+" · "+width+" × "+height+" · "+fps+" FPS";} }
    record Packet(byte[] bytes,long timestamp) {}
    static List<Choice> choices(Context context)throws CameraAccessException {
        CameraManager manager=context.getSystemService(CameraManager.class);ArrayList<Choice> choices=new ArrayList<>();
        for(String id:manager.getCameraIdList()){
            CameraCharacteristics details=manager.getCameraCharacteristics(id);StreamConfigurationMap map=details.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
            Range<Integer>[] ranges=details.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);if(map==null||ranges==null)continue;
            Size[] sizes=map.getOutputSizes(MediaCodec.class);if(sizes==null)continue;
            boolean rate=false;for(Range<Integer> range:ranges)if(range.contains(30))rate=true;if(!rate)continue;
            for(Size preferred:new Size[]{new Size(1280,720),new Size(640,480),new Size(1920,1080)}){
                if(!Arrays.asList(sizes).contains(preferred)||encoder(preferred.getWidth(),preferred.getHeight(),30)==null)continue;
                Integer facing=details.get(CameraCharacteristics.LENS_FACING);String label=Objects.equals(facing,CameraCharacteristics.LENS_FACING_FRONT)?"Front camera":Objects.equals(facing,CameraCharacteristics.LENS_FACING_BACK)?"Back camera":"External camera";
                choices.add(new Choice(id,label,preferred.getWidth(),preferred.getHeight(),30));break;
            }
        }
        return choices;
    }
    private static MediaCodecInfo encoder(int width,int height,int fps){
        MediaCodecInfo chosen=null;
        for(MediaCodecInfo codec:new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos())if(codec.isEncoder()){
            for(String type:codec.getSupportedTypes())if(type.equalsIgnoreCase("video/avc")){
                MediaCodecInfo.CodecCapabilities caps=codec.getCapabilitiesForType(type);boolean surface=false;for(int color:caps.colorFormats)if(color==MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)surface=true;
                if(surface&&caps.getVideoCapabilities().areSizeAndRateSupported(width,height,fps)&&(chosen==null||codec.isHardwareAccelerated()))chosen=codec;
            }
            if(chosen!=null&&chosen.isHardwareAccelerated())break;
        }
        return chosen;
    }
    private final Context context;private final Choice choice;private final Consumer<Exception> onFailure;
    private final ArrayBlockingQueue<Packet> frames=new ArrayBlockingQueue<>(3);
    private final AtomicReference<Exception> failure=new AtomicReference<>();
    private final Object lifecycle=new Object();
    private final CountDownLatch ready=new CountDownLatch(1),cameraClosed=new CountDownLatch(1);
    private final CountDownLatch deviceStopped=new CountDownLatch(1);
    private volatile boolean stopped;private volatile CameraDevice camera;private volatile CameraCaptureSession session;
    private boolean cleanupPending,cleanupFinished;
    private MediaCodec codec;private Surface surface;private HandlerThread callbacks;private Thread drain;
    CameraCapture(Context context,Choice choice,Consumer<Exception> onFailure){this.context=context.getApplicationContext();this.choice=choice;this.onFailure=onFailure;}
    private void fail(Exception error){if(!stopped&&failure.compareAndSet(null,error)){ready.countDown();onFailure.accept(error);}}
    @android.annotation.SuppressLint("MissingPermission") // Runtime check immediately before opening; caller started a visible camera FGS.
    void start()throws Exception {
        if(pendingCleanup.get()!=0)throw new IOException("The previous Android camera encoder is still stopping; wait before reconnecting");
        if(context.checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED)throw new IOException("Camera permission was not granted");
        MediaCodecInfo selected=encoder(choice.width,choice.height,choice.fps);if(selected==null)throw new IOException("No H.264 encoder supports this camera mode");
        callbacks=new HandlerThread("ExtraPane camera callbacks");callbacks.start();Handler handler=new Handler(callbacks.getLooper());
        codec=MediaCodec.createByCodecName(selected.getName());MediaFormat format=MediaFormat.createVideoFormat("video/avc",choice.width,choice.height);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT,MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);format.setInteger(MediaFormat.KEY_FRAME_RATE,choice.fps);
        format.setInteger(MediaFormat.KEY_BIT_RATE,Math.min(12000000,Math.max(2000000,choice.width*choice.height*4)));format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL,1);
        format.setInteger(MediaFormat.KEY_MAX_B_FRAMES,0);format.setInteger(MediaFormat.KEY_PRIORITY,0);
        codec.configure(format,null,null,MediaCodec.CONFIGURE_FLAG_ENCODE);surface=codec.createInputSurface();codec.start();
        drain=new Thread(this::drain,"ExtraPane camera encoder");drain.start();
        CameraManager manager=context.getSystemService(CameraManager.class);
        CameraCharacteristics characteristics=manager.getCameraCharacteristics(choice.id);Range<Integer> selectedRange=null;
        Range<Integer>[] ranges=characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);
        if(ranges!=null)for(Range<Integer> range:ranges)if(range.contains(choice.fps)&&(selectedRange==null||range.getUpper()-range.getLower()<selectedRange.getUpper()-selectedRange.getLower()))selectedRange=range;
        if(selectedRange==null)throw new IOException("Camera frame-rate support changed");final Range<Integer> range=selectedRange;
        manager.openCamera(choice.id,new CameraDevice.StateCallback(){
            public void onOpened(CameraDevice device){synchronized(lifecycle){if(stopped){device.close();return;}camera=device;}
                try{device.createCaptureSession(Collections.singletonList(surface),new CameraCaptureSession.StateCallback(){
                    public void onConfigured(CameraCaptureSession configured){synchronized(lifecycle){if(stopped){configured.close();return;}session=configured;}
                        try{CaptureRequest.Builder request=device.createCaptureRequest(CameraDevice.TEMPLATE_RECORD);request.addTarget(surface);request.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE,range);configured.setRepeatingRequest(request.build(),null,handler);ready.countDown();}catch(Exception e){fail(e);}}
                    public void onConfigureFailed(CameraCaptureSession configured){fail(new IOException("Camera capture session could not start"));}
                },handler);}catch(Exception e){fail(e);}}
            public void onDisconnected(CameraDevice device){device.close();fail(new IOException("Camera disconnected or became unavailable"));}
            public void onError(CameraDevice device,int error){device.close();fail(new IOException("Camera unavailable (Android error "+error+"); another application or privacy setting may be using it"));}
            public void onClosed(CameraDevice device){cameraClosed.countDown();}
        },handler);
        if(!ready.await(8,TimeUnit.SECONDS))throw new IOException("Camera startup timed out");if(failure.get()!=null)throw failure.get();if(stopped)throw new IOException("Camera sharing stopped");
    }
    private void drain(){
        byte[] configuration=new byte[0];boolean first=true;long previous=-1;
        try{MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();while(!stopped){
            int index=codec.dequeueOutputBuffer(info,10000);
            if(index==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED){MediaFormat output=codec.getOutputFormat();if(output.getInteger(MediaFormat.KEY_WIDTH)!=choice.width||output.getInteger(MediaFormat.KEY_HEIGHT)!=choice.height)throw new IOException("Camera encoder changed the negotiated dimensions");byte[] csd=new byte[0];for(String key:new String[]{"csd-0","csd-1"}){ByteBuffer data=output.getByteBuffer(key);if(data!=null&&data.hasRemaining())csd=CameraPackets.join(csd,CameraPackets.annexB(CameraPackets.copy(data,data.position(),data.remaining())));}if(csd.length>0)configuration=csd;continue;}
            if(index<0)continue;
            try{
                if((info.flags&MediaCodec.BUFFER_FLAG_END_OF_STREAM)!=0)throw new IOException("Camera encoder ended unexpectedly");if(info.size==0)continue;
                byte[] packet=CameraPackets.annexB(CameraPackets.copy(codec.getOutputBuffer(index),info.offset,info.size));
                if((info.flags&MediaCodec.BUFFER_FLAG_CODEC_CONFIG)!=0){if(packet.length>65536)throw new IOException("Camera codec configuration is too large");configuration=packet;continue;}
                boolean key=(info.flags&MediaCodec.BUFFER_FLAG_KEY_FRAME)!=0;
                if(first&&!key)throw new IOException("Camera encoder did not start with a key frame");
                if(key&&configuration.length>0)packet=CameraPackets.join(configuration,packet);
                if(info.presentationTimeUs<0||info.presentationTimeUs>Long.MAX_VALUE/10||info.presentationTimeUs<previous)throw new IOException("Invalid camera capture timestamp");previous=info.presentationTimeUs;
                if(!frames.offer(new Packet(packet,info.presentationTimeUs*10)))throw new IOException("Camera connection is too slow; sharing stopped to avoid queued latency");first=false;
            }finally{codec.releaseOutputBuffer(index,false);}
        }}catch(Exception e){fail(e);}
        finally{
            // The drain thread is the sole codec/buffer owner after start. Never
            // release its buffers from close() while a vendor codec call is in
            // progress. A stuck codec retains only this bounded source until it
            // returns; cleanup timeout is reported to the UI.
            boolean waiting=true;while(waiting)try{deviceStopped.await();waiting=false;}catch(InterruptedException e){Thread.interrupted();}
            releaseEncoder();
            synchronized(lifecycle){cleanupFinished=true;if(cleanupPending){cleanupPending=false;pendingCleanup.decrementAndGet();}}
        }
    }
    Packet next()throws Exception{if(failure.get()!=null)throw failure.get();Packet packet=frames.poll(500,TimeUnit.MILLISECONDS);if(failure.get()!=null)throw failure.get();return packet;}
    private void releaseEncoder(){
        if(codec!=null){try{codec.stop();}catch(RuntimeException ignored){}try{codec.release();}catch(RuntimeException ignored){}codec=null;}
        if(surface!=null){surface.release();surface=null;}
    }
    public void close()throws IOException{
        CameraCaptureSession current;CameraDevice device;synchronized(lifecycle){stopped=true;current=session;device=camera;}ready.countDown();IOException closingFailure=null;
        if(current!=null)try{current.close();}catch(RuntimeException error){closingFailure=new IOException("Android camera session cleanup failed",error);}
        if(device!=null){try{device.close();}catch(RuntimeException error){closingFailure=new IOException("Android camera device cleanup failed",error);}try{cameraClosed.await(2,TimeUnit.SECONDS);}catch(InterruptedException e){Thread.interrupted();}}
        deviceStopped.countDown();if(drain!=null){try{drain.join(2000);}catch(InterruptedException e){Thread.interrupted();}}else releaseEncoder();
        if(callbacks!=null){callbacks.quitSafely();try{callbacks.join(1000);}catch(InterruptedException e){Thread.interrupted();}}
        frames.clear();
        synchronized(lifecycle){if(drain!=null&&!cleanupFinished){if(!cleanupPending){cleanupPending=true;pendingCleanup.incrementAndGet();}throw new IOException("Camera stopped, but Android's encoder is still finishing cleanup");}}
        if(closingFailure!=null)throw closingFailure;
    }
}
