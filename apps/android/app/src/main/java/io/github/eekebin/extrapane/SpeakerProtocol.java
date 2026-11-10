package io.github.eekebin.extrapane;

import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Bounded Java codec for the native speaker protocol. No media crosses UI IPC. */
final class SpeakerProtocol {
    static final int PCM_BYTES = 1920, FRAMES = 480;

    record Frame(byte[] pcm, boolean muted, boolean discontinuity) {}

    private final long resource, generation;
    private long sequence, position;
    private boolean first = true, failed;

    SpeakerProtocol(long resource, long generation) {
        if (resource <= 0 || generation <= 0)
            throw new IllegalArgumentException("Invalid speaker binding");
        this.resource = resource;
        this.generation = generation;
    }

    private static ByteBuffer buffer(int size) {
        return ByteBuffer.allocate(size).order(ByteOrder.BIG_ENDIAN);
    }

    static byte[] hello(long resource, long generation) {
        return buffer(32)
                .put(new byte[] {'E', 'P', 'S', 'O'})
                .putShort((short) 1)
                .put((byte) 1)
                .put((byte) 2)
                .putInt(48000)
                .putShort((short) 480)
                .putShort((short) 0)
                .putLong(resource)
                .putLong(generation)
                .array();
    }

    private static byte[] message(int operation, int kind, long id, byte[] payload) {
        return buffer(24 + payload.length)
                .put(new byte[] {'E', 'P', 'R', 'S'})
                .putShort((short) 1)
                .put((byte) operation)
                .put((byte) kind)
                .putLong(id)
                .putInt(payload.length)
                .putInt(0)
                .put(payload)
                .array();
    }

    static byte[] capabilities() {
        ByteBuffer b = buffer(35);
        for (int i = 0; i < 5; i++)
            b.put((byte) i)
                    .put((byte) 0)
                    .putShort((short) 0)
                    .put((byte) (i == 4 ? 1 : 0))
                    .putShort((short) 0);
        return message(1, 0, 0, b.array());
    }

    static byte[] request(long resource) {
        byte[] name = "speakers".getBytes(StandardCharsets.UTF_8);
        return message(
                2,
                4,
                resource,
                buffer(2 + name.length).putShort((short) name.length).put(name).array());
    }

    private record Control(int operation, int kind, long id, byte[] payload) {}

    private static Control control(DataInputStream in) throws IOException {
        byte[] h = new byte[24];
        in.readFully(h);
        ByteBuffer b = ByteBuffer.wrap(h).order(ByteOrder.BIG_ENDIAN);
        if (b.getInt() != 0x45505253 || b.getShort() != 1)
            throw new IOException("Resource protocol mismatch; update both endpoints");
        int operation = b.get() & 255, kind = b.get() & 255;
        long id = b.getLong();
        int count = b.getInt();
        if (operation < 1
                || operation > 6
                || kind > 4
                || count < 0
                || count > 65536
                || b.getInt() != 0
                || (operation == 1 ? id != 0 : id == 0)
                || (operation == 5 && count != 0))
            throw new IOException("Invalid speaker control envelope");
        byte[] payload = new byte[count];
        in.readFully(payload);
        return new Control(operation, kind, id, payload);
    }

    static void capabilities(DataInputStream in) throws IOException {
        Control c = control(in);
        if (c.operation() != 1 || c.kind() != 0 || c.id() != 0)
            throw new IOException("Invalid speaker capabilities");
        DataInputStream p = new DataInputStream(new ByteArrayInputStream(c.payload()));
        int seen = 0;
        boolean supported = false;
        while (p.available() > 0) {
            int kind = p.readUnsignedByte();
            if (kind > 4 || (seen & (1 << kind)) != 0)
                throw new IOException("Invalid capability kinds");
            seen |= 1 << kind;
            boolean source = availability(p);
            availability(p);
            if (kind == 4) supported = source;
        }
        if (seen != 15 && seen != 31) throw new IOException("Incomplete capabilities");
        if (!supported)
            throw new IOException(
                    "Server speaker output is unavailable; update the server and install its audio"
                            + " component");
    }

    private static boolean availability(DataInputStream in) throws IOException {
        int flag = in.readUnsignedByte(), size = in.readUnsignedShort();
        if (flag > 1 || size > 1024 || size > in.available())
            throw new IOException("Invalid capability availability");
        byte[] reason = new byte[size];
        in.readFully(reason);
        for (byte value : reason) if (value == 0) throw new IOException("Invalid capability text");
        return flag == 1;
    }

    static void grant(DataInputStream in, long resource) throws IOException {
        Control c = control(in);
        if (c.kind() != 4 || c.id() != resource)
            throw new IOException("Invalid speaker grant binding");
        if (c.operation() == 4)
            throw new IOException(
                    "Server could not start its speaker output; check the server status");
        if (c.operation() != 3 || c.payload().length != 0)
            throw new IOException("Speaker request was not granted");
    }

    Frame receive(DataInputStream in) throws IOException {
        if (failed) throw new IOException("Speaker channel is closed");
        try {
            byte[] header = new byte[40];
            in.readFully(header);
            ByteBuffer b = ByteBuffer.wrap(header).order(ByteOrder.BIG_ENDIAN);
            if (b.getInt() != 0x45505344 || b.getShort() != 1 || b.get() != 0)
                throw new IOException("Invalid speaker frame");
            int flags = b.get() & 255;
            if (flags > 3
                    || b.getLong() != resource
                    || b.getLong() != generation
                    || b.getLong() != sequence
                    || sequence == Long.MAX_VALUE)
                throw new IOException("Speaker frame binding or sequence mismatch");
            long next = b.getLong();
            boolean gap = (flags & 2) != 0;
            if (next < 0
                    || next > Long.MAX_VALUE - FRAMES
                    || (!first && (next < position || (!gap && next != position))))
                throw new IOException("Speaker sample position mismatch");
            byte[] pcm = new byte[PCM_BYTES];
            in.readFully(pcm);
            boolean muted = (flags & 1) != 0;
            if (muted) Arrays.fill(pcm, (byte) 0);
            ++sequence;
            position = next + FRAMES;
            first = false;
            return new Frame(pcm, muted, gap);
        } catch (IOException error) {
            failed = true;
            throw error;
        }
    }
}
