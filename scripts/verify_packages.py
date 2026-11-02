"""Validate distributable contents, checksums and embedded app icons without a GPU."""
from pathlib import Path
import os,zipfile,json,hashlib,ctypes,tempfile,subprocess,time
from ctypes import wintypes
ctypes.windll.shell32.ExtractIconW.argtypes=[wintypes.HINSTANCE,wintypes.LPCWSTR,wintypes.UINT]
ctypes.windll.shell32.ExtractIconW.restype=wintypes.HICON
ctypes.windll.user32.DestroyIcon.argtypes=[wintypes.HICON]
root=Path(__file__).resolve().parent.parent
version=os.environ.get('SIDESCREEN_VERSION','0.12.0');dist=root/'out/dist'
for role in ['Client','Server']:
 archive=dist/f'SideScreen-{version}-{role}-win-x64.zip'
 with zipfile.ZipFile(archive) as z:
  names=z.namelist();prefix=role+'/'
  for required in [role+'.exe','dm-view.exe','SideScreen.ico','README.txt','manifest.json']:
   assert prefix+required in names,required
  assert not any(n.lower().endswith(('.pfx','.key','.pem','.log','.bmp','.pdb')) for n in names)
  manifest=json.loads(z.read(prefix+'manifest.json'))
  for entry in manifest['files']:
   assert hashlib.sha256(z.read(prefix+entry['path'])).hexdigest()==entry['sha256'],entry['path']
  with tempfile.TemporaryDirectory() as folder:
   z.extractall(folder)
   for exe in [role+'.exe','dm-view.exe']:
    icon=ctypes.windll.shell32.ExtractIconW(None,str(Path(folder)/role/exe),0)
    assert icon not in (0,1),f'Missing icon: {exe}'
    ctypes.windll.user32.DestroyIcon(icon)
 installer=dist/f'SideScreen-{version}-{role}-Setup-win-x64.exe'
 assert installer.stat().st_size>1_000_000
 with tempfile.TemporaryDirectory(prefix='SideScreen-installer-') as folder:
  target=Path(folder)/role
  subprocess.run([str(installer),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',f'/DIR={target}',f'/GROUP=SideScreen Validation {role}','/TASKS='],check=True,timeout=90)
  for entry in manifest['files']:
   assert hashlib.sha256((target/entry['path']).read_bytes()).hexdigest()==entry['sha256'],entry['path']
  subprocess.run([str(target/'unins000.exe'),'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'],check=True,timeout=90)
  for attempt in range(100):
   if not (target/(role+'.exe')).exists():break
   time.sleep(.1)
  assert not (target/(role+'.exe')).exists(),'Uninstaller left the app executable'

for line in (dist/'SHA256SUMS.txt').read_text().splitlines():
 checksum,name=line.split('  ',1)
 assert hashlib.sha256((dist/name).read_bytes()).hexdigest()==checksum,name
print('Release artifacts passed: manifests, icons, four downloads and SHA256 checksums')
