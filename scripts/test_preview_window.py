import subprocess,time,uuid,secrets,json
from pathlib import Path
b=Path('out/build/windows-release').resolve();name='window-check-'+uuid.uuid4().hex
subprocess.run([str(b/'dm-session.exe'),'pair','--name',name,'--secret-stdin'],input=secrets.token_hex(32)+'\n',text=True,capture_output=True,check=True)
h=r=None
try:
 h=subprocess.Popen([str(b/'dm-view.exe'),'host','--name',name,'--bind','127.0.0.1','--allow-screen','--allow-input','--extend','--client-mode','--frames','0'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 assert h.stdout.readline().startswith('LISTENING')
 r=subprocess.Popen([str(b/'dm-view.exe'),'receive','--name',name,'--bind','127.0.0.1','--peer','127.0.0.1','--control'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 time.sleep(4)
 subprocess.run([str(b/'dm-window-check.exe'),str(r.pid)],check=True)
 assert h.poll() is None and r.poll() is None,'Stream ended during window changes'
 print('Client auto mode and window controls passed while streaming',flush=True)
finally:
 for p in [r,h]:
  if p and p.poll() is None:p.terminate()
 for p in [r,h]:
  if p:print(p.communicate(timeout=8)[0])
 subprocess.run([str(b/'dm-session.exe'),'revoke','--name',name],capture_output=True)
