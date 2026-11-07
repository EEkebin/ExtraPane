package io.github.eekebin.extrapane;
public final class ConnectionValidationCheck {
    private static void require(boolean value){if(!value)throw new AssertionError("Connection validation");}
    public static void main(String[] args){
        for(String address:new String[]{"10.0.20.130","192.168.1.2","172.29.1.1"})require(ConnectionValidation.ipv4(address));
        for(String address:new String[]{"","localhost","127.0.0.1","0.0.0.0","224.0.0.1","255.255.255.255","1.2.3.256","1.2.3","1.2.3.4.5","1.2.3.-1"})require(!ConnectionValidation.ipv4(address));
        require(ConnectionValidation.port("1")&&ConnectionValidation.port("65535"));for(String p:new String[]{"","0","65536","999999999999","-1","1.5"})require(!ConnectionValidation.port(p));
        require(ConnectionValidation.code("")&&ConnectionValidation.code("012345"));for(String code:new String[]{"12345","abcdef","1234567","123 56"})require(!ConnectionValidation.code(code));
        require(ConnectionValidation.dimension(1920)&&ConnectionValidation.dimension(4096)&&!ConnectionValidation.dimension(4097)&&!ConnectionValidation.dimension(1919));
        System.out.println("Android connection validation checks passed");
    }
}
