package io.github.eekebin.extrapane;

/** One immutable destination and one submission; pairing codes are never retained. */
final class PairingAttempt {
    final String address, local, identity;
    final int port;
    final long gateway;
    final boolean open, codeRequired;
    private boolean finished;

    PairingAttempt(
            String address,
            int port,
            String local,
            String identity,
            boolean remembered,
            boolean open) {
        this.gateway = 0;
        this.address = address;
        this.port = port;
        this.local = local;
        this.identity = identity;
        this.open = !remembered && open;
        this.codeRequired = !remembered && !this.open;
    }

    private PairingAttempt(long gateway, String identity) {
        if (gateway <= 0 || identity == null || !identity.matches("[0-9a-f]{64}"))
            throw new IllegalArgumentException("Invalid endpoint pairing target");
        this.gateway = gateway;
        this.identity = identity;
        address = "";
        local = "";
        port = 0;
        open = false;
        codeRequired = true;
    }

    static PairingAttempt via(long gateway, String identity) {
        return new PairingAttempt(gateway, identity);
    }

    static boolean validSecret(String value) {
        if (value == null || value.isEmpty() || value.length() > 1024 || value.indexOf('\0') >= 0)
            return false;
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if (Character.isHighSurrogate(c)) {
                if (++i >= value.length() || !Character.isLowSurrogate(value.charAt(i)))
                    return false;
            } else if (Character.isLowSurrogate(c)) return false;
        }
        return value.getBytes(java.nio.charset.StandardCharsets.UTF_8).length <= 1024;
    }

    boolean submit(String code) {
        if (finished) return false;
        if (codeRequired && !validSecret(code))
            throw new IllegalArgumentException("Enter a PIN or password (up to 1024 UTF-8 bytes)");
        if (!codeRequired && code != null && !code.isEmpty())
            throw new IllegalArgumentException("This connection does not use a pairing code");
        finished = true;
        return true;
    }

    void cancel() {
        finished = true;
    }
}
