"""Fetch verified packaging tools and record the Arch build's dependency notices."""
import json
from pathlib import Path
import shutil
import subprocess
from verified_download import fetch_verified

ROOT = Path(__file__).resolve().parent.parent

def main():
    lock = json.loads((ROOT/'packaging/linux/tools.lock.json').read_text())
    target = ROOT/'out/linux-tools'
    target.mkdir(parents=True, exist_ok=True)
    for name, entry in [*lock['tools'].items(), ('runtime-x86_64', lock['runtime'])]:
        path = target/name
        fetch_verified([entry['url'], *([entry['fallback_url']] if 'fallback_url' in entry else [])], path, entry['sha256'])
        path.chmod(0o755)
        entry['path'] = str(path.resolve())
    (target/'tools.lock.json').write_text(json.dumps(lock, indent=2)+'\n')
    notices = ROOT/'out/linux-notices'
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT/'out/build/linux-release/_deps/boringssl-src/LICENSE', notices/'BoringSSL-LICENSE')
    shutil.copytree('/usr/share/licenses', notices/'licenses', dirs_exist_ok=True)
    packages = subprocess.check_output(['pacman', '-Q'], text=True)
    (notices/'build-packages.txt').write_text(packages)
    details = subprocess.check_output(['pacman', '-Qi'], text=True)
    (notices/'package-details.txt').write_text(details)
    (notices/'SOURCES.txt').write_text(
        'Build environment: Arch Linux archive snapshot 2026-11-03.\n'
        'Exact installed package versions and upstream URLs are recorded alongside this file.\n'
        'Arch packaging recipes and source references: https://gitlab.archlinux.org/archlinux/packaging/packages\n'
        'Archived package binaries: https://archive.archlinux.org/packages/\n'
        'ExtraPane source and build scripts: https://github.com/EEkebin/ExtraPane\n'
        'This inventory includes build-only packages as well as runtime dependencies.\n'
        'System GPU drivers and the desktop PipeWire runtime are not bundled.\n')

if __name__ == '__main__':
    main()
