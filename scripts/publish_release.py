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
Choose **Server** on the computer receiving resources and **Client** on the device supplying them. A display connection extends the server's desktop onto the client. The client can independently share camera and microphone sources where supported. See the platform guides for current capabilities and validation limits.
On Windows, choose the portable ZIP or Setup.exe installer. Extract the entire portable folder. Setup requires administrator permission and installs all managed components for offering and using resources.

**Linux x86_64:** download the AppImage, mark it executable and run it. This build targets current Arch-based systems; compatibility with older distributions is not guaranteed. Normal launch requires FUSE 2, or use `--appimage-extract-and-run`. Display receiving uses OpenGL/H.264. Display hosting requires a compatible Wayland desktop portal (KDE tested), with software encoding and portal-controlled monitor dimensions. Camera and microphone receiving have separate system prerequisites; the AppImage does not install those endpoints. USB remains unavailable. Physical Linux touch/pen and combined-resource delivery remain unverified. The Linux dependency archive contains build versions and third-party notices. See [Linux setup](https://github.com/EEkebin/ExtraPane/blob/main/docs/LINUX.md) and [resource prerequisites](https://github.com/EEkebin/ExtraPane/blob/main/docs/LINUX-RESOURCES.md).

**Windows installation:** Setup prepares the bundled virtual-display driver, camera component, and audio/USB components. The display driver asks for explicit certificate consent. Portable hosting still requires these installed components; use Setup to prepare them.

The display driver is development signed, not Microsoft release signed. With consent, Setup adds the package's public code-signing certificate to machine Root and TrustedPublisher. Windows policy can still prevent loading. Setup does not change Secure Boot, Memory Integrity or test-signing mode. There is one **ExtraPane** Windows Apps uninstall entry for the app and its managed components/settings. Independently installed shared drivers, pre-existing trust and uncertain ownership are preserved; active USB ownership may require safe cleanup before removal.

**macOS preview:** the Universal ZIP and DMG contain both Apple Silicon and Intel code, targeting macOS 12+. Drag ExtraPane.app to Applications. This preview is ad-hoc signed, not Developer ID signed or notarized; Gatekeeper may require explicit approval in Privacy & Security. Display hosting requires macOS 13+, Screen Recording permission and Accessibility for remote input. Its virtual monitors depend on an undocumented macOS interface. Camera/microphone sending uses native permissions; virtual camera/microphone receiving, USB, native touch/pen and system-audio playback remain unavailable. See [macOS setup](https://github.com/EEkebin/ExtraPane/blob/main/docs/MACOS.md) for verification status and controls.

**Android 11+:** install the Android APK on an ARM64 or x86_64 device and allow installation from your download app when Android asks. Android receives a display and can send camera/microphone sources with permission; hosting and USB remain unavailable. It supports native touch, mouse, physical keyboard and compatible stylus input, subject to host/device capabilities. The APK uses the project's persistent Android signing key for updates. An old debug-key build must be uninstalled before installing the release-key APK. See [Android setup](https://github.com/EEkebin/ExtraPane/blob/main/docs/ANDROID.md).

Windows apps and installers are currently unsigned development builds. System-audio playback and Windows+K are not implemented. Resource availability depends on each platform's installed components, permissions and hardware; successful package checks do not establish physical-device delivery. Use SHA256SUMS.txt to verify downloads.
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
