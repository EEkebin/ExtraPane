"""Build distributable self-contained folders as ZIPs; no credentials or local settings."""
from pathlib import Path
import zipfile,hashlib,json,os,re
ROOT=Path(__file__).resolve().parent.parent
VERSION=os.environ.get('SIDESCREEN_VERSION','0.13.0')
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?',VERSION):raise ValueError('Invalid release version')
out=ROOT/'out/dist';out.mkdir(parents=True,exist_ok=True)
reports=[]
for role in ['SideScreen']:
 source=ROOT/'out/winui'/role
 if not (source/(role+'.exe')).exists():raise RuntimeError('Run scripts/build-apps.ps1 first')
 archive=out/f'SideScreen-{VERSION}-win-x64.zip'
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  files=[]
  for file in sorted(source.rglob('*')):
   if not file.is_file() or file.suffix.lower() in ['.pdb','.ilk','.bmp','.log','.exp','.lib']:continue
   if file.name in ['DM.exe','DM.pri','DM.winmd']:continue # Prior build names, not part of this release.
   if file.suffix.lower() in ['.key','.pem','.pfx','.p12']:raise RuntimeError('Private material in build output')
   relative=file.relative_to(source).as_posix();z.write(file,role+'/'+relative)
   files.append({'path':relative,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
  assets=json.loads((ROOT/'out/winui-obj'/role/'project.assets.json').read_text())
  for library,metadata in sorted(assets['libraries'].items()):
   if metadata['type']!='package':continue
   package=next((Path(folder)/metadata['path'] for folder in assets['packageFolders'] if (Path(folder)/metadata['path']).exists()),None)
   if package is None:raise RuntimeError('Missing restored package '+library)
   copied=set()
   for name in ['license.txt','LICENSE','LICENSE.txt','NOTICE.txt','ThirdPartyNotices.txt']:
    file=package/name
    if file.is_file() and str(file).lower() not in copied:
     z.write(file,role+'/Licenses/'+library.replace('/','-')+'/'+name);copied.add(str(file).lower())
  for dependency,relative in [('mbedtls','mbedtls-src'),('tf-psa-crypto','mbedtls-src/tf-psa-crypto')]:
   file=ROOT/'out/build/windows-release/_deps'/relative/'LICENSE'
   if not file.exists():raise RuntimeError('Missing TLS dependency license: '+str(file))
   z.write(file,role+'/Licenses/'+dependency+'/LICENSE')
  readme=f'''SideScreen {VERSION} - Windows 11 x64 development prerelease

Extract the entire folder and run SideScreen.exe on either PC. No Python, SSH or
separate WinUI runtime installation is required. The sidebar has Server, Client
and About pages. Switching pages does not interrupt either role.

On the desktop PC, open Server and choose Start Driver. Administrator approval
is requested only for the separate driver helper. On the second PC, open Client,
enter the server IPv4 address and Connect. Pair the PCs once using the same code.
Pairings stay in Windows Credential Manager. The app normally runs unelevated.

The Server requires the separately installed DM virtual-display driver. This
installer/ZIP does not install a driver or change signing/security settings.
The current driver is a development test-signed component. Receiving needs no driver.

Start Driver waits for a paired client before creating its display. The client
supplies resolution/refresh preferences (currently capped at 60 fps). Windows
layout changes are retained. Disconnecting the client leaves the server listening
with its monitor intact. Stop Driver or tray Exit removes it.

Close the setup window to hide it to the tray. The tray has Server and Client
controls and Exit. Ctrl+Shift+Q disconnects reception; F11 toggles fullscreen,
F10 reveals the toolbar, F8 pauses input. Reveal the toolbar by hovering briefly
at the top center or swiping down from the small top-center strip.

Normal sessions have no time limit. Debug is off by default. Audio, microphone,
Windows+K and automatic client reconnect are not implemented. Update both PCs
together. Apps/installers are unsigned development builds; protocol version 7.
'''

  z.writestr(role+'/README.txt',readme)
  z.writestr(role+'/manifest.json',json.dumps({'version':VERSION,'role':role,'files':files},indent=2))
 reports.append({'archive':archive.name,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()})
(out/'SHA256SUMS.txt').write_text(''.join(f"{r['sha256']}  {r['archive']}\n" for r in reports))
print(json.dumps(reports,indent=2))
