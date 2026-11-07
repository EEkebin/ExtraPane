package io.github.eekebin.extrapane;
import java.io.IOException;
final class MicrophoneProtocol {
    static{System.loadLibrary("extrapane");}
    static native byte[] hello(long resource,long generation)throws IOException;
    static native byte[] capabilities()throws IOException;
    static native byte[] request(long resource)throws IOException;
    static native int payloadLength(byte[] header)throws IOException;
    static native void validateCapabilities(byte[] header,byte[] payload)throws IOException;
    static native void validateGrant(byte[] header,byte[] payload,long resource)throws IOException;
    static native byte[] frameHeader(long resource,long generation,long sequence,long position,boolean muted)throws IOException;
}
