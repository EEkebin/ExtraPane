"""Build distributable self-contained folders as ZIPs; no credentials or local settings."""
from pathlib import Path
import zipfile,hashlib,json,os,re
ROOT=Path(__file__).resolve().parent.parent
from release_version import current
VERSION=current()
out=ROOT/'out/dist';out.mkdir(parents=True,exist_ok=True)
reports=[]
for role in ['SideScreen']:
 source=ROOT/'out/winui'/role
 if not (source/(role+'.exe')).exists():raise RuntimeError('Run scripts/build-apps.ps1 first')
 if (source/'version.txt').read_text().strip()!=VERSION:raise RuntimeError('Rebuild apps with the requested release version')
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
  headers=ROOT/'out/build/windows-release/_deps/vulkan_headers-src'
  for relative in ['LICENSE.md','LICENSES/Apache-2.0.txt','LICENSES/MIT.txt']:
   file=headers/relative
   if not file.is_file():raise RuntimeError('Missing Vulkan-Headers notice: '+relative)
   z.write(file,role+'/Licenses/vulkan-headers/'+relative)
  readme=f'''SideScreen {VERSION} - Windows 11 x64

Extract the entire folder and run SideScreen.exe on either PC. No Python, SSH or
separate WinUI runtime installation is required. The sidebar has Server, Client
Settings and About pages. Switching pages does not interrupt either role.

On the desktop PC, open Server and choose Start Driver. Administrator approval
is requested only for the separate driver helper. On the second PC, open Client,
enter the server IPv4 address and Connect. No pairing or approval is needed.
The app normally runs unelevated. Use automatic connections on a trusted LAN: TLS
encrypts traffic but does not verify the identity of the other computer.

The Server requires the separately installed DM virtual-display driver. This
installer/ZIP does not install a driver or change signing/security settings.
The current driver is a development test-signed component. Receiving needs no driver.

Start Driver waits for a valid client before creating its display. Each client
supplies resolution/refresh preferences (currently capped at 60 fps). Windows
layout changes are retained. Up to four clients can connect at once. Disconnecting
a client removes only its monitor; other clients and the server keep running.
Stop Driver or tray Exit removes all session monitors.

Closing setup hides to the tray only while the Server driver is running.
Client-only and idle windows exit fully. The tray has Server and Client controls
and Exit. Ctrl+Shift+Q disconnects reception; F11 toggles fullscreen,
F10 reveals the toolbar, F8 pauses input. Reveal the toolbar by hovering briefly
at the top center or swiping down from the small top-center strip.

Normal sessions have no time limit. Debug is off by default. Audio, microphone,
Windows+K and automatic client reconnect are not implemented. Update both PCs
together. Apps/installers are unsigned development builds; protocol version 9.
Toolbar Settings offers 75%/50% stream resolution for the next connection only;
it is not saved and subsequent connections default to Native.
Main-app Settings contains client/server input and debug options. Select the
client renderer before connecting: D3D11 (default), D3D12, Vulkan or OpenGL when supported.
The renderer preference is saved locally and locked while connected.
Unavailable choices stay visible with an explanation. Server capture is D3D11.
Vulkan uses the graphics driver's runtime; no Vulkan SDK installation is needed.
OpenGL requires WGL_NV_DX_interop2 for D3D11 GPU texture sharing. Support is
checked on this PC; hybrid-GPU configurations may reject an adapter and fall back.
'''

  z.writestr(role+'/README.txt',readme)
  z.writestr(role+'/manifest.json',json.dumps({'version':VERSION,'role':role,'files':files},indent=2))
 reports.append({'archive':archive.name,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()})
(out/'SHA256SUMS.txt').write_text(''.join(f"{r['sha256']}  {r['archive']}\n" for r in reports))
print(json.dumps(reports,indent=2))
