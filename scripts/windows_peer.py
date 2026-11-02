"""Shared Windows SSH test helpers. Credentials are prompted, never logged."""
import base64
import getpass
import hashlib
from pathlib import Path
import subprocess
import uuid

import paramiko

ROOT = Path(__file__).resolve().parent.parent


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def powershell(script):
    encoded = base64.b64encode(("$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; " + script).encode('utf-16-le')).decode('ascii')
    return 'powershell.exe -NoProfile -NonInteractive -EncodedCommand ' + encoded


def local_ps(script):
    r = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                        "$ErrorActionPreference='Stop'; " + script], capture_output=True, text=True, timeout=20)
    if r.returncode:
        raise RuntimeError('Local command failed: ' + r.stderr.strip())
    return r.stdout.strip()


def interactive_wrapper(script, timeout=45):
    """Stream a temporary interactive scheduled task over an owner-only named pipe.

    One stdin line is passed in memory (used for pairing), never in task args or
    files. Tasks and pipe handles are removed in finally, including on failure.
    """
    name = 'DM-Test-' + uuid.uuid4().hex
    child = r'''
$ErrorActionPreference='Stop'
$pipe=[System.IO.Pipes.NamedPipeClientStream]::new('.', PIPE_NAME, [System.IO.Pipes.PipeDirection]::InOut)
try {
 $pipe.Connect(10000)
 $reader=[System.IO.StreamReader]::new($pipe,[System.Text.UTF8Encoding]::new($false),$false,4096,$true)
 $writer=[System.IO.StreamWriter]::new($pipe,[System.Text.UTF8Encoding]::new($false),4096,$true)
 $writer.AutoFlush=$true
 $secret=$reader.ReadLine()
 $global:LASTEXITCODE=0
 try {
  & { COMMAND_SCRIPT } 2>&1 | ForEach-Object { $writer.WriteLine($_.ToString()) }
  $writer.WriteLine('__DM_EXIT:' + $global:LASTEXITCODE)
 } catch { $writer.WriteLine('DM_TASK_ERROR: ' + $_.Exception.Message); $writer.WriteLine('__DM_EXIT:1') }
 $secret=$null
} finally { $pipe.Dispose() }
'''.replace('PIPE_NAME', quote(name)).replace('COMMAND_SCRIPT', script)
    encoded_child = base64.b64encode(child.encode('utf-16-le')).decode('ascii')
    parent = r'''
$pipeName=PIPE_NAME
$sid=[System.Security.Principal.WindowsIdentity]::GetCurrent().User
$security=[System.IO.Pipes.PipeSecurity]::new()
$security.AddAccessRule([System.IO.Pipes.PipeAccessRule]::new($sid,[System.IO.Pipes.PipeAccessRights]::FullControl,[System.Security.AccessControl.AccessControlType]::Allow))
$security.SetOwner($sid)
$pipe=[System.IO.Pipes.NamedPipeServerStream]::new($pipeName,[System.IO.Pipes.PipeDirection]::InOut,1,[System.IO.Pipes.PipeTransmissionMode]::Byte,[System.IO.Pipes.PipeOptions]::Asynchronous,4096,4096,$security)
$exitCode=1
try {
 $action=New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand CHILD_COMMAND')
 $principal=New-ScheduledTaskPrincipal -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
 $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds TASK_TIMEOUT_SECONDS) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
 Register-ScheduledTask -TaskName $pipeName -Action $action -Principal $principal -Settings $settings | Out-Null
 $waiting=$pipe.BeginWaitForConnection($null,$null)
 Start-ScheduledTask -TaskName $pipeName
 if(-not $waiting.AsyncWaitHandle.WaitOne(15000)){throw 'Interactive task did not connect; confirm the laptop user is signed in'}
 $pipe.EndWaitForConnection($waiting)
 $waiting.AsyncWaitHandle.Close()
 $reader=[System.IO.StreamReader]::new($pipe,[System.Text.UTF8Encoding]::new($false),$false,4096,$true)
 $writer=[System.IO.StreamWriter]::new($pipe,[System.Text.UTF8Encoding]::new($false),4096,$true)
 $writer.AutoFlush=$true
 $writer.WriteLine([Console]::ReadLine())
 while($null -ne ($line=$reader.ReadLine())) {
  if($line.StartsWith('__DM_EXIT:')){$exitCode=[int]$line.Substring(10); break}
  [Console]::WriteLine($line)
 }
} finally {
 Stop-ScheduledTask -TaskName $pipeName -ErrorAction SilentlyContinue
 Unregister-ScheduledTask -TaskName $pipeName -Confirm:$false -ErrorAction SilentlyContinue
 $pipe.Dispose()
}
exit $exitCode
'''
    return parent.replace('PIPE_NAME', quote(name)).replace('CHILD_COMMAND', encoded_child).replace('TASK_TIMEOUT_SECONDS',str(timeout+15))


