package io.github.eekebin.extrapane;

import java.io.*;
import java.nio.*;
import java.util.*;

public final class SpeakerCheck {
    static void require(boolean value, String reason) {
        if (!value) throw new AssertionError(reason);
    }

    interface Checked {
        void run() throws Exception;
    }

    static void rejects(Checked action) throws Exception {
        boolean failed = false;
        try {
            action.run();
        } catch (IOException expected) {
            failed = true;
        }
        require(failed, "malformed input accepted");
    }

    static byte[] frame(long sequence, long position, int flags) {
        ByteBuffer b = ByteBuffer.allocate(40 + 1920).order(ByteOrder.BIG_ENDIAN);
        b.putInt(0x45505344)
                .putShort((short) 1)
                .put((byte) 0)
                .put((byte) flags)
                .putLong(11)
                .putLong(22)
                .putLong(sequence)
                .putLong(position);
        Arrays.fill(b.array(), 40, b.capacity(), (byte) 1);
        return b.array();
    }

    static DataInputStream input(byte[] bytes) {
        return new DataInputStream(new ByteArrayInputStream(bytes));
    }

    static byte[] join(byte[] a, byte[] b) {
        byte[] result = Arrays.copyOf(a, a.length + b.length);
        System.arraycopy(b, 0, result, a.length, b.length);
        return result;
    }

    public static void main(String[] args) throws Exception {
        byte[] hello = SpeakerProtocol.hello(11, 22);
        require(
                hello.length == 32 && ByteBuffer.wrap(hello).getInt() == 0x4550534f,
                "native offer magic");
        require(ByteBuffer.wrap(hello).getInt(8) == 48000 && hello[7] == 2, "native stereo format");
        byte[] capability = SpeakerProtocol.capabilities();
        require(capability.length == 59, "five capability records");
        byte[] server = capability.clone();
        server[24 + 4 * 7 + 1] = 1;
        server[24 + 4 * 7 + 4] = 0;
        SpeakerProtocol.capabilities(input(server));
        rejects(() -> SpeakerProtocol.capabilities(input(capability)));
        byte[] duplicate = server.clone();
        duplicate[24 + 4 * 7] = 3;
        rejects(() -> SpeakerProtocol.capabilities(input(duplicate)));
        byte[] huge = server.clone();
        ByteBuffer.wrap(huge).putInt(16, Integer.MAX_VALUE);
        rejects(() -> SpeakerProtocol.capabilities(input(huge)));
        byte[] reserved = server.clone();
        reserved[23] = 1;
        rejects(() -> SpeakerProtocol.capabilities(input(reserved)));
        SpeakerProtocol protocol = new SpeakerProtocol(11, 22);
        SpeakerProtocol.Frame first = protocol.receive(input(frame(0, 0, 0)));
        require(!first.muted() && first.pcm()[0] == 1, "PCM data");
        SpeakerProtocol.Frame silent = protocol.receive(input(frame(1, 480, 1)));
        require(
                silent.muted() && Arrays.equals(silent.pcm(), new byte[1920]),
                "remote mute sanitizes PCM");
        protocol.receive(input(frame(2, 1920, 2)));
        rejects(() -> protocol.receive(input(frame(3, 2880, 0))));
        rejects(() -> protocol.receive(input(frame(3, 2400, 0))));
        rejects(() -> new SpeakerProtocol(11, 22).receive(input(frame(1, 0, 0))));
        byte[] wrong = frame(0, 0, 0);
        wrong[23] = 23;
        rejects(() -> new SpeakerProtocol(11, 22).receive(input(wrong)));
        rejects(
                () ->
                        new SpeakerProtocol(11, 22)
                                .receive(input(Arrays.copyOf(frame(0, 0, 0), 100))));
        rejects(() -> new SpeakerProtocol(11, 22).receive(input(frame(0, 0, 4))));
        byte[] tone = new byte[1920];
        for (int i = 0; i < tone.length; i += 4) {
            tone[i] = 100;
            tone[i + 2] = (byte) 200;
        }
        SpeakerJitter jitter = new SpeakerJitter();
        for (int i = 0; i < 3; i++) {
            jitter.offer(tone, false);
            require(Arrays.equals(jitter.take(), new short[960]), "must wait for40ms reserve");
        }
        jitter.offer(tone, false);
        short[] audio = jitter.take();
        require(
                audio[958] == 100 && audio[959] == 200,
                "stereo channels and gain preserved after fade");
        for (int i = 0; i < 4; i++) jitter.take();
        require(jitter.targetFrames() == 2880, "underrun adapts reserve to60ms");
        jitter.offer(tone, false);
        require(
                Arrays.equals(jitter.take(), new short[960]),
                "isolated late packet remains buffered");
        for (int i = 0; i < 40; i++) {
            jitter.offer(tone, false);
            require(jitter.queuedFrames() <= 5760, "hard120ms queue bound");
        }
        jitter.mute(true);
        jitter.offer(tone, false);
        jitter.mute(false);
        require(
                jitter.queuedFrames() == 0 && Arrays.equals(jitter.take(), new short[960]),
                "mute discards all previous samples");
        for (int i = 0; i < 4; i++) jitter.offer(tone, false);
        jitter.offer(tone, true);
        require(
                jitter.queuedFrames() == 480 && Arrays.equals(jitter.take(), new short[960]),
                "discontinuity invalidates buffered generation");
        SpeakerJitter other = new SpeakerJitter();
        for (int i = 0; i < 4; i++) other.offer(tone, false);
        jitter.reset();
        require(other.take()[958] == 100, "per-output independent state");
        long inFlight = other.epoch();
        other.mute(true);
        other.mute(false);
        require(
                !other.offerCurrent(tone, inFlight) && other.queuedFrames() == 0,
                "in-flight receive crossing mute/unmute is discarded");
        require(
                other.offerCurrent(tone, other.epoch()) && other.queuedFrames() == 480,
                "fresh receive after unmute accepted");
        long beforeVolume = other.epoch();
        other.reset();
        require(
                !other.offerCurrent(tone, beforeVolume),
                "volume/reset invalidates in-flight receive");
        System.out.println(
                "Speaker framing, malformed input, mute and jitter timing checks passed");
    }
}
