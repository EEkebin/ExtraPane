package io.github.eekebin.extrapane;

final class AccessChannel implements AutoCloseable {
    final java.io.InputStream input = null;
    final java.io.OutputStream output = null;

    AccessChannel(java.io.DataInputStream in, java.io.OutputStream out, String code) {
        throw new AssertionError("JNI transport must not run in lifecycle checks");
    }

    public void close() {}
}
