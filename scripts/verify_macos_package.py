"""Check the distributable Universal app without launching a host or creating monitors."""
import hashlib
import os
import plistlib
from pathlib import Path
import subprocess
import tempfile
from macos_package_checks import verify_disk_image

root = Path(__file__).resolve().parents[1]
version = os.environ['EXTRAPANE_VERSION']
dist = root / 'out/dist-macos'
stem = f'ExtraPane-{version}-macos-universal'
for extension in ('.zip', '.dmg'):
    artifact = dist / (stem + extension)
    expected = artifact.with_suffix(extension + '.sha256').read_text().split()[0]
    if hashlib.sha256(artifact.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f'Checksum mismatch: {artifact.name}')
verify_disk_image(dist / (stem + '.dmg'))
with tempfile.TemporaryDirectory(prefix='ExtraPane-package-') as temporary:
    subprocess.run(['ditto', '-x', '-k', str(dist / (stem + '.zip')), temporary], check=True)
    app = Path(temporary) / 'ExtraPane.app'
    binary = app / 'Contents/MacOS/ExtraPane'
    with (app / 'Contents/Info.plist').open('rb') as source:
        info = plistlib.load(source)
    for key in ('NSCameraUsageDescription', 'NSMicrophoneUsageDescription', 'NSLocalNetworkUsageDescription'):
        if not isinstance(info.get(key), str) or not info[key].strip():
            raise RuntimeError(f'Missing privacy description: {key}')
    if not (app / 'Contents/Resources/Documentation/MACOS.md').is_file():
        raise RuntimeError('Missing macOS setup guide')
    subprocess.run(['lipo', str(binary), '-verify_arch', 'arm64', 'x86_64'], check=True)
    subprocess.run(['codesign', '--verify', '--strict', '--verbose=2', str(app)], check=True)
    for name in ('boringssl', 'mbedtls', 'tf-psa-crypto'):
        if not (app / 'Contents/Resources/Licenses' / name / 'LICENSE').read_text().strip():
            raise RuntimeError(f'Missing dependency notice: {name}')
    dependencies = subprocess.check_output(['otool', '-L', str(binary)], text=True)
    for line in dependencies.splitlines():
        if not line.startswith(('\t', ' ')):
            continue
        dependency = line.strip().split(' (', 1)[0]
        if not dependency.startswith(('/System/Library/', '/usr/lib/')):
            raise RuntimeError(f'Non-system runtime dependency: {dependency}')
    subprocess.run([str(binary), '--self-test'], check=True, timeout=60, cwd=temporary)
    subprocess.run([str(binary), '--ui-smoke'], check=True, timeout=20, cwd=temporary)
print('ZIP extraction, Universal architectures, signature, TLS notices, runtime dependencies, app startup and DMG integrity passed.')
