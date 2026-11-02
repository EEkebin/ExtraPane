"""Fixed stream profiles over real TLS/H264; no feedback gate or adaptive resets."""
import json,socket,ssl,struct,subprocess,time
from pathlib import Path
BIN=Path(__file__).resolve().parents[1]/'out/build/windows-release'
def read(stream,n):
 data=b''
 while len(data)<n:
  chunk=stream.recv(n-len(data))
  if not chunk:raise EOFError('Stream closed')
  data+=chunk
 return data
baseline=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
source=baseline.split(' name=')[0]
results=[]
for quality,control in [(0,False),(1,False),(2,False),(0,True),(3,False)]:
 with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
 args=[str(BIN/'dm-view.exe'),'host','--bind','127.0.0.1','--port',str(port),'--lan','--allow-screen','--display',source,'--frames','180','--fps','60']
 if control:args+=['--allow-input']
 host=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 try:
  assert host.stdout.readline().startswith('LISTENING')
  context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);context.check_hostname=False;context.verify_mode=ssl.CERT_NONE
  context.minimum_version=context.maximum_version=ssl.TLSVersion.TLSv1_3
  with context.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=5)) as stream:
   stream.settimeout(10)
   request=bytearray(b'DMVR'+bytes([9,1,3])+bytes(25));request[9]=quality
   if control:request[7]=1;request[8]=15
   stream.sendall(request)
   if quality==3:
    assert stream.recv(1)==b''
   else:
    offer=read(stream,32);size=struct.unpack_from('>II',offer,8);epoch=struct.unpack_from('>Q',offer,24)[0]
    assert size==[(1280,720),(960,540),(640,360)][quality],size
    assert bool(epoch)==control
    stream.sendall(b'RDY9')
    for sequence in range(180):
     header=read(stream,24);number,length=struct.unpack_from('>II',header,4)
     assert number==sequence and length>0,'Unexpected codec/input restart'
     read(stream,length)
     if control:stream.sendall(struct.pack('>4sIQIIII',b'DMIN',1,epoch,sequence,0,0,0))
     # No quality report or per-frame video acknowledgement is sent.
     # Brief receiver stalls must not trigger a stream/input rebuild.
     if sequence in (50,100):time.sleep(.08)
    stream.sendall(b'DONE');stream.unwrap()
  out,err=host.communicate(timeout=8)
  assert (host.returncode==0)==(quality!=3),(out,err)
  assert 'QUALITY_CHANGED' not in out and 'DISPLAY_RECONFIGURING' not in out,out
  if quality==3:assert 'Unknown quality mode' in err
  results.append({'profile':quality,'input':control,'exit':host.returncode})
  assert subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)==baseline
 finally:
  if host.poll() is None:host.kill();host.communicate(timeout=5)
print(json.dumps(results,indent=2))
