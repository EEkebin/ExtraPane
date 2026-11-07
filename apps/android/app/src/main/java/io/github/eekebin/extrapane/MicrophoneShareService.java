package io.github.eekebin.extrapane;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.Network;
import android.os.*;

/** Visible-activity initiated microphone foreground service. Never auto-restarts. */
public final class MicrophoneShareService extends Service {
    static final String STOP="io.github.eekebin.extrapane.STOP_MICROPHONE",MUTE="io.github.eekebin.extrapane.MUTE_MICROPHONE",UNMUTE="io.github.eekebin.extrapane.UNMUTE_MICROPHONE";
    private static final String CHANNEL="microphone-sharing";private static final int NOTIFICATION=13;
    interface Listener{void changed(String status,boolean running,boolean connected,String destination,String detail);}
    final class LocalBinder extends Binder{boolean muted(){return muted;}void listen(Listener value){listener=value;if(value!=null)value.changed(status,session!=null,connected,destination,detail);}}
    private final LocalBinder binder=new LocalBinder();private final Handler main=new Handler(Looper.getMainLooper());
    private boolean connected;private String destination="",detail="";
    private MicrophoneSession session;private Listener listener;private String status="Microphone sharing is stopped";private boolean muted;
    public IBinder onBind(Intent intent){return binder;}
    private PendingIntent action(String action,int id){return PendingIntent.getService(this,id,new Intent(this,MicrophoneShareService.class).setAction(action),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);}
    private Notification notification(String text){
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this,CHANNEL).setSmallIcon(android.R.drawable.ic_btn_speak_now).setContentTitle(muted?"ExtraPane microphone muted":"ExtraPane microphone sharing").setContentText(text).setContentIntent(open).setOngoing(true).setCategory(Notification.CATEGORY_SERVICE)
            .addAction(new Notification.Action.Builder(null,muted?"Unmute":"Mute",action(muted?UNMUTE:MUTE,3)).build())
            .addAction(new Notification.Action.Builder(null,"Stop sharing",action(STOP,2)).build()).build();
    }
    private void status(String text){status=text;if(listener!=null)listener.changed(text,session!=null,connected,destination,detail);if(session!=null)getSystemService(NotificationManager.class).notify(NOTIFICATION,notification(text));}
    @SuppressWarnings("deprecation")
    public int onStartCommand(Intent intent,int flags,int startId){
        if(intent==null){stopSelf();return START_NOT_STICKY;}
        if(STOP.equals(intent.getAction())){if(session!=null)session.cancel();else stopSelf();return START_NOT_STICKY;}
        if(MUTE.equals(intent.getAction())||UNMUTE.equals(intent.getAction())){if(session!=null){muted=MUTE.equals(intent.getAction());session.mute(muted);status(muted?"Microphone is muted":"Microphone sharing resumed");}else stopSelf();return START_NOT_STICKY;}
        if(session!=null)return START_NOT_STICKY;
        if(checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){status("Microphone permission is required");stopSelf();return START_NOT_STICKY;}
        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"Microphone sharing",NotificationManager.IMPORTANCE_LOW));
        try{
            startForeground(NOTIFICATION,notification("Connecting..."),ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE);
            String address=intent.getStringExtra("address"),local=intent.getStringExtra("local"),code=intent.getStringExtra("code"),label=intent.getStringExtra("microphoneLabel");
            int port=intent.getIntExtra("port",0),device=intent.getIntExtra("microphone",-1);Network network=intent.getParcelableExtra("network");intent.removeExtra("code");
            if(address==null||local==null||code==null||label==null||device<0||port<1||port>65535)throw new IllegalArgumentException("Invalid microphone sharing request");
            connected=false;destination=address+":"+port;detail=label + " - 48 kHz mono";
            muted=false;session=new MicrophoneSession(this,address,port,local,network,code,new MicrophoneCapture.Choice(device,label),new MicrophoneSession.Listener(){
                public void connected(){main.post(()->{if(session!=null){connected=true;MicrophoneShareService.this.status(status);}});}
                public void status(String text){main.post(()->MicrophoneShareService.this.status(text));}
                public void ended(String reason){main.post(()->{session=null;connected=false;MicrophoneShareService.this.status(reason);stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();});}
            });status("Connecting microphone...");session.start();
        }catch(RuntimeException error){session=null;connected=false;status(error.getMessage()==null?"Microphone sharing could not start":error.getMessage());stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
        return START_NOT_STICKY;
    }
    public boolean onUnbind(Intent intent){listener=null;return true;}
    public void onDestroy(){if(session!=null)session.cancel();listener=null;super.onDestroy();}
}
