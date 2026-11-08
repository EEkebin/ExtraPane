package io.github.eekebin.extrapane;

import java.util.*;

/** Untrusted discovery hints. Pairing, never discovery, authenticates their identity. */
final class NearbyPeers {
    record Hint(String id,String name,String address,int port,boolean pairingRequired) {}
    private record Entry(Hint hint,String instance,long sequence,long expires) {}
    private final Map<String,Entry> entries=new LinkedHashMap<>();
    private final Set<String> knownOnly;
    NearbyPeers(){knownOnly=null;}
    NearbyPeers(Set<String> known){knownOnly=known==null?null:Set.copyOf(known);}
    void accept(String[] row,String address,long now){
        if(row.length<8||knownOnly!=null&&!knownOnly.contains(row[0]))return;
        try{
            String id=row[0];long sequence=Long.parseUnsignedLong(row[2]);int lifetime=Integer.parseInt(row[5]);
            Entry old=entries.get(id);if(old!=null&&old.instance().equals(row[1])&&Long.compareUnsigned(sequence,old.sequence())<=0)return;
            if(old==null&&entries.size()>=128)return;
            int port=Integer.parseInt(row[4]);if(lifetime>0&&(port<1||port>65535))return;
            entries.put(id,new Entry(new Hint(id,row[3],address,port,Boolean.parseBoolean(row[7])),row[1],sequence,now+Math.max(0,Math.min(30,lifetime))*1000L));
        }catch(NumberFormatException ignored){}
    }
    List<Hint> visible(String self,Set<String> known,long now){List<Hint> result=new ArrayList<>();for(Entry entry:entries.values())if(entry.expires()>now&&!entry.hint().id().equals(self)&&!known.contains(entry.hint().id()))result.add(entry.hint());return result;}
}
