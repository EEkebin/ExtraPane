package io.github.eekebin.extrapane;

import java.io.IOException;
import java.nio.ByteBuffer;

/** Encoded bytes only. No preview bitmap or camera pixels pass through the UI. */
final class CameraPackets {
    static boolean independent(byte[] bytes) {
        boolean sps = false, pps = false, idr = false;
        for (int i = 0; i + 3 < bytes.length; i++)
            if (bytes[i] == 0 && bytes[i + 1] == 0) {
                int start =
                        bytes[i + 2] == 1
                                ? i + 3
                                : (bytes[i + 2] == 0 && i + 4 < bytes.length && bytes[i + 3] == 1
                                        ? i + 4
                                        : -1);
                if (start < 0) continue;
                int kind = bytes[start] & 31;
                if (kind == 7) sps = true;
                if (kind == 8) pps = true;
                if (kind == 5 && sps && pps) idr = true;
                i = start;
            }
        return idr;
    }

    static final int MAX_FRAME = 2 * 1024 * 1024;

    static byte[] copy(ByteBuffer buffer, int offset, int length) throws IOException {
        if (buffer == null
                || offset < 0
                || length < 1
                || length > MAX_FRAME
                || offset > buffer.capacity() - length)
            throw new IOException("Invalid encoded camera buffer");
        ByteBuffer view = buffer.duplicate();
        view.clear();
        view.position(offset);
        view.limit(offset + length);
        byte[] bytes = new byte[length];
        view.get(bytes);
        return bytes;
    }

    static byte[] annexB(byte[] bytes) throws IOException {
        if (bytes.length < 4 || bytes.length > MAX_FRAME)
            throw new IOException("Invalid H.264 camera packet");
        if (bytes[0] == 0 && bytes[1] == 0 && (bytes[2] == 1 || (bytes[2] == 0 && bytes[3] == 1)))
            return bytes;
        // Some platform codecs emit four-byte AVCC NAL lengths. Convert only
        // this explicitly validated representation to the shared Annex-B wire.
        byte[] converted = bytes.clone();
        int offset = 0;
        while (offset < bytes.length) {
            if (bytes.length - offset < 4) throw new IOException("Truncated AVC NAL length");
            long length =
                    ((bytes[offset] & 255L) << 24)
                            | ((bytes[offset + 1] & 255L) << 16)
                            | ((bytes[offset + 2] & 255L) << 8)
                            | (bytes[offset + 3] & 255L);
            if (length < 1 || length > bytes.length - offset - 4)
                throw new IOException("Invalid AVC NAL length");
            converted[offset] = converted[offset + 1] = converted[offset + 2] = 0;
            converted[offset + 3] = 1;
            offset += 4 + (int) length;
        }
        return converted;
    }

    static byte[] join(byte[] prefix, byte[] frame) throws IOException {
        if (prefix.length > 65536 || frame.length > MAX_FRAME - prefix.length)
            throw new IOException("Camera frame exceeds channel limit");
        byte[] result = new byte[prefix.length + frame.length];
        System.arraycopy(prefix, 0, result, 0, prefix.length);
        System.arraycopy(frame, 0, result, prefix.length, frame.length);
        return result;
    }
}
