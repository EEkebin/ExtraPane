package io.github.eekebin.extrapane;

import java.io.IOException;
import java.net.*;
import java.util.concurrent.*;

/** Interface-scoped metadata announcements only; never opens media capture. */
final class PeerBeacon implements AutoCloseable {
    private final PeerService peers;
    private final MulticastSocket socket;
    private final InetSocketAddress destination;
    private final ScheduledExecutorService worker=Executors.newSingleThreadScheduledExecutor();
    private final int port;
    private long sequence=1;
    private boolean closed;
    PeerBeacon(PeerService peers,String local,int port)throws IOException{
        this.peers=peers;this.port=port;
        InetAddress address=InetAddress.getByName(local);
        if(!(address instanceof Inet4Address)||address.isAnyLocalAddress()||address.isLoopbackAddress())throw new IOException("LAN discovery needs a connected network interface");
        NetworkInterface nic=NetworkInterface.getByInetAddress(address);if(nic==null||!nic.isUp())throw new IOException("Network interface is unavailable");
        socket=new MulticastSocket(null);try{socket.bind(new InetSocketAddress(address,0));socket.setNetworkInterface(nic);socket.setTimeToLive(1);}catch(IOException e){socket.close();throw e;}
        destination=new InetSocketAddress("239.255.43.24",43825);
        worker.scheduleAtFixedRate(()->{synchronized(this){if(closed)return;try{send(peers.announcement(port,sequence++));}catch(IOException ignored){/* Direct address connections remain available. */}}},0,3,TimeUnit.SECONDS);
    }
    private void send(byte[] bytes)throws IOException{socket.send(new DatagramPacket(bytes,bytes.length,destination));}
    public synchronized void close(){if(closed)return;closed=true;worker.shutdownNow();try{send(peers.withdrawal(sequence++));}catch(IOException ignored){}socket.close();}
}