class WindowsPeer:
    def __init__(self, host, user):
        self.ssh = paramiko.SSHClient()
        self.ssh.load_host_keys(str(ROOT / 'out/ssh_known_hosts'))
        self.ssh.set_missing_host_key_policy(paramiko.RejectPolicy())
        password = getpass.getpass('Laptop SSH password (not saved): ')
        try:
            self.ssh.connect(host, username=user, password=password, look_for_keys=False,
                             allow_agent=False, timeout=6, auth_timeout=8, banner_timeout=6)
        finally:
            del password
        self.host = host

    def start(self, script, interactive=False, timeout=45):
        if not isinstance(timeout,int) or not 1<=timeout<=900:
            raise ValueError('SSH command timeout must be 1..900 seconds')
        if interactive:
            # The default Windows SSH shell has an ~8K command-line limit.
            # Upload only non-secret task code, then invoke it with a short bootstrap.
            with self.ssh.open_sftp() as sftp:
                home = sftp.normalize('.')
                task_file = home.rstrip('/') + '/AppData/Local/DM/network-test/task-' + uuid.uuid4().hex + '.ps1'
                with sftp.file(task_file, 'w') as f:
                    f.write(interactive_wrapper(script,timeout))
            native_file = task_file.lstrip('/').replace('/', '\\')
            script = (f'try {{ & ([scriptblock]::Create([System.IO.File]::ReadAllText({quote(native_file)}))) }} '
                      + f'finally {{ Remove-Item -LiteralPath {quote(native_file)} -ErrorAction SilentlyContinue }}')
        return self.ssh.exec_command(powershell(script), timeout=timeout)

    def run(self, script, secret=None, required=True, interactive=False, timeout=45):
        stdin, out, err = self.start(script, interactive=interactive,timeout=timeout)
        if secret is not None:
            stdin.write(secret)
            stdin.flush()
        stdin.channel.shutdown_write()
        result = out.read().decode('utf-8', errors='replace').strip()
        error = err.read().decode('utf-8', errors='replace').strip()
        code = out.channel.recv_exit_status()
        if required and code:
            raise RuntimeError(f'Remote command failed ({code}): {result}\n{error}')
        return code, result, error

    def upload(self, exe):
        _, directory, _ = self.run("$p=Join-Path $env:LOCALAPPDATA 'DM\\network-test'; New-Item -ItemType Directory -Path $p -Force | Out-Null; $p")
        destination = directory + '\\' + exe.name
        with self.ssh.open_sftp() as sftp:
            sftp.put(str(exe), '/' + destination.replace('\\', '/').lstrip('/'))
        _, digest, _ = self.run(f'(Get-FileHash -Algorithm SHA256 -LiteralPath {quote(destination)}).Hash')
        if digest.lower() != hashlib.sha256(exe.read_bytes()).hexdigest():
            raise RuntimeError('Transferred executable hash mismatch')
        return destination

    def close(self):
        self.ssh.close()


class TestFirewall:
    def __init__(self, peer, desktop_ip, local_exe, remote_exe, port):
        self.peer = peer
        self.name = 'DM-Network-Test-' + uuid.uuid4().hex
        self.desktop_ip = desktop_ip
        self.local_exe, self.remote_exe, self.port = local_exe, remote_exe, port

    def __enter__(self):
        try:
            common = (f'New-NetFirewallRule -Name {quote(self.name)} -DisplayName {quote(self.name)}'
                      + f' -Direction Inbound -Action Allow -Protocol TCP -LocalPort {self.port} -Profile Any')
            local_ps(common + f' -LocalAddress {self.desktop_ip} -RemoteAddress {self.peer.host} -Program {quote(self.local_exe)} | Out-Null')
            self.peer.run(common + f' -LocalAddress {self.peer.host} -RemoteAddress {self.desktop_ip} -Program {quote(self.remote_exe)} | Out-Null')
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *args):
        remove = f'Get-NetFirewallRule -Name {quote(self.name)} -ErrorAction SilentlyContinue | Remove-NetFirewallRule'
        errors = []
        for action in (lambda: self.peer.run(remove), lambda: local_ps(remove)):
            try:
                action()
            except Exception as e:
                errors.append(str(e))
        if errors:
            raise RuntimeError('Temporary firewall cleanup failed for ' + self.name + ': ' + '; '.join(errors))
        print('Temporary test firewall rules removed', flush=True)
