package io.github.eekebin.extrapane;

import android.app.*;
import android.content.*;
import android.content.pm.ServiceInfo;
import android.net.Network;
import android.os.*;

import java.util.*;

/** Playback lives independently of the Activity and other resource services. */
public final class SpeakerService extends Service {
    static final String STOP = "speakers.stop",
            MUTE = "speakers.mute",
            UNMUTE = "speakers.unmute",
            VOLUME = "speakers.volume";

    record Snapshot(
            String id,
            boolean active,
            boolean connected,
            boolean stopping,
            boolean muted,
            int volume,
            String status,
            String destination,
            String detail,
            Intent configuration) {}

    interface Listener {
        void changed(List<Snapshot> values);
    }

    final class LocalBinder extends Binder {
        void listen(Listener value) {
            listener = value;
            publish();
        }
    }

    private final LocalBinder binder = new LocalBinder();
    private Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final LinkedHashMap<String, Entry> entries = new LinkedHashMap<>();
    private boolean destroyed;
    private static final int NOTIFICATION = 14;
    private static final String CHANNEL = "speaker-playback";

    private final class Entry {
        String id, status = "Connecting...", destination, detail;
        Intent config;
        SpeakerSession worker;
        boolean active, connected, stopping, muted;
        int volume = 100;

        Snapshot snapshot() {
            return new Snapshot(
                    id,
                    active,
                    connected,
                    stopping,
                    muted,
                    volume,
                    status,
                    destination,
                    detail,
                    new Intent(config));
        }
    }

    public IBinder onBind(Intent intent) {
        return binder;
    }

    private boolean active() {
        for (Entry e : entries.values()) if (e.active) return true;
        return false;
    }

    private Notification notification() {
        long count = entries.values().stream().filter(e -> e.active).count();
        PendingIntent open =
                PendingIntent.getActivity(
                        this,
                        0,
                        new Intent(this, MainActivity.class)
                                .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop =
                PendingIntent.getService(
                        this,
                        NOTIFICATION,
                        new Intent(this, SpeakerService.class).setAction(STOP),
                        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_lock_silent_mode_off)
                .setContentTitle("ExtraPane speakers")
                .setContentText(count + " output connection(s)")
                .setContentIntent(open)
                .setOngoing(true)
                .setCategory(Notification.CATEGORY_SERVICE)
                .addAction(
                        new Notification.Action.Builder(null, "Disconnect speakers", stop).build())
                .build();
    }

    private void publish() {
        if (destroyed) return;
        if (listener != null) {
            List<Snapshot> values = new ArrayList<>();
            for (Entry e : entries.values()) values.add(e.snapshot());
            listener.changed(values);
        }
        if (active())
            getSystemService(NotificationManager.class).notify(NOTIFICATION, notification());
    }

    private void ended(Entry e, String reason) {
        e.active = e.connected = e.stopping = false;
        e.worker = null;
        e.status = reason;
        if (entries.get(e.id) == e) publish();
        if (!active() && !destroyed) {
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
        }
    }

    @SuppressWarnings("deprecation")
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            if (!active()) stopSelf();
            return START_NOT_STICKY;
        }
        String action = intent.getAction(), id = intent.getStringExtra("row");
        if (STOP.equals(action)
                || MUTE.equals(action)
                || UNMUTE.equals(action)
                || VOLUME.equals(action)) {
            for (Entry e : entries.values())
                if (e.active && (id == null || e.id.equals(id))) {
                    if (STOP.equals(action)) {
                        e.stopping = true;
                        e.status = "Stopping...";
                        e.worker.cancel();
                    } else if (VOLUME.equals(action)) {
                        e.volume = Math.max(0, Math.min(100, intent.getIntExtra("volume", 100)));
                        e.worker.volume(e.volume);
                    } else {
                        e.muted = MUTE.equals(action);
                        e.worker.mute(e.muted);
                    }
                }
            publish();
            if (!active()) stopSelf();
            return START_NOT_STICKY;
        }
        if (id == null || id.isEmpty()) {
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        Entry prior = entries.get(id);
        if (prior != null && prior.active) return START_NOT_STICKY;
        Entry e = new Entry();
        e.id = id;
        e.config = new Intent(intent);
        e.config.removeExtra("code");
        e.destination = intent.getStringExtra("address") + ":" + intent.getIntExtra("port", 0);
        e.detail = intent.getStringExtra("outputLabel");
        entries.put(id, e);
        if (entries.size() > 128) {
            Iterator<Entry> iterator = entries.values().iterator();
            while (entries.size() > 128 && iterator.hasNext()) {
                Entry old = iterator.next();
                if (old != e && !old.active) iterator.remove();
            }
        }
        getSystemService(NotificationManager.class)
                .createNotificationChannel(
                        new NotificationChannel(
                                CHANNEL, "Speaker playback", NotificationManager.IMPORTANCE_LOW));
        try {
            startForeground(
                    NOTIFICATION,
                    notification(),
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
            String address = intent.getStringExtra("address"),
                    local = intent.getStringExtra("local"),
                    code = intent.getStringExtra("code");
            int port = intent.getIntExtra("port", 0), output = intent.getIntExtra("output", -1);
            Network network = intent.getParcelableExtra("network");
            intent.removeExtra("code");
            if (address == null
                    || local == null
                    || code == null
                    || !ConnectionValidation.ipv4(address)
                    || port < 1
                    || port > 65535
                    || output < 0
                    || e.detail == null)
                throw new IllegalArgumentException("Invalid speaker destination or output");
            e.worker =
                    new SpeakerSession(
                            this,
                            address,
                            port,
                            local,
                            network,
                            code,
                            new SpeakerSession.Choice(output, e.detail),
                            new SpeakerSession.Listener() {
                                public void connected() {
                                    main.post(
                                            () -> {
                                                if (entries.get(e.id) == e
                                                        && e.active
                                                        && !e.stopping) {
                                                    e.connected = true;
                                                    publish();
                                                }
                                            });
                                }

                                public void status(String text) {
                                    main.post(
                                            () -> {
                                                if (entries.get(e.id) == e
                                                        && e.active
                                                        && !e.stopping) {
                                                    e.status = text;
                                                    publish();
                                                }
                                            });
                                }

                                public void ended(String reason) {
                                    main.post(() -> SpeakerService.this.ended(e, reason));
                                }
                            });
            e.active = true;
            publish();
            e.worker.start();
        } catch (RuntimeException error) {
            ended(
                    e,
                    error.getMessage() == null
                            ? "Speaker output could not start"
                            : error.getMessage());
        }
        return START_NOT_STICKY;
    }

    public boolean onUnbind(Intent intent) {
        listener = null;
        return true;
    }

    public void onRebind(Intent intent) {
        publish();
    }

    public void onDestroy() {
        destroyed = true;
        listener = null;
        for (Entry e : entries.values()) if (e.worker != null) e.worker.cancel();
        super.onDestroy();
    }
}
