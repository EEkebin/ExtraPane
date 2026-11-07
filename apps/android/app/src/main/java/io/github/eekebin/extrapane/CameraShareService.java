package io.github.eekebin.extrapane;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.Network;
import android.os.*;

/** User-started camera foreground service. Never restarts camera capture itself. */
public final class CameraShareService extends Service {
    static final String STOP="io.github.eekebin.extrapane.STOP_CAMERA";
    private static final String CHANNEL="camera-sharing";private static final int NOTIFICATION=12;
    interface Listener{void changed(String status,boolean running,boolean connected,String destination,String detail);}
    final class LocalBinder extends Binder {
        void listen(Listener listener){CameraShareService.this.listener=listener;if(listener!=null)listener.changed(status,session!=null,connected,destination,detail);}
    }
    private final LocalBinder binder=new LocalBinder();private final Handler main=new Handler(Looper.getMainLooper());
    private boolean connected;private String destination="",detail="";
    private CameraSession session;private Listener listener;private String status="Camera sharing is stopped";
    public IBinder onBind(Intent intent){return binder;}
    private Notification notification(String text){
        Intent open=new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent content=PendingIntent.getActivity(this,0,open,PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,CameraShareService.class).setAction(STOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this,CHANNEL).setSmallIcon(android.R.drawable.ic_menu_camera).setContentTitle("ExtraPane camera sharing").setContentText(text).setContentIntent(content).setOngoing(true).setCategory(Notification.CATEGORY_SERVICE).addAction(new Notification.Action.Builder(null,"Stop sharing",stop).build()).build();
    }
    private void status(String text){status=text;if(listener!=null)listener.changed(text,session!=null,connected,destination,detail);if(session!=null)getSystemService(NotificationManager.class).notify(NOTIFICATION,notification(text));}
    @SuppressWarnings("deprecation")
    public int onStartCommand(Intent intent,int flags,int startId){
        if(intent==null){stopSelf();return START_NOT_STICKY;}
        if(STOP.equals(intent.getAction())){if(session!=null)session.cancel();else stopSelf();return START_NOT_STICKY;}
        if(session!=null)return START_NOT_STICKY;
        if(checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){status("Camera permission is required");stopSelf();return START_NOT_STICKY;}
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"Camera sharing",NotificationManager.IMPORTANCE_LOW));
        try{
            // Must be launched by a visible Activity after CAMERA permission.
            startForeground(NOTIFICATION,notification("Connecting…"),ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA);
            String address=intent.getStringExtra("address"),local=intent.getStringExtra("local"),code=intent.getStringExtra("code"),camera=intent.getStringExtra("camera"),label=intent.getStringExtra("cameraLabel");
            int port=intent.getIntExtra("port",0),width=intent.getIntExtra("width",0),height=intent.getIntExtra("height",0),fps=intent.getIntExtra("fps",0);
            Network network=intent.getParcelableExtra("network");intent.removeExtra("code");
            if(address==null||local==null||code==null||camera==null||label==null||port<1||port>65535||width<2||height<2||fps<1)throw new IllegalArgumentException("Invalid camera sharing request");
            connected=false;destination=address+":"+port;detail=label + " - " + width + " x " + height + " @ " + fps + " FPS";
            session=new CameraSession(this,address,port,local,network,code,new CameraCapture.Choice(camera,label,width,height,fps),new CameraSession.Listener(){
                public void connected(){main.post(()->{if(session!=null){connected=true;CameraShareService.this.status(status);}});}
                public void status(String text){main.post(()->CameraShareService.this.status(text));}
                public void ended(String reason){main.post(()->{session=null;connected=false;CameraShareService.this.status(reason);stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();});}
            });status("Connecting camera…");session.start();
        }catch(RuntimeException error){session=null;connected=false;status(error.getMessage()==null?"Camera sharing could not start":error.getMessage());stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
        return START_NOT_STICKY;
    }
    public boolean onUnbind(Intent intent){listener=null;return true;}
    public void onDestroy(){if(session!=null)session.cancel();listener=null;super.onDestroy();}
}
