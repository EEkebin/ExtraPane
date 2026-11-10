package io.github.eekebin.extrapane;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.media.*;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/** AudioRecord owns its native microphone on one worker; no UI audio IPC. */
final class MicrophoneCapture implements AutoCloseable {
    record Choice(int id, String label) {
        public String toString() {
            return label;
        }
    }

    record Packet(byte[] pcm, long position, boolean muted, long captureStarted) {}

    interface Failure {
        void failed(Exception error);
    }

    static List<Choice> choices(Context context) {
        List<Choice> result = new ArrayList<>();
        for (AudioRouteCatalog.Entry route : AndroidAudioDevices.choices(context, true))
            result.add(new Choice(route.id(), route.label()));
        return result;
    }

    private final Context context;
    private final Choice choice;
    private final Failure failure;
    private final ArrayBlockingQueue<Packet> queue = new ArrayBlockingQueue<>(8);
    private volatile boolean stopped, muted;
    private volatile Exception error;
    private Thread worker;
    private DeviceCaptureLease sourceLease;

    MicrophoneCapture(Context context, Choice choice, Failure failure) {
        this.context = context.getApplicationContext();
        this.choice = choice;
        this.failure = failure;
    }

    synchronized void start() {
        if (worker != null || stopped)
            throw new IllegalStateException("Create a new microphone generation");
        sourceLease = DeviceCaptureLease.acquire("microphone:" + choice.id());
        worker = new Thread(this::capture, "ExtraPane microphone capture");
        try {
            worker.start();
        } catch (RuntimeException error) {
            sourceLease.close();
            throw error;
        }
    }

    synchronized void mute(boolean value) {
        muted = value;
        if (value) {
            List<Packet> old = new ArrayList<>();
            queue.drainTo(old);
            for (Packet p : old)
                queue.offer(new Packet(new byte[960], p.position(), true, p.captureStarted()));
        }
    }

    Packet next() throws Exception {
        if (error != null) throw error;
        Packet packet = queue.poll(100, TimeUnit.MILLISECONDS);
        if (error != null) throw error;
        return packet;
    }

    private void capture() {
        AudioRecord recorder = null;
        try {
            if (stopped) return;
            if (context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                    != PackageManager.PERMISSION_GRANTED)
                throw new IOException("Microphone permission is required");
            AudioDeviceInfo selected = null;
            for (AudioDeviceInfo device :
                    context.getSystemService(AudioManager.class)
                            .getDevices(AudioManager.GET_DEVICES_INPUTS))
                if (device.getId() == choice.id()) selected = device;
            if (selected == null)
                throw new IOException("Selected microphone is no longer connected");
            int minimum =
                    AudioRecord.getMinBufferSize(
                            48000, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
            if (minimum <= 0)
                throw new IOException("This Android device cannot record 48 kHz mono PCM16");
            if (stopped) return;
            recorder =
                    new AudioRecord.Builder()
                            .setAudioSource(MediaRecorder.AudioSource.MIC)
                            .setAudioFormat(
                                    new AudioFormat.Builder()
                                            .setSampleRate(48000)
                                            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                            .build())
                            .setBufferSizeInBytes(Math.max(minimum, 960 * 8))
                            .build();
            if (recorder.getState() != AudioRecord.STATE_INITIALIZED
                    || recorder.getSampleRate() != 48000
                    || !recorder.setPreferredDevice(selected))
                throw new IOException(
                        "Selected microphone cannot provide the requested format or route");
            if (stopped) return;
            recorder.startRecording();
            if (recorder.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING)
                throw new IOException("Microphone recording did not start");
            int nativeBufferFrames = recorder.getBufferSizeInFrames();
            AudioTimestamp hardwareTimestamp = new AudioTimestamp();
            short[] samples = new short[480];
            int filled = 0;
            long position = 0, last = System.nanoTime(), captureStarted = last;
            while (!stopped) {
                if (context.checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                        != PackageManager.PERMISSION_GRANTED)
                    throw new IOException("Microphone permission was withdrawn");
                if (filled == 0) {
                    long readStarted = System.nanoTime();
                    boolean timestampAvailable =
                            recorder.getTimestamp(
                                            hardwareTimestamp, AudioTimestamp.TIMEBASE_MONOTONIC)
                                    == AudioRecord.SUCCESS;
                    captureStarted =
                            FanoutPackets.microphoneCaptureStart(
                                    readStarted,
                                    nativeBufferFrames,
                                    position,
                                    hardwareTimestamp.framePosition,
                                    hardwareTimestamp.nanoTime,
                                    timestampAvailable);
                }
                int read =
                        recorder.read(
                                samples,
                                filled,
                                samples.length - filled,
                                AudioRecord.READ_NON_BLOCKING);
                if (read < 0)
                    throw new IOException("Microphone capture failed (AudioRecord " + read + ")");
                if (read == 0) {
                    if (System.nanoTime() - last > 2_000_000_000L)
                        throw new IOException("Microphone capture timed out");
                    Thread.sleep(5);
                    continue;
                }
                last = System.nanoTime();
                filled += read;
                AudioDeviceInfo routed = recorder.getRoutedDevice();
                if (routed == null || routed.getId() != choice.id())
                    throw new IOException(
                            "Android changed the selected microphone route; select it again");
                AudioRecordingConfiguration configuration =
                        recorder.getActiveRecordingConfiguration();
                if (configuration != null && configuration.isClientSilenced())
                    throw new IOException(
                            "Android has silenced this microphone; check privacy controls and other"
                                    + " recording apps");
                if (filled == 480) {
                    synchronized (this) {
                        if (stopped) break;
                        byte[] pcm = new byte[960];
                        if (!muted)
                            for (int i = 0; i < 480; ++i) {
                                pcm[i * 2] = (byte) samples[i];
                                pcm[i * 2 + 1] = (byte) (samples[i] >> 8);
                            }
                        if (!queue.offer(new Packet(pcm, position, muted, captureStarted)))
                            throw new IOException(
                                    "Microphone connection is too slow; bounded queue filled");
                    }
                    position += 480;
                    filled = 0;
                }
            }
        } catch (Exception cause) {
            if (!stopped) {
                error = cause;
                failure.failed(cause);
            }
        } finally {
            try {
                if (recorder != null) {
                    try {
                        if (recorder.getRecordingState() == AudioRecord.RECORDSTATE_RECORDING)
                            recorder.stop();
                    } catch (RuntimeException ignored) {
                    }
                    recorder.release();
                }
                sourceLease.close();
            } catch (RuntimeException cleanup) {
                error = cleanup;
                failure.failed(cleanup);
            }
        }
    }

    public void close() throws IOException {
        stopped = true;
        queue.clear();
        Thread capture = worker;
        if (capture != null) {
            capture.interrupt();
            try {
                capture.join(2500);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("Interrupted microphone cleanup", e);
            }
            if (capture.isAlive())
                throw new IOException("Microphone driver cleanup is still pending");
        }
    }
}
