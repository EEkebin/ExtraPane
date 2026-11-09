package io.github.eekebin.extrapane;
public final class PairingAttemptCheck {
    private static void require(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args){
        PairingAttempt fresh=new PairingAttempt("10.0.0.1",43825,"10.0.0.2","peer",false,false);
        require(fresh.codeRequired&&!fresh.open);
        for(String invalid:new String[]{"","12345","1234567","abcdef","??????"}){
            boolean rejected=false;try{fresh.submit(invalid);}catch(IllegalArgumentException expected){rejected=true;}require(rejected);
        }
        require(fresh.submit("012345"));require(!fresh.submit("012345"));
        PairingAttempt cancelled=new PairingAttempt("10.0.0.1",43825,"10.0.0.2","peer",false,false);
        cancelled.cancel();require(!cancelled.submit("123456"));
        PairingAttempt saved=new PairingAttempt("10.0.0.3",1234,"10.0.0.2","saved-id",true,true);
        require(!saved.open&&!saved.codeRequired&&saved.submit(""));
        require(saved.address.equals("10.0.0.3")&&saved.port==1234&&saved.identity.equals("saved-id")&&saved.local.equals("10.0.0.2"));
        PairingAttempt unpaired=new PairingAttempt("127.0.0.1",43825,"127.0.0.1","",false,true);
        require(unpaired.open&&!unpaired.codeRequired&&unpaired.submit(""));
        System.out.println("Pairing modal lifecycle checks passed");
    }
}
