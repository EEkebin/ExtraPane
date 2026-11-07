package io.github.eekebin.extrapane;

import java.util.*;

/** Main-thread ownership/generation model, separate from Android capture APIs. */
final class CaptureRegistry {
    static final class Sources {private final Set<String> owned=new HashSet<>();}
    static final class Lease {final String row,source;private boolean finished,stopping;Lease(String row,String source){this.row=row;this.source=source;}boolean stopping(){return stopping;}}
    private final Sources sources;private final Map<String,Lease> rows=new LinkedHashMap<>();
    CaptureRegistry(Sources sources){this.sources=sources;}
    Lease begin(String row,String source){
        if(rows.containsKey(row))throw new IllegalStateException("This connection is still active or stopping");
        if(!sources.owned.add(source))throw new IllegalStateException("This source is already sharing or stopping. Choose another device; same-source fanout is not available yet.");
        Lease lease=new Lease(row,source);rows.put(row,lease);return lease;
    }
    boolean current(Lease lease){return lease!=null&&!lease.finished&&rows.get(lease.row)==lease;}
    List<Lease> stop(String row){ArrayList<Lease> result=new ArrayList<>();for(Lease lease:rows.values())if(row==null||lease.row.equals(row)){lease.stopping=true;result.add(lease);}return result;}
    void finish(Lease lease){if(lease==null||lease.finished)return;lease.finished=true;if(rows.remove(lease.row,lease))sources.owned.remove(lease.source);}
}
