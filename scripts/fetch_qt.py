"""Restore a pinned Qt SDK from download.qt.io without the Qt installer.

aqtinstall 3.3.0 cannot read the per-target repository layout Qt uses from 6.11
(for example .../qt6_6113/qt6_6113_msvc2022_64/). This reads Updates.xml for the
requested packages, downloads only their archives, verifies each archive against
the repository .sha1 file and against tools/qt-<version>.lock.json (SHA-256,
created on first use and enforced afterwards), and extracts with 7-Zip.

  python scripts/fetch_qt.py --version 6.11.3 --target windows-msvc2022_64 --target android-arm64_v8a --target android-x86_64 -O out/qt-sdk-6.11

Nothing is installed system-wide.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BASE = 'https://download.qt.io/online/qtsdkrepository'
# target -> (repository subpath template, package-name suffix, install directory under <version>/)
TARGETS = {
    'windows-msvc2022_64': ('windows_x86/desktop/qt6_{v}/qt6_{v}_msvc2022_64', 'win64_msvc2022_64', 'msvc2022_64'),
    'android-arm64_v8a': ('all_os/android/qt6_{v}/qt6_{v}_arm64_v8a', 'android_arm64_v8a', 'android_arm64_v8a'),
    'android-x86_64': ('all_os/android/qt6_{v}/qt6_{v}_x86_64', 'android_x86_64', 'android_x86_64'),
}
ADDONS = ('qtshadertools',)
SKIP_ARCHIVES = ('qtdoc-', 'qttranslations-')


def fetch(url):
    with urllib.request.urlopen(url, timeout=300) as response:
        return response.read()


def seven_zip():
    for candidate in (shutil.which('7z'), shutil.which('7za'), r'C:\Program Files\7-Zip\7z.exe'):
        if candidate and Path(candidate).exists():
            return candidate
    raise SystemExit('7-Zip is required to extract Qt archives')


def packages(xml, names):
    found = {}
    for block in re.findall(r'<PackageUpdate>(.*?)</PackageUpdate>', xml, re.S):
        name = re.search(r'<Name>(.*?)</Name>', block).group(1)
        if name in names:
            version = re.search(r'<Version>(.*?)</Version>', block).group(1)
            archives = re.search(r'<DownloadableArchives>(.*?)</DownloadableArchives>', block, re.S)
            items = [a.strip() for a in (archives.group(1) if archives else '').split(',') if a.strip()]
            found[name] = (version, [a for a in items if not a.startswith(SKIP_ARCHIVES)])
    missing = set(names) - set(found)
    if missing:
        raise SystemExit('Qt packages not found: ' + ', '.join(sorted(missing)))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', required=True)
    parser.add_argument('--target', action='append', required=True, choices=sorted(TARGETS))
    parser.add_argument('-O', '--output', required=True)
    args = parser.parse_args()
    v = args.version.replace('.', '')
    output = (ROOT / args.output).resolve()
    if not str(output).startswith(str((ROOT / 'out').resolve())):
        raise SystemExit('Output must stay under out/')
    lock_path = ROOT / 'tools' / f'qt-{args.version}.lock.json'
    lock = json.loads(lock_path.read_text()) if lock_path.exists() else {}
    sevenzip = seven_zip()
    changed = False
    for target in args.target:
        subpath, arch, folder = TARGETS[target]
        destination = output / args.version / folder
        repo = f'{BASE}/{subpath.format(v=v)}'
        xml = fetch(f'{repo}/Updates.xml').decode()
        names = [f'qt.qt6.{v}.{arch}'] + [f'qt.qt6.{v}.addons.{a}.{arch}' for a in ADDONS]
        for name, (version, archives) in packages(xml, names).items():
            for archive in archives:
                key = f'{target}/{name}/{archive}'
                url = f'{repo}/{name}/{version}{archive}'
                cache = ROOT / 'out' / 'qt-archives' / args.version / target / archive
                data = cache.read_bytes() if cache.is_file() else fetch(url)
                expected_sha1 = fetch(url + '.sha1').decode().split()[0]
                if hashlib.sha1(data).hexdigest() != expected_sha1:
                    raise SystemExit(f'SHA-1 mismatch for {archive}')
                sha256 = hashlib.sha256(data).hexdigest()
                if key in lock and lock[key] != sha256:
                    raise SystemExit(f'{archive} differs from the pinned SHA-256 in {lock_path.name}')
                if key not in lock:
                    lock[key] = sha256
                    changed = True
                if not cache.is_file():
                    cache.parent.mkdir(parents=True, exist_ok=True)
                    cache.write_bytes(data)
                destination.mkdir(parents=True, exist_ok=True)
                subprocess.run([sevenzip, 'x', '-y', '-bso0', '-bsp0', f'-o{destination}', str(cache)], check=True)
                print(f'{target}: {archive}')
    if changed:
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        lock_path.write_text(json.dumps(dict(sorted(lock.items())), indent=2) + '\n')
        print(f'Pinned new archives in {lock_path.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
