"""Use disposable current-user credentials; always revoke them after tests."""
import secrets
import socket
import ssl
import subprocess
import sys
import uuid

EXE = sys.argv[1]


def run(*args, secret=None):
    return subprocess.run([EXE, *args], input=secret, capture_output=True, text=True, timeout=8)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return str(s.getsockname()[1])


def listener(name, timeout=2000):
    selected = port()
    p = subprocess.Popen([EXE, 'listen', '--name', name, '--bind', '127.0.0.1',
                          '--port', selected, '--timeout-ms', str(timeout)],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    ready = p.stdout.readline()
    if not ready.startswith('LISTENING '):
        out, err = p.communicate(timeout=4)
        raise AssertionError(f'Listener startup: {ready}{out}{err}')
    return p, selected


def finish(p, success):
    try:
        out, err = p.communicate(timeout=5)
        check((p.returncode == 0) == success, (p.returncode, out, err))
        check(('SESSION_OK' in out) == success, (out, err))
        return out, err
    finally:
        if p.poll() is None:
            p.kill()
            p.communicate(timeout=3)


def openssl_client(selected, name, key, maximum=ssl.TLSVersion.TLSv1_3):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    # Require PSK authentication; no certificate fallback is trusted.
    ctx.minimum_version = ctx.maximum_version = maximum
    ctx.set_psk_client_callback(lambda hint: (name, key))
    raw = socket.create_connection(('127.0.0.1', int(selected)), timeout=3)
    try:
        return ctx.wrap_socket(raw)
    except BaseException:
        raw.close()
        raise


def main():
    name = 'test-' + uuid.uuid4().hex
    key = secrets.token_bytes(32)
    try:
        result = run('pair', '--name', name, '--secret-stdin', secret=key.hex() + '\n')
        check(result.returncode == 0 and 'PAIRING_SAVED' in result.stdout, result)
        check(name in run('peers').stdout, 'Pairing not listed')
        result = run('pair', '--name', name, '--secret-stdin', secret=secrets.token_hex(32) + '\n')
        check(result.returncode != 0 and 'already exists' in result.stderr, 'Silent trust overwrite')

        p, selected = listener(name)
        result = run('connect', '--name', name, '--bind', '127.0.0.1', '--peer', '127.0.0.1', '--port', selected)
        check(result.returncode == 0 and 'TLSv1.3' in result.stdout and 'permissions=none' in result.stdout, result)
        finish(p, True)

        # Neither a cleartext diagnostic nor an idle TCP peer gets a session.
        for payload in (b'DMNP\x01\x01\x00\x00abcdefgh', b''):
            p, selected = listener(name, timeout=250)
            with socket.create_connection(('127.0.0.1', int(selected)), timeout=2) as s:
                if payload:
                    s.sendall(payload)
                _, err = finish(p, False)
                if not payload:
                    check('timed out' in err, err)

        if hasattr(ssl.SSLContext, 'set_psk_client_callback'):
            # Independent TLS implementation with the same identity and wrong key.
            for identity, psk, version in (
                (name, secrets.token_bytes(32), ssl.TLSVersion.TLSv1_3),
                ('wrong-identity', key, ssl.TLSVersion.TLSv1_3),
                (name, key, ssl.TLSVersion.TLSv1_2),
            ):
                p, selected = listener(name)
                rejected = False
                try:
                    with openssl_client(selected, identity, psk, version) as s:
                        s.sendall(b'not authorized')
                        s.recv(16)
                except (ssl.SSLError, OSError):
                    rejected = True
                check(rejected, 'Unauthorized or downgraded TLS connection succeeded')
                finish(p, False)

            # Valid OpenSSL TLS-PSK client, then malformed application data.
            p, selected = listener(name)
            with openssl_client(selected, name, key) as s:
                check(s.session_reused, 'OpenSSL did not authenticate using PSK')
                s.sendall(b'BAD!\x01\x01\x00\x00abcdefgh')
                try:
                    s.recv(16)
                except OSError:
                    pass
            _, err = finish(p, False)
            check('not a DM' in err, err)
        else:
            print('Independent TLS wrong-key/downgrade checks require Python 3.13+ with PSK support')

        run('revoke', '--name', name)
        check(name not in run('peers').stdout, 'Revoked peer still listed')
        result = run('listen', '--name', name, '--bind', '127.0.0.1', '--timeout-ms', '150')
        check(result.returncode != 0 and 'LISTENING' not in result.stdout, 'Revoked trust opened a listener')

        for value in ('', '00', 'z' * 64, 'f' * 4096):
            result = run('pair', '--name', name, '--secret-stdin', secret=value + '\n')
            check(result.returncode != 0, 'Malformed pairing key accepted')
        print('Security integration passed: TLS1.3, trust storage, rejection, revocation, deadlines')
    finally:
        run('revoke', '--name', name)


if __name__ == '__main__':
    main()
