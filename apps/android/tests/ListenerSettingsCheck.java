package io.github.eekebin.extrapane;
public final class ListenerSettingsCheck {
    static void check(boolean value){if(!value)throw new AssertionError("Listener settings regression");}
    static void rejects(Runnable action){try{action.run();throw new AssertionError("Invalid listener accepted");}catch(IllegalArgumentException expected){}}
    public static void main(String[] args){
        check(ListenerSettings.DEFAULT_PORT==43824);check(ListenerSettings.address("0.0.0.0").equals("0.0.0.0"));
        check(ListenerSettings.address("localhost").equals("127.0.0.1"));check(ListenerSettings.address(" 010.000.020.143 ").equals("10.0.20.143"));
        rejects(()->ListenerSettings.address("example.com"));rejects(()->ListenerSettings.address("239.1.1.1"));rejects(()->ListenerSettings.address("10.0.0.300"));
        check(ListenerSettings.port("43824")==43824);rejects(()->ListenerSettings.port("0"));rejects(()->ListenerSettings.port("65536"));
        SharingLifecycle state=new SharingLifecycle();state.begin(1);state.confirm(1);state.replace(2);check(!state.confirm(1));check(state.confirm(2));
        state.replace(3);check(!state.matches(2));check(state.confirm(3));state.stop();check(!state.confirm(3));
        state.begin(4);state.confirm(4);state.replace(4);state.confirm(4);check(state.listening());
        System.out.println("Listener settings and replacement checks passed");
    }
}
