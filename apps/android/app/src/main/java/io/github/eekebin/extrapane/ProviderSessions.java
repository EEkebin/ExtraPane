package io.github.eekebin.extrapane;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Actual native grants, kept separate from the catalog of available resources. */
final class ProviderSessions {
    record Session(long connection, long grant, String resource, String peer) {
        String key() {
            return connection + ":" + grant;
        }
    }

    private final Map<String, Session> sessions = new LinkedHashMap<>();

    void add(Session session) {
        sessions.put(session.key(), session);
    }

    boolean contains(String key) {
        return sessions.containsKey(key);
    }

    void remove(String key) {
        sessions.remove(key);
    }

    void clear() {
        sessions.clear();
    }

    List<Session> all() {
        return new ArrayList<>(sessions.values());
    }

    List<Session> forResource(String resource) {
        List<Session> result = new ArrayList<>();
        for (Session session : sessions.values())
            if (session.resource().equals(resource)) result.add(session);
        return result;
    }
}
