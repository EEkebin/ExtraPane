"""Disposable TLS pairing and rejection checks against the available laptop."""
import argparse
import ipaddress
import json
import secrets
import subprocess
import uuid

from windows_peer import ROOT, WindowsPeer, TestFirewall, quote


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--laptop-ip', required=True, type=ipaddress.IPv4Address)
    p.add_argument('--desktop-ip', required=True, type=ipaddress.IPv4Address)
    p.add_argument('--user', required=True)
    args = p.parse_args()
    laptop_ip, desktop_ip = str(args.laptop_ip), str(args.desktop_ip)
    exe = ROOT / 'out/build/windows-release/dm-session.exe'
    peer = WindowsPeer(laptop_ip, args.user)
    name = 'test-' + uuid.uuid4().hex
    secret = secrets.token_hex(32) + '\n'
    remote_exe = None
    results = {}

    def local(*words, key=None):
        return subprocess.run([str(exe), *words], input=key, capture_output=True, text=True, timeout=20)

    def forward(expected=True):
        listener = subprocess.Popen([str(exe), 'listen', '--name', name, '--bind', desktop_ip, '--timeout-ms', '20000'],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            line = listener.stdout.readline()
            if not line.startswith('LISTENING '):
                raise RuntimeError('Desktop secure listener failed: ' + line)
            code, out, err = peer.run(f'& {quote(remote_exe)} connect --name {name} --bind {laptop_ip} --peer {desktop_ip} --timeout-ms 5000', required=False, interactive=True)
            server_out, server_err = listener.communicate(timeout=22)
            if expected:
                if code or listener.returncode or 'SESSION_OK' not in out or 'SESSION_OK' not in server_out:
                    raise RuntimeError(f'Secure forward failed: {out} {err} {server_out} {server_err}')
            elif code == 0 or listener.returncode == 0 or 'SESSION_OK' in out or 'SESSION_OK' in server_out:
                raise RuntimeError('Incorrect key authenticated')
            return {'peer': out, 'server': server_out.strip(), 'rejected': not expected}
        finally:
            if listener.poll() is None:
                listener.kill()
                listener.communicate(timeout=4)

    try:
        remote_exe = peer.upload(exe)
        result = local('pair', '--name', name, '--secret-stdin', key=secret)
        if result.returncode:
            raise RuntimeError('Local pairing failed: ' + result.stderr)
        peer.run(f'$secret | & {quote(remote_exe)} pair --name {name} --secret-stdin', secret=secret, interactive=True)
        with TestFirewall(peer, desktop_ip, exe, remote_exe, 43823):
            results['laptop_to_desktop'] = forward()
            print('Laptop -> desktop authenticated', flush=True)

            stdin, out, err = peer.start(f'& {quote(remote_exe)} listen --name {name} --bind {laptop_ip} --timeout-ms 15000', interactive=True)
            stdin.channel.shutdown_write()
            try:
                if not out.readline().startswith('LISTENING '):
                    raise RuntimeError('Laptop TLS listener failed')
                result = local('connect', '--name', name, '--bind', desktop_ip, '--peer', laptop_ip, '--timeout-ms', '5000')
                server_out = out.read().decode('utf-8', errors='replace')
                server_err = err.read().decode('utf-8', errors='replace')
                code = out.channel.recv_exit_status()
                if code or result.returncode or 'SESSION_OK' not in server_out:
                    raise RuntimeError('Secure reverse failed: ' + result.stderr + server_out + server_err)
                results['desktop_to_laptop'] = {'peer': result.stdout.strip(), 'server': server_out.strip()}
                print('Desktop -> laptop authenticated', flush=True)
            finally:
                out.channel.close()

            peer.run(f'& {quote(remote_exe)} revoke --name {name}', interactive=True)
            peer.run(f'$secret | & {quote(remote_exe)} pair --name {name} --secret-stdin', secret=secrets.token_hex(32) + '\n', interactive=True)
            results['wrong_key'] = forward(expected=False)
            print('Same pairing identity with wrong key rejected at TLS handshake', flush=True)
            peer.run(f'& {quote(remote_exe)} revoke --name {name}', interactive=True)
            code, out, err = peer.run(f'& {quote(remote_exe)} listen --name {name} --bind {laptop_ip} --timeout-ms 200', required=False, interactive=True)
            if code == 0 or 'LISTENING' in out:
                raise RuntimeError('Revoked laptop pairing opened a listener')
            results['revoked_pairing_rejected'] = True
            print('Revoked pairing rejected before opening a port', flush=True)
    finally:
        local('revoke', '--name', name)
        if remote_exe:
            peer.run(f'& {quote(remote_exe)} revoke --name {name}', interactive=True)
        peer.close()
        del secret
    results['scope'] = 'TLS1.3 PSK-DHE diagnostic; no media or input permissions'
    (ROOT / 'out/laptop-security-test.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('Security laptop checks passed; disposable credentials revoked', flush=True)


if __name__ == '__main__':
    main()
