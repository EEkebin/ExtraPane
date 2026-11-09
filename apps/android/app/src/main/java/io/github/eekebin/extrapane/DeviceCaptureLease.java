package io.github.eekebin.extrapane;

import java.util.concurrent.ConcurrentHashMap;
/** Held by the actual native-capture worker, including delayed vendor cleanup. */
final class DeviceCaptureLease implements AutoCloseable {
    private static final ConcurrentHashMap<String,DeviceCaptureLease> owners=new ConcurrentHashMap<>();
    private final String source;
    private DeviceCaptureLease(String source){this.source=source;}
    static DeviceCaptureLease acquire(String source){DeviceCaptureLease value=new DeviceCaptureLease(source);if(owners.putIfAbsent(source,value)!=null)throw new IllegalStateException("This source is still capturing or finishing driver cleanup");return value;}
    public void close(){owners.remove(source,this);}
}
