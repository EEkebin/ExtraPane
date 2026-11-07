"""Real driver-store/trust lifecycle checks, exclusively on disposable GitHub runners.

Never run this against either development PC. Does not create a virtual monitor,
change boot/security policy, or claim that staging proves runtime loading.
"""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid
import winreg

from release_version import current
from test_uninstall_data import registered_uninstaller

DRIVER_KEY = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen.VirtualDisplayDriver_is1'

def ps(script, check=True):
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                           "$ErrorActionPreference='Stop'; $env:PSModulePath=Join-Path $PSHOME 'Modules'; "
                           "Import-Module Microsoft.PowerShell.Security -ErrorAction Stop; " + script],
                          text=True, capture_output=True, timeout=180)
    if check and result.returncode:
        raise RuntimeError(f'PowerShell failed ({result.returncode}): {result.stdout}\n{result.stderr}')
    return result

def quote(value):
    return "'" + str(value).replace("'", "''") + "'"

def run(exe, *args, success=True):
    logs = Path(__file__).resolve().parent.parent/'out/driver-installer-logs'
    logs.mkdir(exist_ok=True)
    log = logs/(uuid.uuid4().hex+'.log')
    result = subprocess.run([str(exe), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', f'/LOG={log}', *args], timeout=240)
    if (result.returncode == 0) != success and log.exists():
        print(log.read_text(encoding='utf-8-sig', errors='replace')[-12000:], flush=True)
    assert (result.returncode == 0) == success, (exe, result.returncode, args)

def driver_uninstaller():
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, DRIVER_KEY, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
        command = winreg.QueryValueEx(key, 'UninstallString')[0]
    assert command.startswith('"')
    return Path(command.split('"')[1])

def driver_entry_exists():
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, DRIVER_KEY, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY):
            return True
    except FileNotFoundError:
        return False

def wait_absent(path):
    for _ in range(100):
        if not path.exists(): return
        time.sleep(.1)
    raise AssertionError(f'Uninstaller left {path}')

def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit('Driver integration checks are restricted to disposable GitHub runners.')
    root = Path(__file__).resolve().parent.parent
    folder = Path(os.environ['ProgramFiles']) / 'ExtraPane/Components/Display'
    assert not folder.exists(), 'Refusing to overwrite an existing driver installation'
    baseline = ps("@(Get-WindowsDriver -Online -All | Where-Object {$_.ProviderName -eq 'DM Project'}).Count").stdout.strip()
    assert baseline == '0', 'Refusing to touch pre-existing DM driver packages'
    installer = root / 'out/dist' / f'ExtraPane-{current()}-Setup-win-x64.exe'
    driver_setup = root / 'out/driver-installer/ExtraPane.DriverSetup.exe'
    manifest = json.loads((root/'out/driver-package/manifest.json').read_text(encoding='utf-8-sig'))
    thumb = manifest['certificateThumbprint']
    cert = root/'out/driver-package/ExtraPane-Driver.cer'
    # A missing Cert provider makes Test-Path silently return false. Verify the
    # stores themselves exist before using it to assert certificate presence.
    assert ps("(Test-Path Cert:\\LocalMachine\\Root) -and (Test-Path Cert:\\LocalMachine\\TrustedPublisher)").stdout.strip() == 'True'
    trust_count = f"@('Root','TrustedPublisher' | Where-Object {{Test-Path ('Cert:\\LocalMachine\\'+$_+'\\{thumb}')}}).Count"
    assert ps(trust_count).stdout.strip() == '0'
    # No implicit trust consent, including silent installs.
    run(driver_setup, success=False)
    assert not folder.exists()
    with tempfile.TemporaryDirectory(prefix='ExtraPane-host-install-') as temp:
        target = Path(os.environ['ProgramFiles'])/'ExtraPane'
        assert not (target/'ExtraPane.exe').exists(), 'Refusing existing app'
        run(installer, '/SERVER=1', f'/DIR={target}', '/TASKS=', success=False)
        assert not folder.exists() and not (target/'ExtraPane.exe').exists()
        # A real manager failure returns failure but leaves the main app and
        # uninstaller available for repair; no child installer is invoked.
        # Hold the manager's mutex to reject installation before trust/store changes.
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
        kernel.CreateMutexW.restype = wintypes.HANDLE
        kernel.ReleaseMutex.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        # This compatibility mutex intentionally retains the pre-rename name,
        # matching packaging/manage-driver.ps1 and existing installations.
        lock = kernel.CreateMutexW(None, True, r'Global\SideScreen.DriverSetup')
        assert lock
        try:
            run(driver_setup, '/ACCEPTDEVELOPMENTCERT=1', success=False)
            run(installer, '/SERVER=1', '/ACCEPTDEVELOPMENTCERT=1', f'/DIR={target}', '/TASKS=', success=False)
            assert (target/'ExtraPane.exe').is_file()
            assert registered_uninstaller(target).is_file()
            assert ps(trust_count).stdout.strip() == '0'
        finally:
            kernel.ReleaseMutex(lock)
            kernel.CloseHandle(lock)
        run(installer, '/SERVER=1', '/ACCEPTDEVELOPMENTCERT=1', f'/DIR={target}', '/TASKS=')
        assert (target/'ExtraPane.exe').is_file()
        assert not driver_entry_exists(), 'Duplicate display-driver uninstall registration remains'
        assert (folder/'manage-driver.ps1').is_file()
        records = list((folder/'state').glob('*.json')); assert len(records) == 1
        record = json.loads(records[0].read_text(encoding='utf-8-sig'))
        assert record['ownedDriver'] and record['addedRoot'] and record['addedTrustedPublisher'] and record['status'] == 'installed'
        assert ps(trust_count).stdout.strip() == '2'
        # Same-package repair/upgrade is idempotent and preserves ownership.
        run(installer, '/SERVER=1', '/ACCEPTDEVELOPMENTCERT=1', f'/DIR={target}', '/TASKS=')
        assert len(list((folder/'state').glob('*.json'))) == 1
        assert json.loads(records[0].read_text(encoding='utf-8-sig'))['publishedInf'] == record['publishedInf']
        # Payload tampering is rejected before any store/trust changes.
        dll = folder/'package/DMIndirectDisplay.dll'; original = dll.read_bytes()
        try:
            dll.write_bytes(original+b'tamper')
            result = ps(f"& {quote(folder/'manage-driver.ps1')} -Action Install; exit $LASTEXITCODE", check=False)
            assert result.returncode != 0 and 'hash mismatch' in result.stdout
            assert ps(trust_count).stdout.strip() == '2'
        finally:
            dll.write_bytes(original)
        app_uninstaller = registered_uninstaller(target)
        # Failed removal must preserve the manager, uninstaller and records.
        backup_dll = folder/'state'/record['catHash']/'DMIndirectDisplay.dll'
        backup_original = backup_dll.read_bytes()
        try:
            backup_dll.write_bytes(backup_original+b'tamper')
            run(app_uninstaller, success=False)
            assert app_uninstaller.is_file() and (folder/'manage-driver.ps1').is_file() and records[0].exists()
            assert ps(trust_count).stdout.strip() == '2'
        finally:
            backup_dll.write_bytes(backup_original)
        run(app_uninstaller)
        wait_absent(target/'ExtraPane.exe')
        wait_absent(folder/'manage-driver.ps1')
        assert ps(trust_count).stdout.strip() == '0'
        assert ps("@(Get-WindowsDriver -Online -All | Where-Object {$_.ProviderName -eq 'DM Project'}).Count").stdout.strip() == '0'
    # An independently staged copy and certificate are not owned by setup.
    for store in ('Root','TrustedPublisher'):
        ps(f"Import-Certificate -FilePath {quote(cert)} -CertStoreLocation 'Cert:\\LocalMachine\\{store}' | Out-Null")
    oem = None
    try:
        ps(f"& pnputil.exe /add-driver {quote(root/'out/driver-package/DMIndirectDisplay.inf')}; if($LASTEXITCODE){{throw 'Independent stage failed'}}")
        oem = ps("@(Get-WindowsDriver -Online -All | Where-Object {$_.ProviderName -eq 'DM Project'})[0].Driver").stdout.strip()
        assert oem.startswith('oem') and oem.endswith('.inf')
        run(driver_setup, '/ACCEPTDEVELOPMENTCERT=1')
        record = json.loads(next((folder/'state').glob('*.json')).read_text(encoding='utf-8-sig'))
        assert not record['ownedDriver'] and not record['addedRoot'] and not record['addedTrustedPublisher']
        run(driver_uninstaller());wait_absent(folder/'manage-driver.ps1')
        assert ps(trust_count).stdout.strip() == '2'
        assert ps("@(Get-WindowsDriver -Online -All | Where-Object {$_.ProviderName -eq 'DM Project'}).Count").stdout.strip() == '1'
    finally:
        if oem:
            ps(f"& pnputil.exe /delete-driver {quote(oem)} /uninstall; if($LASTEXITCODE){{throw 'Fixture cleanup failed'}}")
        for store in ('Root','TrustedPublisher'):
            ps(f"Remove-Item -LiteralPath 'Cert:\\LocalMachine\\{store}\\{thumb}' -ErrorAction SilentlyContinue")
    print('DRIVER_INSTALLER passed: consent, required host dependency, staging, idempotency, tamper rejection, unified uninstall, exact ownership preservation. Runtime loading is a separate hardware check.')

if __name__ == '__main__':
    sys.stdout.reconfigure(errors='backslashreplace')
    try:
        main()
    finally:
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            log = Path(os.environ['ProgramFiles'])/'ExtraPane/Components/Display/driver-setup.log'
            if log.exists():
                print(log.read_text(encoding='utf-8-sig', errors='replace')[-12000:], flush=True)
