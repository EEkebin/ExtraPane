package io.github.eekebin.extrapane;

import android.content.Context;
import android.media.*;
import android.net.Network;
import java.io.*;
import java.net.*;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.*;
import javax.net.ssl.*;

/** Receives server render audio into exactly the selected Android output. */
final class SpeakerSession extends Thread {
    record Choice(int id,String label){public String toString(){return label;}}
    interface Listener {void status(String text);void connected();void ended(String reason);}
    static List<Choice> choices(Context context){List<Choice> result=new ArrayList<>();for(AudioRouteCatalog.Entry route:AndroidAudioDevices.choices(context,false))result.add(new Choice(route.id(),route.label()));return result;}
    private final Context context;private final String address,local;private final int port;private String code;
    private final Network network;private final Choice choice;private final Listener listener;
    private volatile boolean cancelled,muted;private volatile int volume=100;private volatile Socket socket;
    private final SocketCancellation cancellation=new SocketCancellation();private volatile String admissionLost;private volatile Exception playbackFailure;private volatile Output active;
    SpeakerSession(Context context,String address,int port,String local,Network network,String code,Choice choice,Listener listener){super("ExtraPane speaker connection");this.context=context.getApplicationContext();this.address=address;this.port=port;this.local=local;this.network=network;this.code=code;this.choice=choice;this.listener=listener;}
    void cancel(){cancelled=true;cancellation.close(socket);interrupt();Output output=active;if(output!=null)output.stop();}
    synchronized void mute(boolean value){muted=value;Output output=active;if(output!=null)output.localMute(value);}
    synchronized void volume(int value){volume=Math.max(0,Math.min(100,value));Output output=active;if(output!=null)output.volume(volume);}
    private static long id(SecureRandom random){long value;do{value=random.nextLong()&Long.MAX_VALUE;}while(value==0);return value;}
    public void run(){
        String ending="Speaker connection stopped";AccessChannel access=null;AdmissionBroker.Lease lease=null;Output output=null;Timer watchdog=new Timer("ExtraPane speaker deadline",true);
        try{
            lease=AdmissionBroker.acquire(address,port,local,network,code,4,()->{admissionLost="Server admission ended";cancel();},()->cancelled);code=lease.credential;
            Socket raw=new Socket();socket=raw;if(cancelled)throw new IOException("Speaker connection stopped");if(network!=null)network.bindSocket(raw);if(!local.isEmpty())raw.bind(new InetSocketAddress(ClientSession.ipv4(local),0));
            raw.setTcpNoDelay(true);raw.connect(new InetSocketAddress(ClientSession.ipv4(address),port),8000);raw.setSoTimeout(10000);listener.status("Connecting speakers to "+address+"...");
            SSLContext contextTls=SSLContext.getInstance("TLSv1.3");contextTls.init(null,new TrustManager[]{new X509TrustManager(){public X509Certificate[] getAcceptedIssuers(){return new X509Certificate[0];}public void checkClientTrusted(X509Certificate[] c,String a){}public void checkServerTrusted(X509Certificate[] c,String a){}}},new SecureRandom());
            SSLSocket tls=(SSLSocket)contextTls.getSocketFactory().createSocket(raw,address,port,true);socket=tls;if(cancelled)throw new IOException("Speaker connection stopped");tls.setEnabledProtocols(new String[]{"TLSv1.3"});
            TimerTask deadline=new TimerTask(){public void run(){cancellation.close(tls);}};watchdog.schedule(deadline,60000);
            DataInputStream input;long resource,generation;
            try{
                tls.startHandshake();input=new DataInputStream(tls.getInputStream());OutputStream stream=tls.getOutputStream();
                if(!code.isEmpty()){access=new AccessChannel(input,stream,code);input=new DataInputStream(access.input);stream=access.output;}code="";
                SecureRandom random=new SecureRandom();resource=id(random);generation=id(random);stream.write(SpeakerProtocol.hello(resource,generation));stream.write(SpeakerProtocol.capabilities());
                SpeakerProtocol.capabilities(input);stream.write(SpeakerProtocol.request(resource));tls.setSoTimeout(40000);SpeakerProtocol.grant(input,resource);
            }finally{deadline.cancel();}
            if(cancelled)throw new IOException("Speaker connection stopped");tls.setSoTimeout(5000);
            output=new Output();synchronized(this){active=output;output.localMute(muted);output.volume(volume);}output.start();
            SpeakerProtocol protocol=new SpeakerProtocol(resource,generation);boolean connected=false;
            while(!cancelled){long epoch=output.epoch();SpeakerProtocol.Frame frame=protocol.receive(input);output.offer(frame,epoch);if(!connected){connected=true;listener.connected();listener.status("Playing through "+choice.label()+" - 48 kHz stereo");}}
        }catch(Exception error){if(!cancelled){Exception actual=playbackFailure!=null?playbackFailure:error;ending=actual.getMessage()==null?actual.getClass().getSimpleName():actual.getMessage();}}
        finally{
            Thread.interrupted();code="";watchdog.cancel();active=null;cancellation.close(socket);
            if(output!=null)try{output.close();}catch(Exception error){ending=error.getMessage();}
            if(access!=null)access.close();if(lease!=null)lease.close();listener.ended(admissionLost!=null?admissionLost:ending);
        }
    }
    private final class Output implements AutoCloseable {
        final SpeakerJitter jitter=new SpeakerJitter();final AudioTrack track;final Thread worker;
        volatile boolean stopped;private boolean localMuted,remoteMuted;private int gain=100;private long revision;
        Output()throws IOException{
            AudioDeviceInfo selected=null;for(AudioDeviceInfo device:context.getSystemService(AudioManager.class).getDevices(AudioManager.GET_DEVICES_OUTPUTS))if(device.isSink()&&device.getId()==choice.id())selected=device;
            if(selected==null)throw new IOException("Selected output is no longer connected");
            int minimum=AudioTrack.getMinBufferSize(48000,AudioFormat.CHANNEL_OUT_STEREO,AudioFormat.ENCODING_PCM_16BIT);if(minimum<=0)throw new IOException("Selected output cannot play 48 kHz stereo PCM16");
            // Earpiece is a communication output. Request that usage per track;
            // never seize global communication mode or another app's call route.
            boolean earpiece=selected.getType()==AudioDeviceInfo.TYPE_BUILTIN_EARPIECE;
            AudioTrack created=new AudioTrack.Builder().setAudioAttributes(new AudioAttributes.Builder().setUsage(earpiece?AudioAttributes.USAGE_VOICE_COMMUNICATION:AudioAttributes.USAGE_MEDIA).setContentType(earpiece?AudioAttributes.CONTENT_TYPE_SPEECH:AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(new AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(48000).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(Math.max(minimum,3840)).build();
            try{
                if(created.getState()!=AudioTrack.STATE_INITIALIZED||created.getSampleRate()!=48000||!created.setPreferredDevice(selected))throw new IOException("Android cannot route audio to the selected output");
                track=created;worker=new Thread(this::play,"ExtraPane speaker playback");
            }catch(IOException|RuntimeException|Error error){created.release();throw error;}
        }
        synchronized long epoch(){return jitter.epoch();}
        synchronized void localMute(boolean value){if(localMuted==value)return;localMuted=value;jitter.mute(localMuted||remoteMuted);++revision;}
        synchronized void volume(int value){if(gain==value)return;gain=value;jitter.reset();++revision;}
        synchronized void offer(SpeakerProtocol.Frame frame,long epoch){
            // A TLS read started before a local control change must not refill
            // the cleared playback queue after mute/unmute or volume changes.
            if(epoch!=jitter.epoch()||stopped)return;
            if(frame.muted()!=remoteMuted){remoteMuted=frame.muted();jitter.mute(localMuted||remoteMuted);++revision;}
            if(frame.discontinuity()){jitter.reset();++revision;}jitter.offerCurrent(frame.pcm(),jitter.epoch());
        }
        void start(){if(!stopped)worker.start();}
        void stop(){stopped=true;worker.interrupt();}
        private void play(){
            try{
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO);track.play();long seen=-1;short[] packet=null;int offset=0;long started=System.nanoTime();
                while(!stopped){
                    synchronized(this){
                        if(seen!=revision){track.pause();track.flush();track.setVolume(gain/100f);track.play();seen=revision;packet=null;offset=0;}
                        if(packet==null){packet=jitter.take();offset=0;}
                        int written=track.write(packet,offset,packet.length-offset,AudioTrack.WRITE_NON_BLOCKING);
                        if(written<0)throw new IOException("Speaker output failed (AudioTrack "+written+")");offset+=written;if(offset==packet.length)packet=null;
                    }
                    AudioDeviceInfo routed=track.getRoutedDevice();if(routed!=null&&routed.getId()!=choice.id())throw new IOException("Android changed the selected output route; select it again");
                    if(routed==null&&System.nanoTime()-started>2_000_000_000L)throw new IOException("Android did not activate the selected output route");
                    Thread.sleep(2);
                }
            }catch(Exception error){if(!stopped){playbackFailure=error;cancellation.close(socket);}}
            finally{try{track.pause();track.flush();}catch(RuntimeException ignored){}track.release();}
        }
        public void close()throws IOException{stop();if(worker.getState()==Thread.State.NEW){track.release();return;}try{worker.join(2500);}catch(InterruptedException error){Thread.currentThread().interrupt();throw new IOException("Interrupted speaker cleanup",error);}if(worker.isAlive())throw new IOException("Speaker output cleanup is still pending");}
    }
}
