package io.github.eekebin.extrapane;
final class ListenerSettings {
    static final int DEFAULT_PORT=43824;
    static String address(String text){
        String value=ConnectionValidation.normalizedAddress(text.trim());
        if(value.equals("0.0.0.0"))return value;
        if(!ConnectionValidation.ipv4(value))throw new IllegalArgumentException("Enter a local IPv4 address or localhost");
        String[] parts=value.split("\\.");StringBuilder normalized=new StringBuilder();
        for(String part:parts){if(normalized.length()>0)normalized.append('.');normalized.append(Integer.parseInt(part));}
        return normalized.toString();
    }
    static int port(String text){if(!ConnectionValidation.port(text.trim()))throw new IllegalArgumentException("Enter a port from 1 to 65535");return Integer.parseInt(text.trim());}
}
