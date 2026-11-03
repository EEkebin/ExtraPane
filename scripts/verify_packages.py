"""Validate distributable contents, checksums and embedded app icons without a GPU."""
from pathlib import Path
import os,zipfile,json,hashlib,ctypes,tempfile,subprocess,time,re
from test_uninstall_data import registered_uninstaller
from ctypes import wintypes
ctypes.windll.shell32.ExtractIconW.argtypes=[wintypes.HINSTANCE,wintypes.LPCWSTR,wintypes.UINT]
ctypes.windll.shell32.ExtractIconW.restype=wintypes.HICON
ctypes.windll.user32.DestroyIcon.argtypes=[wintypes.HICON]
root=Path(__file__).resolve().parent.parent
from release_version import current
version=current();dist=root/'out/dist'
for role in ['SideScreen']:
 archive=dist/f'SideScreen-{version}-win-x64.zip'
 with zipfile.ZipFile(archive) as z:
  names=z.namelist();prefix=role+'/'
  assert z.read(prefix+'version.txt').decode().strip()==version
  for required in [role+'.exe','dm-view.exe','SideScreen.Service.exe','SideScreen.DriverSetup.exe','dm-display.exe','SideScreen.ico','README.txt','manifest.json']:
   assert prefix+required in names,required
  for notice in ['LICENSE.md','LICENSES/Apache-2.0.txt','LICENSES/MIT.txt']:
   assert prefix+'Licenses/vulkan-headers/'+notice in names,'Missing Vulkan-Headers notice'
  assert not any(n.lower().endswith(('.pfx','.key','.pem','.log','.bmp','.pdb')) for n in names)
  manifest=json.loads(z.read(prefix+'manifest.json'))
  for exe,level in [('SideScreen.exe',b'asInvoker'),('SideScreen.Service.exe',b'requireAdministrator')]:
   assert version.encode('utf-16-le') in z.read(prefix+exe), f'Wrong embedded version: {exe}'
   manifests=re.findall(rb'<requestedExecutionLevel[^>]+>',z.read(prefix+exe))
   assert any(level in entry for entry in manifests),f'Wrong elevation manifest: {exe}'

  for entry in manifest['files']:
   assert hashlib.sha256(z.read(prefix+entry['path'])).hexdigest()==entry['sha256'],entry['path']
  with tempfile.TemporaryDirectory() as folder:
   z.extractall(folder)
   for exe in [role+'.exe','dm-view.exe','SideScreen.Service.exe']:
    icon=ctypes.windll.shell32.ExtractIconW(None,str(Path(folder)/role/exe),0)
    assert icon not in (0,1),f'Missing icon: {exe}'
    ctypes.windll.user32.DestroyIcon(icon)
 installer=dist/f'SideScreen-{version}-Setup-win-x64.exe'
 assert installer.stat().st_size>1_000_000
 with tempfile.TemporaryDirectory(prefix='SideScreen-installer-') as folder:
  target=Path(folder)/role
  subprocess.run([str(installer),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',f'/DIR={target}',f'/GROUP=SideScreen Validation {role}','/TASKS='],check=True,timeout=90)
  for entry in manifest['files']:
   assert hashlib.sha256((target/entry['path']).read_bytes()).hexdigest()==entry['sha256'],entry['path']
  subprocess.run([str(registered_uninstaller(target)),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'],check=True,timeout=90)
  for attempt in range(100):
   if not (target/(role+'.exe')).exists():break
   time.sleep(.1)
  assert not (target/(role+'.exe')).exists(),'Uninstaller left the app executable'

for line in (dist/'SHA256SUMS.txt').read_text().splitlines():
 checksum,name=line.split('  ',1)
 assert hashlib.sha256((dist/name).read_bytes()).hexdigest()==checksum,name
print('Release artifacts passed: manifests, icons, two downloads and SHA256 checksums')

from test_uninstall_data import verify
verify(installer)
