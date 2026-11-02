"""Persistent driver/listener lifecycle, authentication failure and client reconnect."""
from pathlib import Path
import subprocess,secrets,time,uuid,threading,queue,winreg,json
ROOT=Path(__file__).resolve().parent.parent;B=ROOT/'out/build/windows-release'
name='service-'+uuid.uuid4().hex;bad=name+'-wrong';host=None;receiver=None;lines=[];events=queue.Queue()
baseline=subprocess.check_output([str(B/'dm-display.exe'),'list'],text=True)
try:
 with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:saved=winreg.QueryValueEx(k,'Layout')
except FileNotFoundError:saved=None
def wait(prefix,timeout=30):
 until=time.monotonic()+timeout
 while time.monotonic()<until:
  try:line=events.get(timeout=.2)
  except queue.Empty:continue
  if line.startswith('error:'):raise RuntimeError(line)
  if line.startswith(prefix):return line
 raise TimeoutError(prefix+repr(lines))
def topology():return subprocess.check_output([str(B/'dm-display.exe'),'list'],text=True)
def identity():return next(line.split(' name=')[0] for line in topology().splitlines() if 'dm_virtual=1' in line)
def receive(pair):return subprocess.Popen([str(B/'dm-view.exe'),'receive','--name',pair,'--bind','auto','--peer','127.0.0.1','--port','43829','--control'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
try:
 for pair in [name,bad]:subprocess.run([str(B/'dm-session.exe'),'pair','--name',pair,'--secret-stdin'],input=secrets.token_hex(32)+'\n',text=True,capture_output=True,check=True)
 host=subprocess.Popen([str(B/'dm-view.exe'),'host','--name',name,'--bind','auto','--port','43829','--allow-screen','--allow-input','--extend','--serve'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 def read():
  for line in host.stdout:lines.append(line.strip());events.put(line.strip())
 threading.Thread(target=read,daemon=True).start()
 wait('DRIVER_STARTED');wait('LISTENING');source=identity();assert host.poll() is None
 print('Driver exists before any client connects',flush=True)
 receiver=receive(bad);receiver.wait(timeout=12);assert receiver.returncode!=0
 wait('DRIVER_WAITING');wait('LISTENING');assert identity()==source
 print('Unpaired client rejected; driver stays ready',flush=True)
 receiver=receive(name);wait('VIDEO_STARTED')
 subprocess.run([str(B/'dm-display.exe'),'configure','1080','1920','1','-1080','100','60'],check=True,capture_output=True)
 wait('DISPLAY_RECONFIGURED');receiver.terminate();receiver.wait(timeout=5)
 wait('DRIVER_WAITING');wait('LISTENING');assert identity()==source
 assert 'bounds=-1080,100,0,2020' in topology()
 subprocess.run([str(B/'dm-display.exe'),'configure','1080','1920','1','-1080','200','60'],check=True,capture_output=True)
 receiver=receive(name);assert '1080x1920' in wait('VIDEO_STARTED');assert identity()==source
 assert 'bounds=-1080,200,0,2120' in topology()
 print('Reconnect kept monitor identity, portrait resolution and placement',flush=True)
 receiver.terminate();receiver.wait(timeout=5);wait('DRIVER_WAITING');wait('LISTENING')
 host.terminate();host.wait(timeout=8)
 (ROOT/'out/driver-service-test.json').write_text(json.dumps({'logs':lines,'monitor_retained':True,'authentication_required':True},indent=2))
finally:
 for p in [receiver,host]:
  if p and p.poll() is None:p.terminate();p.wait(timeout=8)
 for pair in [name,bad]:subprocess.run([str(B/'dm-session.exe'),'revoke','--name',pair],capture_output=True)
 with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:
  if saved:winreg.SetValueEx(k,'Layout',0,saved[1],saved[0])
  else:
   try:winreg.DeleteValue(k,'Layout')
   except FileNotFoundError:pass
 until=time.monotonic()+8
 while time.monotonic()<until:
  try:
   if topology()==baseline:break
  except subprocess.CalledProcessError:pass
  time.sleep(.1)
 else:raise RuntimeError('Physical topology was not restored')
print('DRIVER_SERVICE passed',flush=True)
