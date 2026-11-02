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

**Server prerequisite:** separately installed SideScreen/DM virtual-display driver. The app installer does not install a driver or change driver-signing settings. The current driver is a development test-signed component. Client needs no display driver.

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
