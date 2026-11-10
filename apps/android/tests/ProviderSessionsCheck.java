package io.github.eekebin.extrapane;

public final class ProviderSessionsCheck {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        ProviderSessions state = new ProviderSessions();
        require(state.all().isEmpty(), "Available catalog does not imply active sessions");
        state.add(new ProviderSessions.Session(1, 1, "camera-front", "alice"));
        state.add(new ProviderSessions.Session(2, 1, "camera-front", "bob"));
        state.add(new ProviderSessions.Session(1, 2, "camera-back", "alice"));
        state.add(new ProviderSessions.Session(1, 3, "mic", "alice"));
        require(
                state.forResource("camera-front").size() == 2,
                "Same resource can show both remote users");
        for (ProviderSessions.Session session : state.forResource("camera-front"))
            state.remove(session.key());
        require(
                state.all().size() == 2 && state.contains("1:2") && state.contains("1:3"),
                "Stopping one exact camera preserves another camera and microphone");
        require(
                state.forResource("camera-front").isEmpty(),
                "Power returns idle after final resource grant ends");
        for (ProviderSessions.Session session : state.all()) state.remove(session.key());
        require(
                state.all().isEmpty(),
                "Disconnect all clears grants without a catalog/listener mutation");
        System.out.println("Android provider session checks passed");
    }
}
