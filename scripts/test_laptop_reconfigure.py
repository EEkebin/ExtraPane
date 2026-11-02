"""Two-device live geometry changes and post-change input delivery. Development only."""
from pathlib import Path
import subprocess,time,threading,queue,json
from concurrent.futures import ThreadPoolExecutor
from windows_peer import ROOT,WindowsPeer,TestFirewall,quote
BIN=ROOT/'out/build/windows-release'
peer=WindowsPeer('10.0.20.119','kebinBook')
host=None;pattern=None;pool=ThreadPoolExecutor(max_workers=1);lines=[];q=queue.Queue()
baseline=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
def wait(prefix,timeout=45):
 until=time.monotonic()+timeout
 while time.monotonic()<until:
  try:line=q.get(timeout=.2)
  except queue.Empty:continue
  if line.startswith('error:'):raise RuntimeError(line)
  if line.startswith(prefix):return line
 raise TimeoutError(prefix+repr(lines))
try:
 remote=peer.upload(BIN/'dm-view.exe');drive=peer.upload(BIN/'dm-input-drive.exe')
 with TestFirewall(peer,'10.0.0.210',BIN/'dm-view.exe',remote,43824):
  host=subprocess.Popen([str(BIN/'dm-view.exe'),'host','--name','dm-lan','--bind','10.0.0.210','--allow-screen','--allow-input','--extend','--width','3000','--height','2000','--fps','60','--frames','7200'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  def reader():
   for line in host.stdout:lines.append(line.strip());q.put(line.strip())
  threading.Thread(target=reader,daemon=True).start();wait('LISTENING')
  receive=pool.submit(peer.run,f'& {quote(remote)} receive --name dm-lan --bind 10.0.20.119 --peer 10.0.0.210 --control --fullscreen',interactive=True,timeout=240)
  wait('VIDEO_STARTED');pattern=subprocess.Popen([str(BIN/'dm-display.exe'),'pattern','180'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  time.sleep(2)
  identities=[]
  for mode in [(3000,2000,0,-3000,0,60),(1080,1920,1,-1080,0,60)]:
   subprocess.run([str(BIN/'dm-display.exe'),'configure',*map(str,mode)],check=True,capture_output=True)
   print(wait('DISPLAY_RECONFIGURED'),flush=True);time.sleep(1)
   identities.append(next(line.split(' name=')[0] for line in subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True).splitlines() if 'dm_virtual=1' in line))
  _,exercise,_=peer.run(f'& {quote(drive)}',interactive=True)
  print('Post-rotation input exercise sent',flush=True)
  for mode in [(1920,1080,0,-1920,0,60),(3000,2000,0,4480,0,60)]:
   subprocess.run([str(BIN/'dm-display.exe'),'configure',*map(str,mode)],check=True,capture_output=True)
   print(wait('DISPLAY_RECONFIGURED'),flush=True);time.sleep(1)
   identities.append(next(line.split(' name=')[0] for line in subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True).splitlines() if 'dm_virtual=1' in line))
  assert len(set(identities))==1,identities
  _,receiver,_=receive.result(timeout=200);host.wait(timeout=10)
  assert host.returncode==0,lines
  assert 'decoded_frames=7200' in receiver and receiver.count('DISPLAY_RECONFIGURED')>=4,receiver
  observed=pattern.communicate(timeout=5)[0]
  print(receiver,flush=True);print(observed,flush=True)
  (ROOT/'out/laptop-reconfigure-test.json').write_text(json.dumps({'host':lines,'receiver':receiver,'input':observed,'identity_preserved':True},indent=2))
  assert all(value in observed for value in ['mouse=1','chars=1','touch=2','pen=1','pressure_max=897']),observed
finally:
 if host and host.poll() is None:host.terminate();host.wait(timeout=8)
 if pattern and pattern.poll() is None:pattern.terminate();pattern.wait(timeout=8)
 pool.shutdown(wait=False,cancel_futures=True);peer.close()
 until=time.monotonic()+8
 while time.monotonic()<until:
  after=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
  if after==baseline:break
  time.sleep(.1)
 assert after==baseline,(after,baseline)
