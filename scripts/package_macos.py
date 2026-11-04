"""Create a Universal macOS preview ZIP/DMG. Ad-hoc signing is not notarization."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess

root=Path(__file__).resolve().parents[1]
version=os.environ['EXTRAPANE_VERSION']
if not re.fullmatch(r'\d{4}\.\d{2}\.\d{2}(?:[.-][A-Za-z0-9]+)*',version): raise ValueError('Invalid version')
app=root/'out/build/macos/platform/macos/ExtraPane.app'
binary=app/'Contents/MacOS/ExtraPane'
subprocess.run(['lipo',str(binary),'-verify_arch','arm64','x86_64'],check=True)
resources=app/'Contents/Resources';resources.mkdir(exist_ok=True)
for dependency,relative in [('boringssl','boringssl-src'),('mbedtls','mbedtls-src'),('tf-psa-crypto','mbedtls-src/tf-psa-crypto')]:
    license_file=root/'out/build/macos/_deps'/relative/'LICENSE'
    if not license_file.exists(): raise RuntimeError('Missing TLS dependency license: '+str(license_file))
    target=resources/'Licenses'/dependency;target.mkdir(parents=True,exist_ok=True);shutil.copy2(license_file,target/'LICENSE')
icons=root/'out/macos-icons/ExtraPane.iconset';icons.mkdir(parents=True,exist_ok=True)
for size in (16,32,128,256,512):
    for density in (1,2):
        name=f'icon_{size}x{size}'+('@2x' if density==2 else '')+'.png'
        subprocess.run(['sips','-z',str(size*density),str(size*density),str(root/'assets/branding/icon.png'),'--out',str(icons/name)],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['iconutil','-c','icns',str(icons),'-o',str(resources/'ExtraPane.icns')],check=True)
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
subprocess.run(['codesign','--verify','--strict','--verbose=2',str(app)],check=True)
dist=root/'out/dist-macos';dist.mkdir(parents=True,exist_ok=True)
stem=f'ExtraPane-{version}-macos-universal'
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(app),str(dist/(stem+'.zip'))],check=True)
stage=root/'out/macos-dmg';stage.mkdir(parents=True,exist_ok=True)
shutil.copytree(app,stage/'ExtraPane.app',dirs_exist_ok=True)
link=stage/'Applications'
if not link.exists(): link.symlink_to('/Applications')
(stage/'READ ME.txt').write_text('ExtraPane macOS preview\n\nDrag ExtraPane to Applications. macOS 12+; Intel or Apple Silicon.\nThis preview is ad-hoc signed, not Developer ID signed or notarized.\nGatekeeper can block downloaded copies. Use macOS Privacy & Security approval for an app you trust; do not disable Gatekeeper globally.\nHosting requires macOS 13+, Screen Recording and (for remote input) Accessibility permission. Virtual monitors use an undocumented macOS interface; compatibility may change. No driver install is needed. See https://github.com/EEkebin/ExtraPane/blob/main/docs/MACOS.md\n')
subprocess.run(['hdiutil','create','-ov','-format','UDZO','-volname','ExtraPane','-srcfolder',str(stage),str(dist/(stem+'.dmg'))],check=True)
for ext in ('.zip','.dmg'):
    artifact=dist/(stem+ext);artifact.with_suffix(ext+'.sha256').write_text(hashlib.sha256(artifact.read_bytes()).hexdigest()+'  '+artifact.name+'\n')
print('Verified Universal app; created ad-hoc signed ZIP and DMG (not notarized).')
