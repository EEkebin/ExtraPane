package android.net;

public final class Network {
    private final long handle;

    public Network(long value) {
        handle = value;
    }

    public long getNetworkHandle() {
        return handle;
    }

    public void bindSocket(java.net.Socket socket) {}
}
