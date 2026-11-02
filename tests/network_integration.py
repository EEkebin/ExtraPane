"""Loopback behavior checks; no firewall, adapter, or machine settings changed."""
import concurrent.futures
import socket
import subprocess
import sys
import time

EXE = sys.argv[1]


def run(*args):
    return subprocess.run([EXE, *args], capture_output=True, text=True, timeout=8)


def check(condition, detail):
    if not condition:
        raise AssertionError(detail)


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return str(s.getsockname()[1])


def start_listener(port, timeout=8000):
    p = subprocess.Popen(
        [EXE, 'listen', '--bind', '127.0.0.1', '--port', port, '--timeout-ms', str(timeout)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    # Readiness has a deadline so failed startup cannot hang the suite.
    try:
        with concurrent.futures.ThreadPoolExecutor() as pool:
            line_future = pool.submit(p.stdout.readline)
            try:
                line = line_future.result(timeout=4)
            except concurrent.futures.TimeoutError:
                p.kill()
                raise AssertionError('Listener did not announce readiness')
        if not line.startswith('LISTENING '):
            raise AssertionError(f'Listener startup failed: {line}')
        return p
    except BaseException:
        if p.poll() is None:
            p.kill()
        p.communicate(timeout=3)
        raise


def collect(p):
    try:
        stdout, stderr = p.communicate(timeout=5)
        return p.returncode, stdout, stderr
    finally:
        if p.poll() is None:
            p.kill()
            p.communicate(timeout=3)


def fake_peer(response):
    with socket.socket() as server:
        server.bind(('127.0.0.1', 0))
        server.listen(1)
        server.settimeout(4)
        port = str(server.getsockname()[1])
        with concurrent.futures.ThreadPoolExecutor() as pool:
            future = pool.submit(run, 'probe', '--bind', '127.0.0.1', '--peer', '127.0.0.1',
                                 '--port', port, '--timeout-ms', '1200')
            with server.accept()[0] as client:
                client.settimeout(3)
                packet = b''
                while len(packet) < 16:
                    chunk = client.recv(16 - len(packet))
                    check(bool(chunk), 'Probe closed before sending packet')
                    packet += chunk
                client.sendall(response(packet))
            return future.result(timeout=5)


def main():
    result = run('interfaces')
    check(result.returncode == 0 and '127.0.0.1/' in result.stdout, result)

    port = free_port()
    p = start_listener(port)
    try:
        # An exclusive listener must not be silently replaced.
        occupied = run('listen', '--bind', '127.0.0.1', '--port', port, '--timeout-ms', '150')
        check(occupied.returncode != 0 and 'bind failed' in occupied.stderr, occupied)
        result = run('probe', '--bind', '127.0.0.1', '--peer', '127.0.0.1', '--port', port)
        check(result.returncode == 0 and 'PROBE_OK' in result.stdout and 'not paired' in result.stdout, result)
        code, out, err = collect(p)
        check(code == 0 and 'PROBE_OK' in out, (code, out, err))
    finally:
        if p.poll() is None:
            p.kill()
            collect(p)

    # TCP fragmentation must not be treated as a truncated application packet.
    port = free_port()
    p = start_listener(port)
    packet = b'DMNP\x01\x01\x00\x00abcdefgh'
    with socket.create_connection(('127.0.0.1', int(port)), timeout=2) as s:
        for chunk in (packet[:1], packet[1:7], packet[7:]):
            s.sendall(chunk)
        received = b''
        while len(received) < 16:
            chunk = s.recv(16 - len(received))
            check(bool(chunk), 'Listener closed before full response')
            received += chunk
        check(received == packet[:5] + b'\x02' + packet[6:], received)
    check(collect(p)[0] == 0, 'Fragmented probe failed')

    for payload, expected in ((b'BAD!' + packet[4:], 'not a DM'), (packet[:4], 'full diagnostic packet')):
        port = free_port()
        p = start_listener(port)
        with socket.create_connection(('127.0.0.1', int(port)), timeout=2) as s:
            s.sendall(payload)
            s.shutdown(socket.SHUT_WR)
        code, _, err = collect(p)
        check(code != 0 and expected in err, (code, err))

    port = free_port()
    p = start_listener(port, timeout=300)
    with socket.create_connection(('127.0.0.1', int(port)), timeout=2) as s:
        s.sendall(b'D')
        code, _, err = collect(p)
        check(code != 0 and 'timed out' in err, (code, err))

    port = free_port()
    p = start_listener(port, timeout=150)
    code, _, err = collect(p)
    check(code != 0 and 'accept timed out' in err, (code, err))

    result = fake_peer(lambda p: p[:5] + b'\x02' + p[6:8] + bytes(x ^ 255 for x in p[8:]))
    check(result.returncode != 0 and 'did not match' in result.stderr, result)
    result = fake_peer(lambda p: p)  # Wrong direction is not a successful echo.
    check(result.returncode != 0 and 'did not match' in result.stderr, result)

    for args in (
        ('probe', '--bind', '0.0.0.0', '--peer', '127.0.0.1'),
        ('probe', '--bind', '127.0.0.1', '--peer', 'localhost'),
        ('probe', '--bind', '203.0.113.231', '--peer', '127.0.0.1'),
        ('listen', '--bind', '127.0.0.1', '--port', '65536'),
        ('listen', '--bind', '127.0.0.1', '--port', '0'),
        ('listen', '--bind', '127.0.0.1', '--timeout-ms', '60001'),
        ('listen', '--bind', '127.0.0.1', '--bind', '127.0.0.1'),
        ('listen', '--bind', '127.0.0.1', '--surprise', '1'),
    ):
        result = run(*args)
        check(result.returncode != 0, (args, result))

    started = time.monotonic()
    result = run('probe', '--bind', '127.0.0.1', '--peer', '127.0.0.1', '--port', free_port(), '--timeout-ms', '300')
    check(result.returncode != 0 and 'connect' in result.stderr and time.monotonic() - started < 3, result)
    print('Network integration passed: binding, handshake, fragmentation, validation, deadlines, refusal')


if __name__ == '__main__':
    main()
