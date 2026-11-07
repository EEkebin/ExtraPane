package io.github.eekebin.extrapane;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.net.Network;
import android.os.Build;
import android.os.SystemClock;
import android.util.Log;
import android.view.Surface;
import java.io.*;
import java.net.*;
import java.nio.ByteBuffer;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.concurrent.atomic.AtomicLong;
import javax.net.ssl.*;

final class ClientSession extends Thread {
    interface Listener {
        void status(String message);
        void connected();
        void format(int width,int height,int caps,String decoder);
        void statistics(String text);
        void ended(String reason);
    }
    final InputQueue input;
    private final String address,local;private String accessCode;private final int port;
    private final Network network;
    private final Surface surface;
    private final int width,height,hz,requested,scale;private final boolean exactMode;
    private final Listener listener;
    private volatile boolean cancelled;
    private volatile String admissionLost;private boolean reportedConnected;
    private volatile Socket socket;
    private Decoder decoder;
    ClientSession(String address,String local,Network network,Surface surface,int width,int height,int hz,int caps,int scale,InputQueue input,Listener listener,int port,String accessCode,boolean exactMode){
        super("ExtraPane connection");this.exactMode=exactMode;this.port=port;this.accessCode=accessCode;this.address=address;this.local=local;this.network=network;this.surface=surface;
        this.width=width;this.height=height;this.hz=hz;this.requested=caps;this.scale=scale;this.input=input;this.listener=listener;
    }
    void cancel(){cancelled=true;input.grant(0);Socket s=socket;if(s!=null)try{s.close();}catch(IOException ignored){}interrupt();}
    private void status(String text){Log.i("ExtraPane",text);listener.status(text);}
    static InetAddress ipv4(String text)throws IOException{
        String[] pieces=text.split("\\.",-1);if(pieces.length!=4)throw new IOException("Enter a numeric server IPv4 address");
        byte[] bytes=new byte[4];for(int i=0;i<4;i++){if(!pieces[i].matches("[0-9]{1,3}"))throw new IOException("Invalid IPv4 address");int v=Integer.parseInt(pieces[i]);if(v>255)throw new IOException("Invalid IPv4 address");bytes[i]=(byte)v;}
        InetAddress a=InetAddress.getByAddress(bytes);if(a.isAnyLocalAddress() || a.isLoopbackAddress() || a.isMulticastAddress() || (bytes[0]&255)>=224)throw new IOException("Use a reachable LAN IPv4 address");return a;
    }
    private byte[] read(DataInputStream in,int size)throws IOException{if(cancelled)throw new IOException("Disconnected");byte[] b=new byte[size];in.readFully(b);return b;}
    @Override public void run(){String ending="Disconnected";AccessChannel accessChannel=null;AdmissionBroker.Lease admissionLease=null;
        try{
            admissionLease=AdmissionBroker.acquire(address,port,local,network,accessCode,0,()->{admissionLost="Server admission ended";cancel();},()->cancelled);accessCode=admissionLease.credential;
            status("Connecting to "+address+"…");InetAddress peer=ipv4(address);
            Socket raw=new Socket();socket=raw;
            if(cancelled)throw new IOException("Disconnected");
            if(network!=null)network.bindSocket(raw);
            if(!local.isEmpty())raw.bind(new InetSocketAddress(ipv4(local),0));
            raw.setTcpNoDelay(true);raw.connect(new InetSocketAddress(peer,port),8000);raw.setSoTimeout(10000);
            // Product automatic-LAN mode deliberately has no peer identity check.
            // This trust manager is private to this socket, never a global default.
            SSLContext context=SSLContext.getInstance("TLSv1.3");
            context.init(null,new TrustManager[]{new X509TrustManager(){
                public X509Certificate[] getAcceptedIssuers(){return new X509Certificate[0];}
                public void checkClientTrusted(X509Certificate[] c,String a){}
                public void checkServerTrusted(X509Certificate[] c,String a){}
            }},new SecureRandom());
            SSLSocket tls=(SSLSocket)context.getSocketFactory().createSocket(raw,address,port,true);socket=tls;
            if(cancelled)throw new IOException("Disconnected");
            tls.setEnabledProtocols(new String[]{"TLSv1.3"});tls.startHandshake();
            DataInputStream in=new DataInputStream(tls.getInputStream());OutputStream out=tls.getOutputStream();
            if(!accessCode.isEmpty()){status("Authenticating access code?");java.util.Timer deadline=new java.util.Timer(true);deadline.schedule(new java.util.TimerTask(){public void run(){try{tls.close();}catch(IOException ignored){}}},10000);
                try{accessChannel=new AccessChannel(in,out,accessCode);}finally{deadline.cancel();}accessCode="";in=new DataInputStream(accessChannel.input);out=accessChannel.output;}
            out.write(exactMode?Protocol.customRequest(width,height,hz,requested,scale):Protocol.request(width,height,hz,requested,scale));tls.setSoTimeout(45000);
            long[] offer=Protocol.offer(read(in,32));
            if(!ConnectionValidation.initialDisplayMode(exactMode,width,height,hz,offer))throw new IOException("Server cannot provide the requested custom display resolution and frame rate");
            prepare(offer,out);tls.setSoTimeout(15000);
            long sequence=0,previous=0,frames=0,bytes=0,start=SystemClock.elapsedRealtime();
            while(!cancelled && (offer[3]==0 || sequence<offer[3])){
                long[] frame=Protocol.frame(read(in,24));
                if(frame[0]!=(sequence&0xffffffffL) || frame[2]<previous || frame[2]<0)throw new IOException("Out-of-order video frame");previous=frame[2];
                if(frame[1]==0){
                    input.grant(0);long[] next=Protocol.offer(read(in,32));
                    if(next[3]!=offer[3] || next[4]!=offer[4] || next[5]!=offer[5] || (next[5]!=0 && next[6]==offer[6]))throw new IOException("Invalid display reconfiguration");
                    offer=next;prepare(offer,out);continue;
                }
                byte[] payload=read(in,(int)frame[1]);
                if(offer[5]!=0)out.write(Protocol.input(offer[6],sequence,input.take()));
                decoder.queue(payload,frame[2]);sequence++;frames++;bytes+=payload.length;
                long now=SystemClock.elapsedRealtime();if(now-start>=1000){
                    String stats=String.format(java.util.Locale.ROOT,"%d × %d · Receive %.1f FPS · %.1f Mbps\n%s · output %d frames",offer[0],offer[1],frames*1000.0/(now-start),bytes*0.008/(now-start),decoder.name,decoder.presented.get());
                    listener.statistics(stats);Log.i("ExtraPane",stats.replace('\n',' '));start=now;frames=bytes=0;
                }
            }
            if(!cancelled && offer[3]!=0){decoder.awaitOutput(sequence);out.write(new byte[]{'D','O','N','E'});ending="Server diagnostic session completed";}
        }catch(Exception error){if(!cancelled){ending=error.getMessage()==null?error.getClass().getSimpleName():error.getMessage();Log.e("ExtraPane","Connection failed",error);}}
        finally{accessCode="";if(accessChannel!=null)accessChannel.close();input.grant(0);if(socket!=null)try{socket.close();}catch(IOException ignored){}if(decoder!=null)decoder.close();if(admissionLease!=null)admissionLease.close();listener.ended(admissionLost!=null?admissionLost:ending);}
    }
    private void prepare(long[] offer,OutputStream out)throws Exception{
        if(offer[4]!=2 || (offer[5]&~requested)!=0)throw new IOException("Unsupported codec or input grant");
        input.grant(0);if(decoder!=null)decoder.close();decoder=new Decoder((int)offer[0],(int)offer[1],surface);
        input.grant((int)offer[5]);listener.format((int)offer[0],(int)offer[1],(int)offer[5],decoder.name);
        out.write(Protocol.READY);if(!reportedConnected){reportedConnected=true;listener.connected();}status("Connected · TLS 1.3 · "+decoder.name);
    }
    private final class Decoder implements AutoCloseable {
        final MediaCodec codec;
        final String name;
        final AtomicLong presented=new AtomicLong();
        private final Thread drain;
        private volatile boolean stopped;
        private volatile Exception failure;
        Decoder(int width,int height,Surface target)throws IOException{
            MediaCodecInfo chosen=null;
            for(MediaCodecInfo info:new MediaCodecList(MediaCodecList.REGULAR_CODECS).getCodecInfos()){
                if(info.isEncoder())continue;
                for(String type:info.getSupportedTypes())if(type.equalsIgnoreCase("video/avc")){
                    MediaCodecInfo.CodecCapabilities caps=info.getCapabilitiesForType(type);
                    if(caps.getVideoCapabilities().isSizeSupported(width,height) && (chosen==null || info.isHardwareAccelerated()))chosen=info;
                }
                if(chosen!=null && chosen.isHardwareAccelerated())break;
            }
            if(chosen==null)throw new IOException("No H.264 decoder supports "+width+"×"+height);
            name=chosen.getName();codec=MediaCodec.createByCodecName(name);
            try{
                MediaFormat format=MediaFormat.createVideoFormat("video/avc",width,height);format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE,2*1024*1024);
                if(Build.VERSION.SDK_INT>=30 && chosen.getCapabilitiesForType("video/avc").isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_LowLatency))format.setInteger(MediaFormat.KEY_LOW_LATENCY,1);
                codec.configure(format,target,null,0);codec.start();
            }catch(RuntimeException error){codec.release();throw error;}
            drain=new Thread(()->{
                MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();
                try{while(!stopped && !cancelled){int index=codec.dequeueOutputBuffer(info,10000);if(index>=0){codec.releaseOutputBuffer(index,true);presented.incrementAndGet();}}}
                catch(Exception error){if(!stopped && !cancelled)failure=error;}
            },"ExtraPane decoder");drain.start();
        }
        void queue(byte[] packet,long time)throws Exception{
            long deadline=SystemClock.elapsedRealtime()+500;
            while(!cancelled){if(failure!=null)throw failure;int index=codec.dequeueInputBuffer(10000);
                if(index>=0){ByteBuffer buffer=codec.getInputBuffer(index);if(buffer==null || packet.length>buffer.capacity())throw new IOException("Encoded frame exceeds decoder buffer");buffer.clear();buffer.put(packet);codec.queueInputBuffer(index,0,packet.length,time,0);return;}
                if(SystemClock.elapsedRealtime()>deadline)throw new IOException("Decoder stalled; disconnected to avoid queued latency");
            }
            throw new IOException("Disconnected");
        }
        void awaitOutput(long count)throws Exception{long deadline=SystemClock.elapsedRealtime()+2000;while(!cancelled && presented.get()<count){if(failure!=null)throw failure;if(SystemClock.elapsedRealtime()>deadline)throw new IOException("Decoder did not finish diagnostic frames");Thread.sleep(5);}}
        public void close(){stopped=true;try{drain.join(1000);}catch(InterruptedException ignored){Thread.interrupted();}try{codec.stop();}catch(RuntimeException ignored){}codec.release();}
    }
}
