package io.github.eekebin.extrapane;

/** Persistent opt-out policy, separate from permission and active stream state. */
final class ResourceSharing {
    interface Store {
        boolean enabled(String id);

        void save(String id, boolean enabled);
    }

    private final Store store;

    ResourceSharing(Store store) {
        this.store = store;
    }

    static String audioKey(boolean input, int type, String address, String product) {
        address = address == null ? "" : address;
        product = product == null ? "" : product;
        return (input ? "microphone:" : "speaker:")
                + type
                + ":"
                + address.length()
                + ":"
                + address
                + ":"
                + product;
    }

    boolean enabled(String id) {
        return !id.contains(":unavailable:") && store.enabled(id);
    }

    boolean allowed(String id, boolean permission) {
        return permission && enabled(id);
    }

    void set(String id, boolean enabled, Runnable revoke, Runnable publish) {
        store.save(id, enabled); // A queued start must see the denial before teardown.
        if (!enabled) revoke.run();
        publish.run(); // Enabling only advertises metadata; it never resumes media.
    }
}
