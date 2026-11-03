"""Live renderer/window regression. Requires an installed driver and an idle desktop.

Never installs a driver. Runs each renderer on connection, not a live switch.
"""
import subprocess,time,json,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
b=ROOT/'out/build/windows-release'
baseline=subprocess.check_output([str(b/'dm-display.exe'),'list'],text=True)
assert 'dm_virtual=1' not in baseline,'Disconnect existing virtual displays before this test'
reports=[]
for initial in ('d3d11','d3d12'):
    with open(f'out/d3d12-{initial}-host.log','w') as ho,open(f'out/d3d12-{initial}-client.log','w') as co:
        host=subprocess.Popen([str(b/'dm-view.exe'),'host','--lan','--bind','127.0.0.1','--port','43832','--allow-screen','--extend','--frames','0','--fps','60'],stdout=ho,stderr=subprocess.STDOUT)
        client=None
        try:
            time.sleep(1)
            client=subprocess.Popen([str(b/'dm-view.exe'),'receive','--lan','--bind','127.0.0.1','--peer','127.0.0.1','--port','43832','--renderer',initial],stdout=co,stderr=subprocess.STDOUT)
            time.sleep(3)
            for args in ([str(client.pid)],[str(client.pid),'--disconnect']):
                result=subprocess.run([str(b/'dm-window-check.exe'),*args],capture_output=True,text=True,timeout=30)
                print(initial,args,result.stdout,result.stderr,flush=True)
                assert result.returncode==0
            client.wait(timeout=12)
            assert client.returncode==0
        finally:
            if client and client.poll() is None:client.kill();client.wait()
            if host.poll() is None:
                try:host.wait(timeout=8)
                except subprocess.TimeoutExpired:host.kill();host.wait()
    text=Path(f'out/d3d12-{initial}-client.log').read_text()
    assert 'RENDERER_FALLBACK' not in text and 'cpu_pixel_uploads=0' in text,text
    assert f'renderer={initial.upper()}' in text,text
    reports.append({'initial':initial,'passed':True,'log':text})
Path('out/d3d12-preview-tests.json').write_text(json.dumps(reports,indent=2))
