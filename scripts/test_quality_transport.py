"""Real H264/TLS host with an independent quality-feedback peer; no input injection."""
import json,secrets,socket,ssl,struct,subprocess,time,uuid
from pathlib import Path
BIN=Path(__file__).resolve().parents[1]/'out/build/windows-release'
def read(stream,n):
 data=b''
 while len(data)<n:
  chunk=stream.recv(n-len(data))
  if not chunk:raise EOFError('Stream closed')
  data+=chunk
 return data
if not hasattr(ssl.SSLContext,'set_psk_client_callback'):raise RuntimeError('Run this hardware test with Python 3.13 or newer')
name='quality-'+uuid.uuid4().hex;key=secrets.token_bytes(32)
baseline=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
subprocess.run([str(BIN/'dm-session.exe'),'pair','--name',name,'--secret-stdin'],input=key.hex()+'\n',text=True,capture_output=True,check=True)
results={}
try:
 for invalid in (False,True):
  with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
  host=subprocess.Popen([str(BIN/'dm-view.exe'),'host','--bind','127.0.0.1','--port',str(port),'--name',name,'--allow-screen','--frames','45','--fps','30'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   assert host.stdout.readline().startswith('LISTENING')
   context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);context.check_hostname=False;context.verify_mode=ssl.CERT_NONE
   context.minimum_version=ssl.TLSVersion.TLSv1_3;context.maximum_version=ssl.TLSVersion.TLSv1_3
   context.set_psk_client_callback(lambda hint:(name,key))
   with context.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=5)) as stream:
    stream.settimeout(10);stream.sendall(b'DMVR'+bytes([8,1,3])+bytes(25))
    offer=read(stream,32);initial=struct.unpack_from('>II',offer,8);modes=[initial];stream.sendall(b'RDY8')
    for sequence in range(45):
     header=read(stream,24);length=struct.unpack_from('>I',header,8)[0]
     while length==0:
      offer=read(stream,32);modes.append(struct.unpack_from('>II',offer,8));stream.sendall(b'RDY8');header=read(stream,24);length=struct.unpack_from('>I',header,8)[0]
     assert struct.unpack_from('>I',header,4)[0]==sequence
     read(stream,length)
     quality=3 if invalid else (2 if 5<=sequence<20 else 1)
     stream.sendall(struct.pack('>4sIIIII',b'DMQ'+bytes([8]),sequence,quality,1000,1000,0))
     if invalid:break
    if not invalid:stream.sendall(b'DONE');stream.unwrap()
   out,err=host.communicate(timeout=8)
   if invalid:assert host.returncode!=0 and 'quality feedback' in err
   else:
    assert host.returncode==0,(out,err)
    expected=tuple(max(2,int(n*.75)//2*2) for n in initial)
    assert expected in modes and modes[-1]==initial,modes
    assert 'display_unchanged=1' in out
   results['invalid' if invalid else 'live_switch']={'modes':modes,'exit':host.returncode}
   assert subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)==baseline,'Physical display geometry changed'
  finally:
   if host.poll() is None:host.kill();host.communicate(timeout=5)
finally:subprocess.run([str(BIN/'dm-session.exe'),'revoke','--name',name],capture_output=True,check=True)
print(json.dumps(results,indent=2))
