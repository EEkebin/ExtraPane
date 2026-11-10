package io.github.eekebin.extrapane;

import java.io.IOException;
import java.net.Socket;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.Set;

/** TLS close can send close_notify and must never run on the UI/service thread. */
final class SocketCancellation {
    private final Set<Socket> closing = Collections.newSetFromMap(new IdentityHashMap<>());

    synchronized void close(Socket socket) {
        if (socket == null || !closing.add(socket)) return;
        // Each session publishes at most a raw socket and its TLS wrapper.
        // Independent closers prevent one blocked TLS shutdown holding up others.
        Thread closer =
                new Thread(
                        () -> {
                            try {
                                socket.close();
                            } catch (IOException | RuntimeException ignored) {
                            }
                        },
                        "ExtraPane socket cancellation");
        closer.setDaemon(true);
        closer.start();
    }
}
