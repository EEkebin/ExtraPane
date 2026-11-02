"""Build distributable self-contained folders as ZIPs; no credentials or local settings."""
from pathlib import Path
import zipfile,hashlib,json
ROOT=Path(__file__).resolve().parent.parent
VERSION='0.10.0'
out=ROOT/'out/dist';out.mkdir(parents=True,exist_ok=True)
reports=[]
for role in ['Client','Server']:
 source=ROOT/'out/winui'/role
 if not (source/(role+'.exe')).exists():raise RuntimeError('Run scripts/build-apps.ps1 first')
 archive=out/f'DM-{VERSION}-{role}-win-x64.zip'
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
  readme=f'''DM {role} {VERSION} — Windows 11 x64 development build

Extract this entire folder, then run {role}.exe. Keep the runtime DLLs beside it.
No Python, SSH, WinUI runtime installation or internet connection is required to run.

Run Server on the computer whose desktop you want to extend. Run Client on the
second PC. Enter the LAN addresses, pair once, Start display, then Connect.
Pairing keys are stored in each user's Windows Credential Manager, never this ZIP.

Server requires administrator elevation and the separately installed DM virtual
display driver. This ZIP does not install a driver or change signing/security
settings. The current driver is a development test-signed component: these app
bundles are not a production signed-driver installer for a clean third-party PC.
Client needs no virtual-display driver.

The client's display supplies resolution and refresh preferences. Current driver
modes: 1280x720 (30/60), 1920x1080 (60), 3000x2000 (60), plus rotation. The stream
currently caps at 60 fps. A supported fallback is shown if exact matching is not
available. Windows Display Settings can override layout/resolution/orientation;
that layout is remembered for the same client resolution profile.

The display window is resizable. Use its top toolbar for minimize, maximize,
fullscreen or close. The toolbar hides automatically; move the mouse to the top
edge or touch/drag down from the top edge to reveal it. F10 reveals the toolbar,
F11 toggles fullscreen, F8 pauses input, F9 closes the display. Local window controls
are not forwarded to the server.

Normal sessions have no time limit. Debug is off by default. Audio, microphone,
Windows+K/Miracast and automatic sleep/network reconnection are not implemented.
Update Client and Server together: this release uses protocol 7.
'''
  z.writestr(role+'/README.txt',readme)
  z.writestr(role+'/manifest.json',json.dumps({'version':VERSION,'role':role,'files':files},indent=2))
 reports.append({'archive':archive.name,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()})
(out/'SHA256SUMS.txt').write_text(''.join(f"{r['sha256']}  {r['archive']}\n" for r in reports))
print(json.dumps(reports,indent=2))

