package io.github.eekebin.extrapane;

import android.content.Context;

/** Keeps pixels/audio in native producers; UI controls only subscriptions. */
final class SharedCaptures {
    private static final CaptureFanout<CameraCapture.Packet> cameras =
            new CaptureFanout<>(3, packet -> CameraPackets.independent(packet.bytes()));
    private static final CaptureFanout<MicrophoneCapture.Packet> microphones =
            new CaptureFanout<>(8, packet -> true);

    static CaptureFanout<CameraCapture.Packet>.Subscription camera(
            Context context, CameraCapture.Choice choice) throws Exception {
        return cameras.subscribe(
                choice.id(),
                choice.width() + "x" + choice.height() + "@" + choice.fps(),
                () ->
                        new CaptureFanout.Producer<>() {
                            final CameraCapture capture =
                                    new CameraCapture(context, choice, error -> {});

                            public void start() throws Exception {
                                capture.start();
                            }

                            public CameraCapture.Packet next() throws Exception {
                                return capture.next();
                            }

                            public void close() throws Exception {
                                capture.close();
                            }
                        });
    }

    static CaptureFanout<MicrophoneCapture.Packet>.Subscription microphone(
            Context context,
            MicrophoneCapture.Choice choice,
            java.util.function.LongPredicate muted)
            throws Exception {
        return microphones.subscribe(
                Integer.toString(choice.id()),
                "48000/mono/PCM16",
                () ->
                        new CaptureFanout.Producer<>() {
                            final MicrophoneCapture capture =
                                    new MicrophoneCapture(context, choice, error -> {});

                            public void start() {
                                capture.start();
                            }

                            public MicrophoneCapture.Packet next() throws Exception {
                                return capture.next();
                            }

                            public void close() throws Exception {
                                capture.close();
                            }
                        },
                packet ->
                        new MicrophoneCapture.Packet(
                                packet.pcm(),
                                packet.position(),
                                packet.muted() || muted.test(packet.captureStarted()),
                                packet.captureStarted()));
    }
}
