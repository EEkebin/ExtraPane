"""Finalize and verify current shared-UI Windows packages without installing them."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

from release_version import current

root = Path(__file__).resolve().parents[1]
stage = root / 'out/shared-portable'
dist = root / 'out/dist'
version = current()
required = (
    'ExtraPane.exe', 'ExtraPane.Session.exe', 'ExtraPane.Service.exe', 'dm-view.exe',
    'ExtraPane.Resources.exe', 'ExtraPane.CameraSetup.exe', 'ExtraPane.Microphone.exe',
    'ExtraPane.MicrophoneEndpoint.exe', 'ExtraPane.Speakers.exe', 'ExtraPane.Usb.exe', 'ExtraPane.UsbSetup.exe',
    'ExtraPane.UsbHostSetup.exe', 'ExtraPane.UsbRecovery.exe',
    'Components/Display/manage-driver.ps1', 'Components/Display/package/manifest.json',
    'platforms/qwindows.dll', 'platforms/qoffscreen.dll', 'Qt6Core.dll', 'Qt6Quick.dll',
    'usb-host-source/usbipd-upstream.tar', 'usb-host-source/usbipd-loopback.patch',
    'USBIP-AUDIO-LICENSE.txt', 'USBIP-WIN2-LICENSE.txt', 'BUILD-INFO.json',
)
for name in required:
    if not (stage / name).is_file():
        raise RuntimeError(f'Missing package component: {name}')

notices = stage / 'Licenses'
for dependency, relative in (
    ('boringssl', 'boringssl-src/LICENSE'), ('mbedtls', 'mbedtls-src/LICENSE'),
    ('tf-psa-crypto', 'mbedtls-src/tf-psa-crypto/LICENSE'),
    ('vulkan-headers', 'vulkan_headers-src/LICENSE.md'),
    ('vulkan-headers/LICENSES', 'vulkan_headers-src/LICENSES/Apache-2.0.txt'),
    ('vulkan-headers/LICENSES', 'vulkan_headers-src/LICENSES/MIT.txt'),
):
    source = root / 'out/shared-engine/_deps' / relative
    if not source.is_file():
        raise RuntimeError(f'Missing dependency notice: {source}')
    target = notices / dependency
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target / source.name)
qt = root / 'out/qt-sdk/6.8.3/msvc2022_64'
qt_license = notices / 'Qt'
qt_license.mkdir(parents=True, exist_ok=True)
# Binary SDK archives omit the license directory; use exact upstream 6.8.3 texts.
for name, digest in {
    'LGPL-3.0-only.txt': 'da7eabb7bafdf7d3ae5e9f223aa5bdc1eece45ac569dc21b3b037520b4464768',
    'GPL-2.0-only.txt': '8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643',
    'GPL-3.0-only.txt': '8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903',
    'Qt-GPL-exception-1.0.txt': '40678d338ce53cd93f8b22b281a2ecbcaa3ee65ce60b25ffb0c462b0530846b2',
    'BSD-3-Clause.txt': '9f0490f18656c6f2435bd14f603ef0c96434d1825615363dce43abb42ed1dcce',
}.items():
    with urllib.request.urlopen('https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/' + name, timeout=60) as response:
        contents = response.read()
    if hashlib.sha256(contents).hexdigest() != digest:
        raise RuntimeError('Qt license checksum mismatch: ' + name)
    (qt_license / name).write_bytes(contents)
if not (qt / 'sbom').is_dir():
    raise RuntimeError('Qt SDK component/license inventory is missing')
shutil.copytree(qt / 'sbom', qt_license / 'sbom', dirs_exist_ok=True)
(notices / 'Qt' / 'SOURCE.txt').write_text(
    'Qt 6.8.3 dynamically linked libraries. Qt is independently licensed.\n'
    'Corresponding upstream sources: https://download.qt.io/archive/qt/6.8/6.8.3/single/\n'
    'Use the supplied Qt licenses and retain the ability to replace these shared libraries.\n'
)
(stage / 'version.txt').write_text(version + '\n')
(stage / 'README.txt').write_text(
    'ExtraPane shared-resource development build\n\n'
    'Run ExtraPane.exe from the complete extracted folder. Hosting requires the receiving components; use the installer to prepare them.\n'
    'The installer offers Share only or Full installation. Both include USB source support; Full installation also prepares display, camera and microphone/USB receiving components.\n'
    'Installation requires administrator approval. The display driver uses a development certificate, not Microsoft release signing; review the installer consent page.\n'
    'There is one ExtraPane uninstall entry. Shared independently installed drivers are preserved. Unresolved active ownership can require safe cleanup before removal.\n'
    'Camera and microphone use remains subject to Windows privacy settings. See TEST-GUIDE.md and DEVELOPMENT-STATUS.md for validation limits.\n'
)
shutil.copy2(root / 'assets/branding/ExtraPane.ico', stage / 'ExtraPane.ico')
for path in stage.rglob('*'):
    if path.is_file() and path.suffix.lower() in ('.pfx', '.p12', '.key', '.pem', '.jks', '.keystore', '.pdb', '.log'):
        raise RuntimeError(f'Private or non-distributable file in stage: {path.name}')
manifest = {'version': version, 'files': [
    {'path': p.relative_to(stage).as_posix(), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
    for p in sorted(stage.rglob('*')) if p.is_file() and p != stage / 'manifest.json'
]}
(stage / 'manifest.json').write_text(json.dumps(manifest, indent=2))
dist.mkdir(parents=True, exist_ok=True)
subprocess.run(['pwsh', '-NoProfile', '-File', str(root / 'scripts/package-shared-installer.ps1'),
                '-Stage', 'out/shared-portable', '-Destination', 'out/dist', '-Version', version],
               cwd=root, check=True, timeout=600)
archive = dist / f'ExtraPane-{version}-win-x64.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            output.write(path, 'ExtraPane/' + path.relative_to(stage).as_posix())
with tempfile.TemporaryDirectory(prefix='ExtraPane-shared-package-') as temporary:
    with zipfile.ZipFile(archive) as package:
        package.extractall(temporary)
    extracted = Path(temporary) / 'ExtraPane'
    for item in manifest['files']:
        if hashlib.sha256((extracted / item['path']).read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError('Extracted portable checksum mismatch: ' + item['path'])
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    # Do not allow the build SDK to conceal missing deployed Qt dependencies.
    env['PATH'] = os.pathsep.join(p for p in env.get('PATH', '').split(os.pathsep) if 'qt-sdk' not in p.lower())
    env.pop('QT_PLUGIN_PATH', None)
    env.pop('QML2_IMPORT_PATH', None)
    subprocess.run([str(extracted / 'ExtraPane.exe'), '--check-ui'], cwd=temporary,
                   env=env, check=True, timeout=30)
    for helper in ('ExtraPane.CameraSetup.exe', 'ExtraPane.UsbSetup.exe', 'ExtraPane.UsbHostSetup.exe'):
        subprocess.run([str(extracted / helper), '--check-package'], cwd=temporary,
                       check=True, timeout=30)
installer = dist / f'ExtraPane-{version}-Setup-win-x64.exe'
if not installer.is_file() or installer.stat().st_size < 1_000_000:
    raise RuntimeError('Shared installer missing or unexpectedly small')
(dist / 'SHA256SUMS.txt').write_text(''.join(
    hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n'
    for p in (archive, installer)
))
print('Current shared UI, native helpers, notices, portable extraction and checksums verified; no installer was run.')
