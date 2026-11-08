package io.github.eekebin.extrapane;

import android.content.Context;
import android.net.wifi.WifiManager;
import java.net.*;
import java.util.Arrays;

/** One explicitly requested, ten-second metadata scan; never dials or opens media. */
final class NearbyPeerSearch implements AutoCloseable {
    interface Listener {void finished(NearbyPeers result,String error);}
    private volatile boolean closed;
    private MulticastSocket socket;
    void start(Context context,String local,Listener listener){new Thread(()->run(context.getApplicationContext(),local,listener),"ExtraPane device search").start();}
    private void run(Context context,String local,Listener listener){
        NearbyPeers result=new NearbyPeers();String error=null;WifiManager.MulticastLock lock=null;
        try{
            InetAddress address=InetAddress.getByName(local);NetworkInterface nic=NetworkInterface.getByInetAddress(address);
            if(!(address instanceof Inet4Address)||address.isLoopbackAddress()||nic==null||!nic.isUp())throw new java.io.IOException("Select a connected LAN interface to search nearby devices");
            WifiManager wifi=(WifiManager)context.getSystemService(Context.WIFI_SERVICE);if(wifi!=null){lock=wifi.createMulticastLock("ExtraPane manual search");lock.setReferenceCounted(false);lock.acquire();}
            try(MulticastSocket receiver=new MulticastSocket(null)){
                synchronized(this){if(closed)return;socket=receiver;}
                receiver.setReuseAddress(true);receiver.bind(new InetSocketAddress(43825));receiver.setNetworkInterface(nic);receiver.joinGroup(new InetSocketAddress("239.255.43.24",43825),nic);receiver.setSoTimeout(500);
                long deadline=android.os.SystemClock.elapsedRealtime()+10000;
                while(!closed&&android.os.SystemClock.elapsedRealtime()<deadline){
                    byte[] bytes=new byte[2049];DatagramPacket packet=new DatagramPacket(bytes,bytes.length);
                    try{receiver.receive(packet);if(packet.getLength()>2048||!(packet.getAddress() instanceof Inet4Address)||packet.getAddress().isLoopbackAddress()||packet.getAddress().isMulticastAddress())continue;
                        result.accept(PeerService.decodeAnnouncement(Arrays.copyOf(bytes,packet.getLength())),packet.getAddress().getHostAddress(),android.os.SystemClock.elapsedRealtime());
                    }catch(SocketTimeoutException ignored){}catch(java.io.IOException malformed){if(receiver.isClosed())break;}
                }
            }
        }catch(Exception e){if(!closed)error=e.getMessage()==null?"Nearby search failed":e.getMessage();}
        finally{ synchronized(this){socket=null;}if(lock!=null&&lock.isHeld())lock.release(); }
        if(!closed)listener.finished(result,error);
    }
    public synchronized void close(){closed=true;if(socket!=null)socket.close();}
}
