package io.github.eekebin.extrapane;

final class Protocol {
    static { System.loadLibrary("extrapane"); }
    static native byte[] request(int width,int height,int hz,int caps,int scale);
    static native byte[] customRequest(int width,int height,int hz,int caps,int scale);
    static native long[] offer(byte[] bytes);
    static native long[] frame(byte[] bytes);
    static native byte[] input(long epoch,long sequence,int[] events);
    static final byte[] READY={'R','D','Y','9'};
    private Protocol() {}
}
