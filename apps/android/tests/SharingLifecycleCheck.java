package io.github.eekebin.extrapane;

public final class SharingLifecycleCheck {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        SharingLifecycle state = new SharingLifecycle();
        require(!state.active() && !state.listening(), "Fresh app must not share");
        state.begin(1);
        require(state.active() && !state.listening(), "Pending bind is cancellable but not ready for media");
        state.stop();
        require(!state.confirm(1) && !state.active(), "Late confirmation after pause must not revive sharing");
        state.begin(2);
        require(!state.confirm(1) && !state.matches(1), "Canceled bind success/error cannot affect a new start");
        require(state.confirm(2) && state.listening(), "Only current bind confirmation enables media");
        require(!state.confirm(0), "Unscoped event must not activate sharing");
        state.stop(); // Listener error, network reconfiguration and pause share this path.
        require(!state.active() && !state.listening() && !state.matches(2), "Stop clears pending and listening intent");
        state.begin(3);
        boolean duplicateRejected=false;
        try { state.begin(4); } catch (IllegalStateException expected) { duplicateRejected=true; }
        require(duplicateRejected && state.matches(3), "Additional resources reuse the pending listener");
        state.stop();
        boolean invalidRejected=false;
        try { state.begin(0); } catch (IllegalArgumentException expected) { invalidRejected=true; }
        require(invalidRejected && !state.active(), "Failed native start cannot create listening intent");
        System.out.println("Android sharing lifecycle checks passed");
    }
}
