package io.github.eekebin.extrapane;

import java.util.Set;

public final class NearbyPeersCheck {
    private static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static String[] row(String id,String seq,String lifetime){return new String[]{id,"1",seq,"Computer","43825",lifetime,"1","true"};}
    public static void main(String[] args){
        NearbyPeers peers=new NearbyPeers();
        require(peers.visible("self",Set.of(),0).isEmpty(),"No scan means no nearby results");
        peers.accept(row("self","1","15"),"10.0.0.1",0);
        peers.accept(row("paired","1","15"),"10.0.0.2",0);
        peers.accept(row("connected","1","15"),"10.0.0.3",0);
        peers.accept(row("new","1","15"),"10.0.0.4",0);
        require(peers.visible("self",Set.of("paired","connected"),1).size()==1,"Self, paired and connected identities are excluded");
        peers.accept(row("new","3","0"),"10.0.0.4",2);
        peers.accept(row("new","2","15"),"10.0.0.4",3);
        require(peers.visible("self",Set.of("paired","connected"),4).isEmpty(),"Withdrawal suppresses replayed older announcement");
        peers.accept(row("new","4","15"),"10.0.0.4",5);
        require(peers.visible("self",Set.of("paired","connected"),6).size()==1,"New announcement restores hint");
        require(peers.visible("self",Set.of(),16000).isEmpty(),"Expired scan results disappear");
        require(new NearbyPeers().visible("self",Set.of(),0).isEmpty(),"Manual refresh starts from an empty result set");
        java.util.Set<String> remembered=new java.util.HashSet<>(Set.of("paired"));
        NearbyPeers known=new NearbyPeers(remembered);remembered.add("unknown");
        known.accept(row("paired","1","15"),"10.0.0.2",0);known.accept(row("unknown","1","15"),"10.0.0.3",0);
        require(known.visible("self",Set.of(),1).size()==1&&known.visible("self",Set.of(),1).get(0).id().equals("paired"),"Passive observation only retains immutable remembered identity allowlist");
        System.out.println("Android manual nearby search checks passed");
    }
}
