package io.github.eekebin.extrapane;

import android.content.Context;
import android.net.Network;
import java.io.*;
import java.net.*;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.*;
import javax.net.ssl.*;

/** Dedicated camera-only admitted session. Does not request a display or input. */
final class CameraSession extends Thread {
    interface Listener {void status(String text);void connected();void ended(String reason);}
    private final Context context;private final String address,local;private final int port;private String code;
    private final Network network;private final CameraCapture.Choice choice;private final Listener listener;
    private volatile String admissionLost;
    private volatile boolean cancelled;private volatile Socket socket;private volatile Exception captureFailure;
    CameraSession(Context context,String address,int port,String local,Network network,String code,CameraCapture.Choice choice,Listener listener){
        super("ExtraPane camera connection");this.context=context.getApplicationContext();this.address=address;this.port=port;this.local=local;this.network=network;this.code=code;this.choice=choice;this.listener=listener;
    }
    void cancel(){cancelled=true;Socket current=socket;if(current!=null)try{current.close();}catch(IOException ignored){}interrupt();}
    private static long id(SecureRandom random){long value;do{value=random.nextLong()&Long.MAX_VALUE;}while(value==0);return value;}
    private static byte[] read(DataInputStream in,int count)throws IOException{byte[] bytes=new byte[count];in.readFully(bytes);return bytes;}
    public void run(){String ending="Camera sharing stopped";CaptureFanout<CameraCapture.Packet>.Subscription capture=null;AccessChannel access=null;AdmissionBroker.Lease admissionLease=null;Timer watchdog=new Timer("ExtraPane camera deadlines",true);
        try{
            admissionLease=AdmissionBroker.acquire(address,port,local,network,code,1,()->{admissionLost="Server admission ended";cancel();},()->cancelled);code=admissionLease.credential;
            InetAddress peer=ClientSession.ipv4(address);Socket raw=new Socket();socket=raw;
            if(cancelled)throw new IOException("Camera sharing stopped");if(network!=null)network.bindSocket(raw);if(!local.isEmpty())raw.bind(new InetSocketAddress(ClientSession.ipv4(local),0));
            listener.status("Connecting camera to "+address+"…");raw.setTcpNoDelay(true);raw.connect(new InetSocketAddress(peer,port),8000);raw.setSoTimeout(10000);
            SSLContext tlsContext=SSLContext.getInstance("TLSv1.3");tlsContext.init(null,new TrustManager[]{new X509TrustManager(){public X509Certificate[] getAcceptedIssuers(){return new X509Certificate[0];}public void checkClientTrusted(X509Certificate[] c,String a){}public void checkServerTrusted(X509Certificate[] c,String a){}}},new SecureRandom());
            SSLSocket tls=(SSLSocket)tlsContext.getSocketFactory().createSocket(raw,address,port,true);socket=tls;if(cancelled)throw new IOException("Camera sharing stopped");tls.setEnabledProtocols(new String[]{"TLSv1.3"});
            TimerTask admission=new TimerTask(){public void run(){try{tls.close();}catch(IOException ignored){}}};watchdog.schedule(admission,20000);
            long resource,generation;OutputStream out;try{
                tls.startHandshake();DataInputStream in=new DataInputStream(tls.getInputStream());out=tls.getOutputStream();
                if(!code.isEmpty()){access=new AccessChannel(in,out,code);in=new DataInputStream(access.input);out=access.output;}code="";
                SecureRandom random=new SecureRandom();resource=id(random);generation=id(random);
                out.write(CameraProtocol.hello(choice.width(),choice.height(),choice.fps(),resource,generation));out.write(CameraProtocol.capabilities());
                byte[] header=read(in,24);CameraProtocol.validateCapabilities(header,read(in,CameraProtocol.payloadLength(header)));
                out.write(CameraProtocol.request(resource));header=read(in,24);CameraProtocol.validateGrant(header,read(in,CameraProtocol.payloadLength(header)),resource);
            }finally{admission.cancel();}
            if(cancelled)throw new IOException("Camera sharing stopped");
            capture=SharedCaptures.camera(context,choice);
            listener.status("Sharing "+choice.label()+" · "+choice.width()+" × "+choice.height()+" · "+choice.fps()+" FPS");
            long sequence=0;boolean reportedConnected=false;
            while(!cancelled){CameraCapture.Packet packet=capture.next();if(packet==null)continue;
                TimerTask deadline=new TimerTask(){public void run(){try{tls.close();}catch(IOException ignored){}}};watchdog.schedule(deadline,5000);
                try{out.write(CameraProtocol.frameHeader(resource,generation,sequence++,packet.timestamp(),packet.bytes().length));out.write(packet.bytes());if(!reportedConnected){reportedConnected=true;listener.connected();}}finally{deadline.cancel();watchdog.purge();}
            }
        }catch(Exception error){if(!cancelled){Exception cause=captureFailure!=null?captureFailure:error;ending=cause.getMessage()==null?cause.getClass().getSimpleName():cause.getMessage();}}
        finally{
            Thread.interrupted();code="";watchdog.cancel();if(socket!=null)try{socket.close();}catch(IOException ignored){}
            if(capture!=null)try{capture.close();}catch(Exception error){ending=error.getMessage()==null?"Camera cleanup failed":error.getMessage();}if(access!=null)access.close();if(admissionLease!=null)admissionLease.close();listener.ended(admissionLost!=null?admissionLost:ending);
        }
    }
}
