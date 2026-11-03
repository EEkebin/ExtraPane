"""Live DM-only arrangement/resolution/rotation recovery; requires installed driver."""
import subprocess,threading,queue,time,secrets,uuid,json,winreg,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
BIN=ROOT/'out/build/windows-release'
parser=argparse.ArgumentParser();parser.add_argument('--renderer',choices=('d3d11','d3d12','vulkan'),default='d3d11');renderer=parser.parse_args().renderer
name='reconfigure-'+uuid.uuid4().hex
baseline=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
assert 'dm_virtual=1' not in baseline,'Disconnect existing virtual displays before this test'
try:
 with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:saved=winreg.QueryValueEx(k,'Layout')
except FileNotFoundError:saved=None
secret=secrets.token_hex(32)+'\n'
subprocess.run([str(BIN/'dm-session.exe'),'pair','--name',name,'--secret-stdin'],input=secret,text=True,capture_output=True,check=True)
del secret
processes=[];logs=[]
def spawn(args):
 p=subprocess.Popen([str(BIN/'dm-view.exe'),*args],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);processes.append(p)
 q=queue.Queue();lines=[];logs.append(lines)
 def read():
  for line in p.stdout:lines.append(line.strip());q.put(line.strip())
 threading.Thread(target=read,daemon=True).start();return p,q,lines
def wait(q,prefix,seconds=30):
 until=time.monotonic()+seconds
 while time.monotonic()<until:
  try:line=q.get(timeout=.2)
  except queue.Empty:continue
  if line.startswith('error:'):raise RuntimeError(line)
  if line.startswith(prefix):return line
 raise TimeoutError(prefix+repr(logs))
def start():
 h,hq,hl=spawn(['host','--name',name,'--bind','127.0.0.1','--port','43827','--allow-screen','--allow-input','--extend','--keep-layout','--width','1280','--height','720','--fps','60','--frames','0'])
 wait(hq,'LISTENING')
 r,rq,rl=spawn(['receive','--name',name,'--bind','127.0.0.1','--peer','127.0.0.1','--port','43827','--control','--renderer',renderer])
 wait(hq,'VIDEO_STARTED');wait(rq,'VIDEO_RECEIVING');return h,hq,r,rq
try:
 h,hq,r,rq=start();identity=None
 for mode in [(1280,720,0,-1280,0,60),(1920,1080,0,-1920,0,60),(1080,1920,1,-1080,0,60),(1080,1920,3,-1080,0,60),(1920,1080,2,-1920,0,60),(1080,1920,1,-1080,100,60)]:
  subprocess.run([str(BIN/'dm-display.exe'),'configure',*map(str,mode)],check=True,capture_output=True)
  host=wait(hq,'DISPLAY_RECONFIGURED');receiver=wait(rq,'DISPLAY_RECONFIGURED')
  time.sleep(.5)
  assert h.poll() is None and r.poll() is None,(logs,mode)
  topology=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
  current=next(line.split(' name=')[0] for line in topology.splitlines() if 'dm_virtual=1' in line)
  if identity is None:identity=current
  assert current==identity,'Virtual monitor was recreated'
  print(host,receiver,flush=True)
 r.terminate();r.wait(timeout=5);h.wait(timeout=8);time.sleep(1)
 h,hq,r,rq=start()
 time.sleep(1)
 assert any('VIDEO_STARTED 1080x1920' in line for line in logs[-2]),logs
 topology=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
 assert 'bounds=-1080,100,0,2020' in topology,topology
 print('Remembered portrait mode and negative placement restored after restart',flush=True)
 if renderer!='d3d11':
  assert not any('RENDERER_UNAVAILABLE' in line or 'RENDERER_FALLBACK' in line for log in logs for line in log),logs
  assert all(any(f'RENDERER_CHANGED {renderer}'.lower() in line.lower() for line in log) for log in logs[1::2]),logs
finally:
 for p in processes:
  if p.poll() is None:p.terminate()
 for p in processes:p.wait(timeout=8)
 subprocess.run([str(BIN/'dm-session.exe'),'revoke','--name',name],capture_output=True)
 with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:
  if saved:winreg.SetValueEx(k,'Layout',0,saved[1],saved[0])
  else:
   try:winreg.DeleteValue(k,'Layout')
   except FileNotFoundError:pass
 until=time.monotonic()+8
 while time.monotonic()<until:
  after=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
  if after==baseline:break
  time.sleep(.1)
 assert after==baseline,(baseline,after)
 (ROOT/f'out/display-reconfigure-{renderer}-test.json').write_text(json.dumps({'logs':logs,'baseline_restored':True},indent=2))
