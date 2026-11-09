package io.github.eekebin.extrapane;

/** One immutable destination and one submission; pairing codes are never retained. */
final class PairingAttempt {
    final String address, local, identity;
    final int port;
    final boolean open, codeRequired;
    private boolean finished;
    PairingAttempt(String address,int port,String local,String identity,boolean remembered,boolean open) {
        this.address=address;this.port=port;this.local=local;this.identity=identity;
        this.open=!remembered&&open;this.codeRequired=!remembered&&!this.open;
    }
    boolean submit(String code) {
        if(finished)return false;
        if(codeRequired&&(code==null||!code.matches("[0-9]{6}")))throw new IllegalArgumentException("Enter the six-digit pairing code");
        if(!codeRequired&&code!=null&&!code.isEmpty())throw new IllegalArgumentException("This connection does not use a pairing code");
        finished=true;return true;
    }
    void cancel(){finished=true;}
}
