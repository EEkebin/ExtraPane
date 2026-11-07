package io.github.eekebin.extrapane;

import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.Network;
import android.os.*;
import java.util.*;

/** Owns capture workers until teardown, independently of the Activity or its rows. */
abstract class CaptureShareService extends Service {
    static final String STOP="capture.stop",MUTE="capture.mute",UNMUTE="capture.unmute";
    record Snapshot(String id,boolean active,boolean connected,boolean stopping,boolean muted,String status,String destination,String detail,Intent configuration) {}
    interface Listener {void changed(List<Snapshot> snapshots);}
    interface Worker {void cancel();default void mute(boolean value){}}
    private static final CaptureRegistry.Sources sourceOwners=new CaptureRegistry.Sources(); // main thread, survives service destruction
    private final CaptureRegistry registry=new CaptureRegistry(sourceOwners);
    private final LinkedHashMap<String,Entry> entries=new LinkedHashMap<>();
    private final Handler main=new Handler(Looper.getMainLooper());
    private Listener listener;private boolean destroyed;
    private final LocalBinder binder=new LocalBinder();
    abstract boolean camera();
    final class LocalBinder extends Binder {
        void listen(Listener value){listener=value;publish();}
        List<Snapshot> snapshots(){return CaptureShareService.this.snapshots();}
    }
    private final class Entry {
        String id,key,status="Connecting...",destination,detail;Intent configuration;
        boolean active,connected,stopping,muted;Worker worker;CaptureRegistry.Lease lease;
        Snapshot snapshot(){return new Snapshot(id,active,connected,stopping,muted,status,destination,detail,new Intent(configuration));}
    }
    public IBinder onBind(Intent intent){return binder;}
    private String kind(){return camera()?"Camera":"Microphone";}
    private String channel(){return camera()?"camera-sharing":"microphone-sharing";}
    private int notificationId(){return camera()?12:13;}
    private boolean active(){for(Entry e:entries.values())if(e.active)return true;return false;}
    private List<Snapshot> snapshots(){ArrayList<Snapshot> result=new ArrayList<>();for(Entry e:entries.values())result.add(e.snapshot());return result;}
    private Notification notification(){
        long count=entries.values().stream().filter(e->e.active).count();
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,notificationId(),new Intent(this,getClass()).setAction(STOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this,channel()).setSmallIcon(camera()?android.R.drawable.ic_menu_camera:android.R.drawable.ic_btn_speak_now).setContentTitle("ExtraPane "+kind().toLowerCase(Locale.ROOT)+" sharing").setContentText(count+" active connection(s)").setContentIntent(open).setOngoing(true).setCategory(Notification.CATEGORY_SERVICE).addAction(new Notification.Action.Builder(null,"Stop all "+kind().toLowerCase(Locale.ROOT)+"s",stop).build()).build();
    }
    private void publish(){if(destroyed)return;if(listener!=null)listener.changed(snapshots());if(active())getSystemService(NotificationManager.class).notify(notificationId(),notification());}
    private void ended(Entry e,String reason){
        registry.finish(e.lease);
        e.active=false;e.connected=false;e.stopping=false;e.worker=null;e.status=reason;
        if(entries.get(e.id)==e)publish();
        if(!active()&&!destroyed){stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
    }
    private void status(Entry e,String text){if(entries.get(e.id)==e&&e.active&&registry.current(e.lease)){e.status=text;publish();}}
    private void connected(Entry e){if(entries.get(e.id)==e&&e.active&&!e.stopping&&registry.current(e.lease)){e.connected=true;publish();}}
    private void action(String id,String action){
        if(STOP.equals(action))registry.stop(id);
        for(Entry e:entries.values())if((id==null||e.id.equals(id))&&e.active){
            if(STOP.equals(action)){e.stopping=true;e.status="Stopping...";if(e.worker!=null)e.worker.cancel();}
            else if(!camera()&&e.worker!=null){e.muted=MUTE.equals(action);e.worker.mute(e.muted);}
        }
        publish();if(!active())stopSelf();
    }
    @SuppressWarnings("deprecation")
    public int onStartCommand(Intent intent,int flags,int startId){
        if(intent==null){if(!active())stopSelf();return START_NOT_STICKY;}
        String action=intent.getAction();if(STOP.equals(action)||MUTE.equals(action)||UNMUTE.equals(action)){action(intent.getStringExtra("row"),action);return START_NOT_STICKY;}
        String id=intent.getStringExtra("row");if(id==null||id.isEmpty())id="legacy";
        Entry previous=entries.get(id);if(previous!=null&&previous.active)return START_NOT_STICKY;
        Entry e=new Entry();e.id=id;e.configuration=new Intent(intent);e.configuration.removeExtra("code");
        e.destination=intent.getStringExtra("address")+":"+intent.getIntExtra("port",0);e.detail=camera()?intent.getStringExtra("cameraLabel"):intent.getStringExtra("microphoneLabel");entries.put(id,e);
        // Bound service history is only for live UI rows, never an unbounded connection log.
        if(entries.size()>128){Iterator<Entry> old=entries.values().iterator();while(entries.size()>128&&old.hasNext()){Entry candidate=old.next();if(candidate!=e&&!candidate.active)old.remove();}}
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(channel(),kind()+" sharing",NotificationManager.IMPORTANCE_LOW));
        try{
            startForeground(notificationId(),notification(),camera()?ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA:ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
            String permission=camera()?android.Manifest.permission.CAMERA:android.Manifest.permission.RECORD_AUDIO;
            if(checkSelfPermission(permission)!=PackageManager.PERMISSION_GRANTED)throw new IllegalArgumentException(kind()+" permission is required");
            String address=intent.getStringExtra("address"),local=intent.getStringExtra("local"),code=intent.getStringExtra("code");int port=intent.getIntExtra("port",0);Network network=intent.getParcelableExtra("network");intent.removeExtra("code");
            if(address==null||local==null||code==null||!ConnectionValidation.ipv4(address)||port<1||port>65535)throw new IllegalArgumentException("Invalid sharing destination");
            e.key=(camera()?"camera:":"microphone:")+(camera()?intent.getStringExtra("camera"):intent.getIntExtra("microphone",-1));
            e.lease=registry.beginShared(id,e.key);
            if(camera()){
                String source=intent.getStringExtra("camera"),label=intent.getStringExtra("cameraLabel");int width=intent.getIntExtra("width",0),height=intent.getIntExtra("height",0),fps=intent.getIntExtra("fps",0);
                if(source==null||label==null||width<2||height<2||fps<1)throw new IllegalArgumentException("Invalid camera selection");
                e.detail=label+" - "+width+" x "+height+" @ "+fps+" FPS";
                CameraSession session=new CameraSession(this,address,port,local,network,code,new CameraCapture.Choice(source,label,width,height,fps),new CameraSession.Listener(){public void connected(){main.post(()->CaptureShareService.this.connected(e));}public void status(String text){main.post(()->CaptureShareService.this.status(e,text));}public void ended(String reason){main.post(()->CaptureShareService.this.ended(e,reason));}});
                e.worker=session::cancel;e.active=true;publish();session.start();
            }else{
                int source=intent.getIntExtra("microphone",-1);String label=intent.getStringExtra("microphoneLabel");if(source<0||label==null)throw new IllegalArgumentException("Invalid microphone selection");e.detail=label+" - 48 kHz mono";
                MicrophoneSession session=new MicrophoneSession(this,address,port,local,network,code,new MicrophoneCapture.Choice(source,label),new MicrophoneSession.Listener(){public void connected(){main.post(()->CaptureShareService.this.connected(e));}public void status(String text){main.post(()->CaptureShareService.this.status(e,text));}public void ended(String reason){main.post(()->CaptureShareService.this.ended(e,reason));}});
                e.worker=new Worker(){public void cancel(){session.cancel();}public void mute(boolean value){session.mute(value);}};e.active=true;publish();session.start();
            }
        }catch(RuntimeException error){ended(e,error.getMessage()==null?"Capture could not start":error.getMessage());}
        return START_NOT_STICKY;
    }
    public boolean onUnbind(Intent intent){listener=null;return true;}
    public void onRebind(Intent intent){publish();}
    public void onDestroy(){destroyed=true;listener=null;registry.stop(null);for(Entry e:entries.values())if(e.worker!=null)e.worker.cancel();super.onDestroy();}
}
