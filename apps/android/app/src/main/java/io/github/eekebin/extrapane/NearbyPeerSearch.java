package io.github.eekebin.extrapane;

import android.content.Context;
import android.net.wifi.WifiManager;

import java.net.*;
import java.util.*;

/** One explicitly requested, ten-second metadata scan; never dials or opens media. */
final class NearbyPeerSearch implements AutoCloseable {
    interface Listener {
        void finished(NearbyPeers result, String error);

        default void progress(NearbyPeers result) {}
    }

    private final java.util.Set<String> knownOnly;

    NearbyPeerSearch() {
        knownOnly = null;
    }

    NearbyPeerSearch(java.util.Set<String> known) {
        knownOnly = java.util.Set.copyOf(known);
    }

    private volatile boolean closed;
    private MulticastSocket socket;

    void start(Context context, String local, Listener listener) {
        new Thread(
                        () -> run(context.getApplicationContext(), local, listener),
                        "ExtraPane device search")
                .start();
    }

    private void run(Context context, String local, Listener listener) {
        NearbyPeers result = new NearbyPeers(knownOnly);
        String error = null;
        WifiManager.MulticastLock lock = null;
        try {
            List<NetworkInterface> interfaces = new ArrayList<>();
            if (local.isEmpty() || local.equals("0.0.0.0")) {
                for (NetworkInterface nic :
                        Collections.list(NetworkInterface.getNetworkInterfaces()))
                    if (nic.isUp() && !nic.isLoopback())
                        for (InetAddress address : Collections.list(nic.getInetAddresses()))
                            if (address instanceof Inet4Address
                                    && !address.isLoopbackAddress()
                                    && !address.isLinkLocalAddress()) {
                                interfaces.add(nic);
                                break;
                            }
            } else {
                InetAddress address = InetAddress.getByName(local);
                NetworkInterface nic = NetworkInterface.getByInetAddress(address);
                if (address instanceof Inet4Address
                        && !address.isLoopbackAddress()
                        && nic != null
                        && nic.isUp()) interfaces.add(nic);
            }
            if (interfaces.isEmpty())
                throw new java.io.IOException(
                        "No connected LAN interface is available for nearby discovery");
            WifiManager wifi = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                lock = wifi.createMulticastLock("ExtraPane manual search");
                lock.setReferenceCounted(false);
                lock.acquire();
            }
            try (MulticastSocket receiver = new MulticastSocket(null)) {
                synchronized (this) {
                    if (closed) return;
                    socket = receiver;
                }
                receiver.setReuseAddress(true);
                receiver.bind(new InetSocketAddress(43825));
                int joined = 0;
                for (NetworkInterface nic : interfaces)
                    try {
                        receiver.joinGroup(new InetSocketAddress("239.255.43.24", 43825), nic);
                        ++joined;
                    } catch (java.io.IOException ignored) {
                    }
                if (joined == 0)
                    throw new java.io.IOException(
                            "Nearby discovery could not join a connected interface");
                receiver.setSoTimeout(500);
                long deadline = android.os.SystemClock.elapsedRealtime() + 10000, lastProgress = 0;
                boolean changed = false;
                while (!closed && android.os.SystemClock.elapsedRealtime() < deadline) {
                    byte[] bytes = new byte[2049];
                    DatagramPacket packet = new DatagramPacket(bytes, bytes.length);
                    try {
                        receiver.receive(packet);
                        if (packet.getLength() > 2048
                                || !(packet.getAddress() instanceof Inet4Address)
                                || packet.getAddress().isLoopbackAddress()
                                || packet.getAddress().isMulticastAddress()) continue;
                        String[] decoded =
                                PeerService.decodeAnnouncement(
                                        Arrays.copyOf(bytes, packet.getLength()));
                        changed =
                                result.accept(
                                                decoded,
                                                packet.getAddress().getHostAddress(),
                                                android.os.SystemClock.elapsedRealtime())
                                        || changed;
                    } catch (SocketTimeoutException ignored) {
                    } catch (java.io.IOException malformed) {
                        if (receiver.isClosed()) break;
                    }
                    long now = android.os.SystemClock.elapsedRealtime();
                    if (changed && !closed && now - lastProgress >= 150) {
                        listener.progress(result.snapshot());
                        changed = false;
                        lastProgress = now;
                    }
                }
            }
        } catch (Exception e) {
            if (!closed) error = e.getMessage() == null ? "Nearby search failed" : e.getMessage();
        } finally {
            synchronized (this) {
                socket = null;
            }
            if (lock != null && lock.isHeld()) lock.release();
        }
        if (!closed) listener.finished(result, error);
    }

    public synchronized void close() {
        closed = true;
        if (socket != null) socket.close();
    }
}
