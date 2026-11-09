package io.github.eekebin.extrapane;
import java.util.*;
public final class InventorySnapshotsCheck {
    static void check(boolean value){if(!value)throw new AssertionError("Inventory snapshot regression");}
    public static void main(String[] args){
        Map<Integer,List<String>> previous=Map.of(1,List.of("active camera"),2,List.of("mic"));
        Map<Integer,List<String>> query=new HashMap<>();query.put(1,null);query.put(2,List.of("mic","USB mic"));
        var next=InventorySnapshots.merge(previous,query);
        check(next.get(1).equals(previous.get(1)));check(next.get(2).size()==2);check(previous.get(2).size()==1);
        check(InventorySnapshots.merge(next,Map.of(1,List.of("active camera"))).equals(next));
        var unplugged=InventorySnapshots.merge(next,Map.of(2,List.of("mic")));
        check(unplugged.get(1).equals(previous.get(1)));check(unplugged.get(2).equals(List.of("mic")));
        var removed=InventorySnapshots.merge(unplugged,Map.of(1,List.of()));check(removed.get(1).isEmpty());
        var unavailable=InventorySnapshots.merge(Map.<Integer,List<String>>of(),query);check(!unavailable.containsKey(1));
        System.out.println("Inventory snapshot checks passed");
    }
}
