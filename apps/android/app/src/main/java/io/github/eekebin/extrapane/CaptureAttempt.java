package io.github.eekebin.extrapane;

import java.util.UUID;
/** Gates asynchronous permission replies and retained service snapshots. */
final class CaptureAttempt {
    private String current="",completed="";
    String begin(){current=UUID.randomUUID().toString();completed="";return current;}
    boolean current(String value){return current.equals(value);}
    void cancelPending(){begin();}
    boolean accept(String value,boolean active){
        if(value==null)value="";
        if(!current.isEmpty()&&!current.equals(value))return false;
        if(!active&&!completed.isEmpty()&&completed.equals(value))return false;
        current=value;if(!active)completed=value;return true;
    }
}
