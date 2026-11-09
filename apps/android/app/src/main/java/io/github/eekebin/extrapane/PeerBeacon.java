package io.github.eekebin.extrapane;

import java.io.IOException;
import java.net.*;
import java.util.*;
import java.util.concurrent.*;

/** Interface-scoped metadata announcements only; never opens media capture. */
final class PeerBeacon implements AutoCloseable {
    private final PeerService peers;
    private final List<MulticastSocket> sockets=new ArrayList<>();
    private final InetSocketAddress destination;
    private final ScheduledExecutorService worker=Executors.newSingleThreadScheduledExecutor();
    private final int port;
    private long sequence=1;
    private boolean closed;
    PeerBeacon(PeerService peers,String local,int port)throws IOException{
        this.peers=peers;this.port=port;
        List<InetAddress> addresses=new ArrayList<>();
        if(local.isEmpty()||local.equals("0.0.0.0")){
            for(NetworkInterface nic:Collections.list(NetworkInterface.getNetworkInterfaces()))if(nic.isUp()&&!nic.isLoopback())for(InetAddress address:Collections.list(nic.getInetAddresses()))if(address instanceof Inet4Address&&!address.isLoopbackAddress()&&!address.isLinkLocalAddress())addresses.add(address);
        }else addresses.add(InetAddress.getByName(local));
        for(InetAddress address:addresses){
            if(!(address instanceof Inet4Address)||address.isAnyLocalAddress()||address.isLoopbackAddress())continue;
            NetworkInterface nic=NetworkInterface.getByInetAddress(address);if(nic==null||!nic.isUp())continue;
            MulticastSocket socket=null;try{socket=new MulticastSocket(null);socket.bind(new InetSocketAddress(address,0));socket.setNetworkInterface(nic);socket.setTimeToLive(1);sockets.add(socket);}catch(IOException e){if(socket!=null)socket.close();}
        }
        if(sockets.isEmpty()){worker.shutdownNow();throw new IOException("LAN discovery needs a connected network interface");}
        destination=new InetSocketAddress("239.255.43.24",43825);
        worker.scheduleAtFixedRate(()->{synchronized(this){if(closed)return;try{send(peers.announcement(port,sequence++));}catch(IOException ignored){/* Direct address connections remain available. */}}},0,3,TimeUnit.SECONDS);
    }
    private void send(byte[] bytes)throws IOException{IOException last=null;boolean sent=false;for(MulticastSocket socket:sockets)try{socket.send(new DatagramPacket(bytes,bytes.length,destination));sent=true;}catch(IOException e){last=e;}if(!sent&&last!=null)throw last;}
    public synchronized void close(){if(closed)return;closed=true;worker.shutdownNow();try{send(peers.withdrawal(sequence++));}catch(IOException ignored){}for(MulticastSocket socket:sockets)socket.close();}
}
