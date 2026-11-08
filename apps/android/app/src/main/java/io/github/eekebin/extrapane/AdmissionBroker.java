package io.github.eekebin.extrapane;

import android.net.Network;
import java.io.*;
import java.net.*;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.BooleanSupplier;
import javax.net.ssl.*;

/** One authenticated control connection per destination; never carries media. */
final class AdmissionBroker {
    private static native byte[] request(int operation,int kind) throws IOException;
    private static native String credential(byte[] reply,int operation,int kind,boolean protectedMode) throws IOException;
    private static final Map<String,Broker> brokers=new HashMap<>();
    interface Transport extends AutoCloseable {
        void open(String address,int port,String local,Network network,String code)throws Exception;
        String exchange(int operation,int kind)throws Exception;
        void cancel();
        void close();
    }
    private static java.util.function.Supplier<Transport> transports=NativeTransport::new;
    static void testTransport(java.util.function.Supplier<Transport> factory){synchronized(brokers){if(!brokers.isEmpty())throw new IllegalStateException("Active admission brokers");transports=factory;}}
    private static final class NativeTransport implements Transport {
        static { System.loadLibrary("extrapane"); }
        private volatile boolean cancelled;private volatile Socket socket;private final SocketCancellation socketCancellation=new SocketCancellation();
        private AccessChannel access;private DataInputStream in;private OutputStream out;
        private final Timer deadline=new Timer("ExtraPane admission deadline",true);
        public void open(String address,int port,String local,Network network,String code)throws Exception {
            Socket raw=new Socket();socket=raw;if(cancelled)throw new InterruptedException("Connection cancelled");
            if(network!=null)network.bindSocket(raw);if(!local.isEmpty())raw.bind(new InetSocketAddress(ClientSession.ipv4(local),0));
            raw.setTcpNoDelay(true);raw.connect(new InetSocketAddress(ClientSession.ipv4(address),port),8000);raw.setSoTimeout(10000);
            SSLContext context=SSLContext.getInstance("TLSv1.3");context.init(null,new TrustManager[]{new X509TrustManager(){public X509Certificate[] getAcceptedIssuers(){return new X509Certificate[0];}public void checkClientTrusted(X509Certificate[] chain,String auth){}public void checkServerTrusted(X509Certificate[] chain,String auth){}}},new SecureRandom());
            SSLSocket tls=(SSLSocket)context.getSocketFactory().createSocket(raw,address,port,true);socket=tls;tls.setEnabledProtocols(new String[]{"TLSv1.3"});
            TimerTask admission=new TimerTask(){public void run(){NativeTransport.this.cancel();}};deadline.schedule(admission,20000);
            try{if(cancelled)throw new InterruptedException("Connection cancelled");tls.startHandshake();access=new AccessChannel(new DataInputStream(tls.getInputStream()),tls.getOutputStream(),code);in=new DataInputStream(access.input);out=access.output;exchange(1,0);}finally{admission.cancel();}
        }
        public String exchange(int operation,int kind)throws Exception {
            TimerTask timeout=new TimerTask(){public void run(){NativeTransport.this.cancel();}};deadline.schedule(timeout,10000);
            byte[] reply=new byte[128];
            try{if(cancelled)throw new IOException("Server admission ended");out.write(request(operation,kind));in.readFully(reply);return credential(reply,operation,kind,true);}
            finally{Arrays.fill(reply,(byte)0);timeout.cancel();deadline.purge();}
        }
        public void cancel(){cancelled=true;socketCancellation.close(socket);}
        public void close(){cancel();deadline.cancel();if(access!=null){access.close();access=null;}}
    }

    static final class Lease implements AutoCloseable {
        final String credential;
        private Pending pending;
        Lease(String value,Pending pending){credential=value;this.pending=pending;}
        public synchronized void close(){if(pending!=null){pending.release();pending=null;}}
    }
    private static final class Pending {
        final Broker broker;final int kind;final Runnable lost;
        final CompletableFuture<String> result=new CompletableFuture<>();
        volatile boolean released;
        Pending(Broker broker,int kind,Runnable lost){this.broker=broker;this.kind=kind;this.lost=lost;}
        void release(){synchronized(brokers){if(released)return;released=true;broker.requests.remove(this);broker.clients.remove(this);if(broker.clients.isEmpty()){brokers.remove(broker.key,broker);broker.stopConnection();}}}
    }
    static Lease acquire(String address,int port,String local,Network network,String code,int kind,Runnable onLost,BooleanSupplier cancelled)throws Exception {
        if(kind<0||kind>4)throw new IOException("Invalid resource kind");
        final String key=address+":"+port+"@"+local+"#"+(network==null?0:network.getNetworkHandle());
        Pending pending;
        synchronized(brokers){
            Broker broker=brokers.get(key);
            if(broker==null){
                if(code.isEmpty())return new Lease("",null);
                broker=new Broker(key,address,port,local,network,code);brokers.put(key,broker);
            }
            if(broker.stopping)throw new IOException("Server admission is stopping; retry shortly");
            pending=new Pending(broker,kind,onLost);broker.clients.add(pending);
            if(!broker.requests.offer(pending)){pending.release();throw new IOException("Too many pending resource requests; retry shortly");}
            if(!broker.started){broker.started=true;broker.start();}
        }
        try{
            for(;;){
                if(cancelled.getAsBoolean()||Thread.currentThread().isInterrupted())throw new InterruptedException("Connection cancelled");
                try{return new Lease(pending.result.get(100,TimeUnit.MILLISECONDS),pending);}catch(TimeoutException waiting){}
            }
        }catch(Exception error){pending.release();if(error instanceof ExecutionException && error.getCause() instanceof Exception)throw (Exception)error.getCause();throw error;}
    }
    private static final class Broker extends Thread {
        final String key,address,local;final int port;final Network network;
        String code;boolean started;volatile boolean stopping;volatile Transport transport;
        final Set<Pending> clients=new HashSet<>();
        final BlockingQueue<Pending> requests=new ArrayBlockingQueue<>(64);
        Broker(String key,String address,int port,String local,Network network,String code){super("ExtraPane server admission");this.key=key;this.address=address;this.port=port;this.local=local;this.network=network;this.code=code;}
        void stopBroker(){stopping=true;interrupt();Transport current=transport;if(current!=null)current.cancel();}
        // Thread.stop is unsafe; all cleanup stays on the owning thread.
        void stopConnection(){stopBroker();}
        public void run(){
            Exception failure=new IOException("Server admission ended");
            try{
                synchronized(brokers){transport=transports.get();}
                if(stopping)throw new InterruptedException("Connection cancelled");
                transport.open(address,port,local,network,code);code="";
                while(!stopping){
                    Pending pending=requests.poll(5,TimeUnit.SECONDS);
                    if(pending!=null){synchronized(brokers){if(pending.released)continue;}}
                    String ticket=transport.exchange(pending==null?2:3,pending==null?0:pending.kind);if(pending!=null)pending.result.complete(ticket);
                }
            }catch(Exception error){failure=error;}finally{
                code="";if(transport!=null)transport.close();List<Pending> abandoned;
                synchronized(brokers){stopping=true;brokers.remove(key,this);abandoned=new ArrayList<>(clients);clients.clear();}
                for(Pending pending:abandoned){boolean admitted=pending.result.isDone()&&!pending.result.isCompletedExceptionally()&&!pending.result.isCancelled();pending.result.completeExceptionally(new IOException("Server access connection ended: "+failure.getMessage(),failure));if(admitted&&!pending.released)try{pending.lost.run();}catch(RuntimeException ignored){}}
            }
        }
    }
}
