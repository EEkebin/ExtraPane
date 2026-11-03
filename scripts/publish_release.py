"""Publish verified release artifacts with names passed as arguments, never shell code."""
import json
import os
from pathlib import Path
import subprocess

data = json.loads(Path('dist/release-metadata.json').read_text())
notes = ''
if not data['prerelease']:
    notes = data['explanation'] + '\n\n'
notes += '''## Downloads
One SideScreen app works on either PC. Choose **Server** on the PC being extended and **Client** on the second screen.
Choose the portable ZIP or per-user Setup.exe installer. Both include Server, Client and About. Extract the entire portable folder.

**Server installation:** select **Server + Client** in Setup. The bundled virtual-display driver is required for this choice and asks for certificate consent and administrator permission. Portable users run `SideScreen.DriverSetup.exe` once before hosting. **Client only** needs no driver.

The driver is development signed, not Microsoft release signed. Its installer adds the package's public code-signing certificate to machine Root and TrustedPublisher. Windows policy can still prevent loading. Setup does not change Secure Boot, Memory Integrity or test-signing mode. The driver has its own Windows Apps uninstall entry; uninstalling the app preserves the driver for other users and portable copies.

Apps and installers are currently unsigned development builds. Audio, microphone passthrough and Windows+K are not implemented. Use SHA256SUMS.txt to verify downloads.
'''
Path('release-notes.md').write_text(notes)
version = data['version']
artifacts = [Path('dist') / f'SideScreen-{version}-win-x64.zip',
             Path('dist') / f'SideScreen-{version}-Setup-win-x64.exe', Path('dist/SHA256SUMS.txt')]
assert all(path.is_file() for path in artifacts), 'Incomplete release artifacts'
command = ['gh', 'release', 'create', data['tag'], *map(str, artifacts),
           '--target', os.environ['GITHUB_SHA'], '--title', data['title'],
           '--generate-notes', '--notes-file', 'release-notes.md']
command += ['--prerelease', '--latest=false'] if data['prerelease'] else ['--latest']
subprocess.run(command, check=True)
