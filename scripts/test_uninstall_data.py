"""Exercise uninstall data choices on an ephemeral CI Windows account only."""
import ctypes
from ctypes import wintypes as w
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import winreg

class Credential(ctypes.Structure):
    _fields_ = [('Flags', w.DWORD), ('Type', w.DWORD), ('TargetName', w.LPWSTR),
                ('Comment', w.LPWSTR), ('LastWritten', w.FILETIME),
                ('CredentialBlobSize', w.DWORD), ('CredentialBlob', ctypes.POINTER(w.BYTE)),
                ('Persist', w.DWORD), ('AttributeCount', w.DWORD), ('Attributes', ctypes.c_void_p),
                ('TargetAlias', w.LPWSTR), ('UserName', w.LPWSTR)]

api = ctypes.WinDLL('advapi32', use_last_error=True)
api.CredWriteW.argtypes = [ctypes.POINTER(Credential), w.DWORD]
api.CredReadW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.POINTER(ctypes.c_void_p)]
api.CredDeleteW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD]
api.CredFree.argtypes = [ctypes.c_void_p]
PAIR = 'DM/peers/dm-lan'
OTHER = 'SideScreen-uninstall-test-unrelated'
KEYS = ['Software\\DM\\' + role for role in ('Host', 'Receiver', 'Display')]
UNINSTALL = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\SideScreen_is1'

def registered_uninstaller(target):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL) as key:
        assert winreg.QueryValueEx(key, 'DisplayName')[0].startswith('SideScreen')
        location = winreg.QueryValueEx(key, 'InstallLocation')[0]
        assert Path(location).resolve() == target.resolve()
        command = winreg.QueryValueEx(key, 'UninstallString')[0]
    match = re.match(r'^"([^"]+)"|^(\S+)', command)
    assert match, 'Missing registered uninstaller'
    exe = Path(match.group(1) or match.group(2))
    assert exe.is_file(), f'Registered uninstaller missing: {exe}'
    return exe

def exists(key):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key): return True
    except FileNotFoundError: return False

def paired(name):
    result = ctypes.c_void_p()
    if api.CredReadW(name, 1, 0, ctypes.byref(result)):
        api.CredFree(result)
        return True
    assert ctypes.get_last_error() == 1168
    return False

def save(name):
    blob = (w.BYTE * 32)(*range(32))
    value = Credential(Type=1, TargetName=name, CredentialBlobSize=32,
                       CredentialBlob=blob, Persist=2, UserName='Installer CI fixture')
    assert api.CredWriteW(ctypes.byref(value), 0), ctypes.get_last_error()

def verify(installer):
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        print('Data-reset integration tests run only on ephemeral GitHub runners.')
        return
    # Refuse to overwrite any existing account data, even on CI.
    assert not any(exists(k) for k in KEYS + [UNINSTALL])
    assert not paired(PAIR) and not paired(OTHER)
    try:
        for key in KEYS:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key) as handle:
                winreg.SetValueEx(handle, 'InstallerFixture', 0, winreg.REG_SZ, 'preserve-me')
        save(PAIR)
        save(OTHER)
        with tempfile.TemporaryDirectory(prefix='SideScreen-data-check-') as folder:
            target = Path(folder) / 'app'
            for reset in (False, True, True):
                fixtures = []
                try:
                    markers = [str(Path(folder) / f'instance-{i}') for i in range(2)]
                    for marker in markers:
                        for suffix in ('.ready', '.stopped'):
                            Path(marker + suffix).unlink(missing_ok=True)
                        fixtures.append(subprocess.Popen([sys.executable, str(Path(__file__).with_name('installer_shutdown_fixture.py')), marker]))
                    for _ in range(100):
                        if all(Path(m + '.ready').exists() for m in markers): break
                        assert all(p.poll() is None for p in fixtures), 'Shutdown fixture failed'
                        time.sleep(.1)
                    assert all(Path(m + '.ready').exists() for m in markers)
                    subprocess.run([str(installer), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                                    f'/DIR={target}', '/GROUP=SideScreen Data Validation', '/TASKS='], check=True, timeout=90)
                    assert all(Path(m + '.stopped').exists() for m in markers), 'Installer did not wait for every app/helper'
                    assert all(p.wait(timeout=5) == 0 for p in fixtures)
                finally:
                    for process in fixtures:
                        if process.poll() is None: process.kill()
                        process.wait()
                args = [str(registered_uninstaller(target)), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART']
                if reset: args.append('/REMOVEUSERDATA=1')
                subprocess.run(args, check=True, timeout=90)
                for _ in range(100):
                    if not exists(UNINSTALL): break
                    time.sleep(.1)
                assert not exists(UNINSTALL), 'Windows uninstall registration remains'
                assert paired(PAIR) == (not reset)
                assert all(exists(key) == (not reset) for key in KEYS)
                if not reset:
                    for name in KEYS:
                        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, name) as key:
                            assert winreg.QueryValueEx(key, 'InstallerFixture')[0] == 'preserve-me'
                assert paired(OTHER), 'Unrelated credential was removed'
    finally:
        for name in (PAIR, OTHER): api.CredDeleteW(name, 1, 0)
        for name in KEYS:
            try: winreg.DeleteKey(winreg.HKEY_CURRENT_USER, name)
            except FileNotFoundError: pass
    print('Installer passed: all instances/helper cleanup; uninstall registration, preserve, reset, missing-data reset, unrelated credential survives')
