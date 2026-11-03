"""Publish verified release artifacts with names passed as arguments, never shell code."""
import json
import hashlib
import os
from pathlib import Path
import subprocess

data = json.loads(Path('dist/release-metadata.json').read_text())
notes = ''
if not data['prerelease']:
    notes = data['explanation'] + '\n\n'
notes += '''## Downloads
One ExtraPane app works on either PC. Choose **Server** on the PC being extended and **Client** on the second screen.
On Windows, choose the portable ZIP or per-user Setup.exe installer. Both include Server, Client and About. Extract the entire portable folder.

**Linux x86_64:** download the AppImage, mark it executable and run it. This build targets current Arch-based systems; compatibility with older distributions is not guaranteed. Normal launch requires FUSE 2, or use `--appimage-extract-and-run`. Linux receiving uses OpenGL/H.264. Hosting requires a compatible Wayland desktop portal (KDE tested), supports one client, and uses software encoding with portal-controlled monitor dimensions. Physical Linux touch/pen remains unverified. The Linux dependency archive contains build versions and third-party notices. See docs/LINUX.md for setup and limitations.

**Server installation:** select **Server + Client** in Setup. The bundled virtual-display driver is required for this choice and asks for certificate consent and administrator permission. Portable users run `ExtraPane.DriverSetup.exe` once before hosting. **Client only** needs no driver.

The driver is development signed, not Microsoft release signed. Its installer adds the package's public code-signing certificate to machine Root and TrustedPublisher. Windows policy can still prevent loading. Setup does not change Secure Boot, Memory Integrity or test-signing mode. The driver has its own Windows Apps uninstall entry; uninstalling the app preserves the driver for other users and portable copies.

**macOS client preview:** the Universal ZIP and DMG contain both Apple Silicon and Intel code, targeting macOS 12+. Drag ExtraPane.app to Applications. This preview is ad-hoc signed, not Developer ID signed or notarized; Gatekeeper may require explicit approval in Privacy & Security. Mac hosting, native touch/pen and audio are not included. See docs/MACOS.md for verification status and controls.

**Android 11+:** install the Android APK on an ARM64 or x86_64 device and allow installation from your download app when Android asks. Android is a receiver only. It supports native touch, mouse, physical keyboard and compatible stylus input, subject to host/device capabilities. The APK uses the project's persistent Android signing key for updates. An old debug-key build must be uninstalled before installing the release-key APK. See docs/ANDROID.md.

Windows apps and installers are currently unsigned development builds. Audio, microphone passthrough and Windows+K are not implemented. Use SHA256SUMS.txt to verify downloads.
'''
Path('release-notes.md').write_text(notes)
version = data['version']
artifacts = [Path('dist') / f'ExtraPane-{version}-win-x64.zip',
             Path('dist') / f'ExtraPane-{version}-Setup-win-x64.exe',
             Path('dist') / f'ExtraPane-{version}-linux-x86_64.AppImage',
             Path('dist') / f'ExtraPane-{version}-linux-dependencies.tar.gz',
             Path('dist') / f'ExtraPane-{version}-android.apk',
             Path('dist') / f'ExtraPane-{version}-macos-universal.zip',
             Path('dist') / f'ExtraPane-{version}-macos-universal.dmg']
assert all(path.is_file() for path in artifacts), 'Incomplete release artifacts'
checksum = Path('dist/SHA256SUMS.txt')
existing = dict(line.strip().split('  ', 1)[::-1] for line in checksum.read_text().splitlines() if line.strip())
for path in artifacts:
    with path.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if path.name in existing and existing[path.name] != actual:
        raise RuntimeError(f'Artifact checksum mismatch: {path.name}')
    if path.suffix in ('.AppImage', '.apk', '.dmg') or path.name.endswith('-macos-universal.zip'):
        recorded = path.with_suffix(path.suffix+'.sha256').read_text().split()[0]
        if recorded != actual: raise RuntimeError('Platform package checksum mismatch')
    existing[path.name] = actual
checksum.write_text(''.join(f'{value}  {name}\n' for name,value in sorted(existing.items())))
artifacts.append(checksum)
command = ['gh', 'release', 'create', data['tag'], *map(str, artifacts),
           '--target', os.environ['GITHUB_SHA'], '--title', data['title'],
           '--generate-notes', '--notes-file', 'release-notes.md']
command += ['--prerelease', '--latest=false'] if data['prerelease'] else ['--latest']
subprocess.run(command, check=True)
