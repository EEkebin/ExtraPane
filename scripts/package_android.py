"""Align, sign and verify an Android release; secrets are passed through environment."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
version = os.environ['EXTRAPANE_VERSION']
if not re.fullmatch(r'\d{4}\.\d{2}\.\d{2}(?:[.-][A-Za-z0-9]+)*', version):
    raise ValueError('Invalid release version')
sdk = Path(os.environ['ANDROID_HOME']) / 'build-tools/35.0.0'
suffix = '.exe' if os.name == 'nt' else ''
signer = sdk / ('apksigner.bat' if os.name == 'nt' else 'apksigner')
dist = root / 'out/dist-android'
dist.mkdir(parents=True, exist_ok=True)
apk = dist / f'ExtraPane-{version}-android.apk'
unsigned = root / 'apps/android/app/build/outputs/apk/release/app-release-unsigned.apk'
subprocess.run([str(sdk / ('zipalign'+suffix)), '-f', '-P', '16', '4', str(unsigned), str(apk)], check=True)
subprocess.run([str(signer), 'sign', '--ks', os.environ['ANDROID_KEYSTORE'], '--ks-key-alias', 'extrapane',
                '--ks-pass', 'env:ANDROID_KEYSTORE_PASSWORD', '--key-pass', 'env:ANDROID_KEYSTORE_PASSWORD', str(apk)], check=True)
verified = subprocess.check_output([str(signer), 'verify', '--verbose', '--print-certs', str(apk)], text=True)
fingerprint = (root / 'packaging/android-signing-certificate.sha256').read_text().strip().lower()
if f'certificate SHA-256 digest: {fingerprint}' not in verified:
    raise RuntimeError('Unexpected Android signing certificate')
subprocess.run([str(sdk / ('zipalign'+suffix)), '-c', '-P', '16', '4', str(apk)], check=True)
badging = subprocess.check_output([str(sdk / ('aapt'+suffix)), 'dump', 'badging', str(apk)], text=True)
for expected in ["name='io.github.eekebin.extrapane'", f"versionName='{version}'", f"versionCode='{os.environ['EXTRAPANE_ANDROID_VERSION_CODE']}'", "sdkVersion:'30'"]:
    if expected not in badging: raise RuntimeError('Incorrect APK metadata: '+expected)
if 'application-debuggable' in badging: raise RuntimeError('Release is debuggable')
with zipfile.ZipFile(apk) as archive:
    for abi in ['arm64-v8a', 'x86_64']:
        if not any(n.startswith('lib/'+abi+'/') for n in archive.namelist()): raise RuntimeError('Missing ABI: '+abi)
apk.with_suffix('.apk.sha256').write_text(hashlib.sha256(apk.read_bytes()).hexdigest()+'  '+apk.name+'\n')
print('Verified signed Android package: '+apk.name)
