"""Run the network diagnostic on a Windows SSH peer; prompt for its password.

Requires paramiko on the development PC and a previously accepted SSH known-host
entry. Only a diagnostic executable is transferred. Temporary per-peer firewall
rules are removed after testing; no credentials are saved.
"""
import argparse
import base64
import getpass
import hashlib
import ipaddress
import json
from pathlib import Path
import subprocess
import uuid

import paramiko


ROOT = Path(__file__).resolve().parent.parent


def ps_command(script):
    encoded = base64.b64encode(script.encode('utf-16-le')).decode('ascii')
    return f'powershell.exe -NoProfile -NonInteractive -EncodedCommand {encoded}'


def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--laptop-ip', required=True, type=ipaddress.IPv4Address)
    parser.add_argument('--desktop-ip', required=True, type=ipaddress.IPv4Address)
    parser.add_argument('--user', required=True)
    parser.add_argument('--known-hosts', type=Path, default=ROOT / 'out/ssh_known_hosts')
    args = parser.parse_args()
    laptop_ip, desktop_ip = str(args.laptop_ip), str(args.desktop_ip)
    executable = ROOT / 'out/build/windows-release/dm-net.exe'
    if not executable.is_file():
        raise RuntimeError('Build the Release diagnostic first')
    if not args.known_hosts.is_file():
        raise RuntimeError('Establish the laptop SSH known-host entry first using OpenSSH')

    ssh = paramiko.SSHClient()
    ssh.load_host_keys(str(args.known_hosts))
    ssh.set_missing_host_key_policy(paramiko.RejectPolicy())

    def remote(script, timeout=20):
        _, out, err = ssh.exec_command(ps_command("$ErrorActionPreference='Stop'; " + script), timeout=timeout)
        output = out.read().decode('utf-8', errors='replace').strip()
        error = err.read().decode('utf-8', errors='replace').strip()
        code = out.channel.recv_exit_status()
        if code:
            raise RuntimeError(f'Remote command failed ({code}): {output}\n{error}')
        return output

    def local_ps(script):
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                                 "$ErrorActionPreference='Stop'; " + script],
                                text=True, capture_output=True, timeout=20)
        if result.returncode:
            raise RuntimeError(f'Local command failed: {result.stderr.strip()}')
        return result.stdout.strip()

    rule = 'DM-Network-Test-' + uuid.uuid4().hex
    cleanup_local = cleanup_remote = False
    results = {}
    try:
        password = getpass.getpass('Laptop SSH password (not saved): ')
        try:
            ssh.connect(laptop_ip, username=args.user, password=password,
                        look_for_keys=False, allow_agent=False, timeout=6,
                        auth_timeout=8, banner_timeout=6)
        finally:
            del password
        print('Connected:', remote('whoami'), flush=True)
        remote_dir = remote("$p=Join-Path $env:LOCALAPPDATA 'DM\\network-test'; New-Item -ItemType Directory -Path $p -Force | Out-Null; $p")
        remote_exe = remote_dir + '\\dm-net.exe'
        with ssh.open_sftp() as sftp:
            destination = remote_exe.replace('\\', '/')
            if not destination.startswith('/'):
                destination = '/' + destination
            sftp.put(str(executable), destination)
        expected_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
        actual_hash = remote(f'(Get-FileHash -Algorithm SHA256 -LiteralPath {ps_quote(remote_exe)}).Hash').lower()
        if actual_hash != expected_hash:
            raise RuntimeError('Transferred executable hash did not match')
        print('Transfer SHA256 verified', flush=True)
        results['laptop_interfaces'] = remote(f'& {ps_quote(remote_exe)} interfaces; exit $LASTEXITCODE')
        print(results['laptop_interfaces'], flush=True)

        # Prove the executable runs locally on the laptop before testing routing.
        loopback = (
            f'$exe={ps_quote(remote_exe)}; '
            "$job=Start-Job -ArgumentList $exe -ScriptBlock { param($exe) & $exe listen --bind 127.0.0.1 --port 43822 --timeout-ms 10000 }; "
            "try { Start-Sleep -Milliseconds 1500; & $exe probe --bind 127.0.0.1 --peer 127.0.0.1 --port 43822 --timeout-ms 5000; "
            "if($LASTEXITCODE -ne 0){throw 'Laptop loopback probe failed'}; "
            "$finished=Wait-Job $job -Timeout 12; if(-not $finished){throw 'Laptop loopback listener stalled'}; Receive-Job $job } "
            "finally { Stop-Job $job -ErrorAction SilentlyContinue; Remove-Job $job -Force -ErrorAction SilentlyContinue }"
        )
        results['laptop_loopback'] = remote(loopback, timeout=25)
        print('Laptop loopback:', results['laptop_loopback'], flush=True)

        common = ' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 43821 -Profile Any'
        # Mark cleanup before mutations so ambiguous command completion still cleans up.
        cleanup_local = True
        local_ps(f'New-NetFirewallRule -Name {ps_quote(rule)} -DisplayName {ps_quote(rule)}'
                 + common + f' -LocalAddress {desktop_ip} -RemoteAddress {laptop_ip}'
                 + f' -Program {ps_quote(executable)} | Out-Null')
        cleanup_remote = True
        remote(f'New-NetFirewallRule -Name {ps_quote(rule)} -DisplayName {ps_quote(rule)}'
               + common + f' -LocalAddress {laptop_ip} -RemoteAddress {desktop_ip}'
               + f' -Program {ps_quote(remote_exe)} | Out-Null')

        listener = subprocess.Popen([str(executable), 'listen', '--bind', desktop_ip, '--timeout-ms', '15000'],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            ready = listener.stdout.readline().strip()
            if not ready.startswith('LISTENING '):
                raise RuntimeError('Desktop listener failed: ' + ready)
            print(ready, flush=True)
            results['laptop_to_desktop'] = remote(f'& {ps_quote(remote_exe)} probe --bind {laptop_ip} --peer {desktop_ip} --timeout-ms 5000; exit $LASTEXITCODE')
            out, err = listener.communicate(timeout=18)
            if listener.returncode or 'PROBE_OK' not in out:
                raise RuntimeError('Desktop listener did not confirm probe: ' + out + err)
            results['desktop_listener'] = out.strip()
            print('Laptop -> desktop:', results['laptop_to_desktop'], flush=True)
        finally:
            if listener.poll() is None:
                listener.kill()
                listener.communicate(timeout=4)

        _, out, err = ssh.exec_command(ps_command(f'& {ps_quote(remote_exe)} listen --bind {laptop_ip} --timeout-ms 15000; exit $LASTEXITCODE'), timeout=20)
        try:
            ready = out.readline().strip()
            if not ready.startswith('LISTENING '):
                raise RuntimeError('Laptop listener failed: ' + ready)
            print(ready, flush=True)
            result = subprocess.run([str(executable), 'probe', '--bind', desktop_ip, '--peer', laptop_ip, '--timeout-ms', '5000'],
                                    text=True, capture_output=True, timeout=8)
            if result.returncode:
                raise RuntimeError('Desktop probe failed: ' + result.stderr.strip())
            results['desktop_to_laptop'] = result.stdout.strip()
            listener_output = out.read().decode('utf-8', errors='replace').strip()
            listener_error = err.read().decode('utf-8', errors='replace').strip()
            if out.channel.recv_exit_status() or 'PROBE_OK' not in listener_output:
                raise RuntimeError('Laptop listener did not confirm probe: ' + listener_output + listener_error)
            results['laptop_listener'] = listener_output
            print('Desktop -> laptop:', results['desktop_to_laptop'], flush=True)
        finally:
            out.channel.close()

        results['status'] = 'passed'
        results['scope'] = 'TCP diagnostic only; no authenticated session, media, or input'
    finally:
        cleanup_errors = []
        if cleanup_remote:
            try:
                remote(f'Get-NetFirewallRule -Name {ps_quote(rule)} -ErrorAction SilentlyContinue | Remove-NetFirewallRule')
            except Exception as error:
                cleanup_errors.append('Laptop firewall cleanup: ' + str(error))
        if cleanup_local:
            try:
                local_ps(f'Get-NetFirewallRule -Name {ps_quote(rule)} -ErrorAction SilentlyContinue | Remove-NetFirewallRule')
            except Exception as error:
                cleanup_errors.append('Desktop firewall cleanup: ' + str(error))
        ssh.close()
        if cleanup_errors:
            raise RuntimeError('\n'.join(cleanup_errors) + '\nRule name: ' + rule)
        if cleanup_local or cleanup_remote:
            print('Temporary test firewall rules removed', flush=True)

    report = ROOT / 'out/laptop-network-test.json'
    report.write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('Results:', report, flush=True)


if __name__ == '__main__':
    main()
