"""Real driver-store/trust lifecycle checks, exclusively on disposable GitHub runners.

Never run this against either development PC. Does not create a virtual monitor,
change boot/security policy, or claim that staging proves runtime loading.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import uuid
import winreg

from release_version import current
from test_uninstall_data import registered_uninstaller

DRIVER_KEY = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen.VirtualDisplayDriver_is1'

def ps(script, check=True):
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command',
                           "$ErrorActionPreference='Stop'; " + script],
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
        print(log.read_text(encoding='utf-8', errors='replace')[-12000:], flush=True)
    assert (result.returncode == 0) == success, (exe, result.returncode, args)

def driver_uninstaller():
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, DRIVER_KEY, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
        command = winreg.QueryValueEx(key, 'UninstallString')[0]
    assert command.startswith('"')
    return Path(command.split('"')[1])

def wait_absent(path):
    for _ in range(100):
        if not path.exists(): return
        time.sleep(.1)
    raise AssertionError(f'Uninstaller left {path}')

def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit('Driver integration checks are restricted to disposable GitHub runners.')
    root = Path(__file__).resolve().parent.parent
    folder = Path(os.environ['ProgramFiles']) / 'SideScreen Driver'
    assert not folder.exists(), 'Refusing to overwrite an existing driver installation'
    baseline = ps("@(Get-WindowsDriver -Online -All | Where-Object {$_.ProviderName -eq 'DM Project'}).Count").stdout.strip()
    assert baseline == '0', 'Refusing to touch pre-existing DM driver packages'
    installer = root / 'out/dist' / f'SideScreen-{current()}-Setup-win-x64.exe'
    driver_setup = root / 'out/driver-installer/SideScreen.DriverSetup.exe'
    manifest = json.loads((root/'out/driver-package/manifest.json').read_text(encoding='utf-8-sig'))
    thumb = manifest['certificateThumbprint']
    cert = root/'out/driver-package/SideScreen-Driver.cer'
    trust_count = f"@('Root','TrustedPublisher' | Where-Object {{Test-Path ('Cert:\\LocalMachine\\'+$_+'\\{thumb}')}}).Count"
    assert ps(trust_count).stdout.strip() == '0'
    # No implicit trust consent, including silent installs.
    run(driver_setup, success=False)
    assert not folder.exists()
    with tempfile.TemporaryDirectory(prefix='SideScreen-host-install-') as temp:
        target = Path(temp)/'app'
        run(installer, '/SERVER=1', f'/DIR={target}', '/TASKS=', success=False)
        assert not folder.exists() and not (target/'SideScreen.exe').exists()
        run(installer, '/SERVER=1', '/ACCEPTDEVELOPMENTCERT=1', f'/DIR={target}', '/TASKS=')
        assert (target/'SideScreen.exe').is_file()
        assert driver_uninstaller().is_file()
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
        run(app_uninstaller)
        wait_absent(target/'SideScreen.exe')
        assert driver_uninstaller().is_file() and ps(trust_count).stdout.strip() == '2'
        # Failed removal must preserve the manager, uninstaller and records.
        backup_dll = folder/'state'/record['catHash']/'DMIndirectDisplay.dll'
        backup_original = backup_dll.read_bytes()
        try:
            backup_dll.write_bytes(backup_original+b'tamper')
            run(driver_uninstaller(), success=False)
            assert driver_uninstaller().is_file() and records[0].exists()
            assert ps(trust_count).stdout.strip() == '2'
        finally:
            backup_dll.write_bytes(backup_original)
        run(driver_uninstaller())
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
    print('DRIVER_INSTALLER passed: consent, required host dependency, staging, idempotency, tamper rejection, separate uninstall, exact ownership preservation. Runtime loading is a separate hardware check.')

if __name__ == '__main__':
    main()
