"""Live automatic-LAN multi-monitor test. Requires the already installed driver.

Creates two independent TLS clients, checks distinct display modes, disconnects
one while the other keeps receiving, then reconnects. Never installs a driver.
"""
import json, socket, ssl, struct, subprocess, threading, time, winreg
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'out/build/windows-release'

def topology():
    return subprocess.check_output([str(BIN/'dm-display.exe'), 'list'], text=True, stderr=subprocess.PIPE)

def read(stream, count):
    result = b''
    while len(result) < count:
        data = stream.recv(count-len(result))
        if not data: raise EOFError('Client closed')
        result += data
    return result

def wait(predicate, seconds=35):
    until = time.monotonic()+seconds
    while time.monotonic() < until:
        try:
            if predicate(): return
        except subprocess.CalledProcessError:pass # Windows is applying a topology transaction.
        time.sleep(.1)
    raise AssertionError('Timed out waiting for session/topology')

class Client:
    def __init__(self, port, width, height):
        self.count=0; self.error=None; self.stop=False; self.size=None
        ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname=False; ctx.verify_mode=ssl.CERT_NONE
        ctx.minimum_version=ctx.maximum_version=ssl.TLSVersion.TLSv1_3
        self.stream=ctx.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=10))
        self.stream.settimeout(45)
        request=bytearray(b'DMVR'+bytes([8,1,3])+bytes(25))
        struct.pack_into('>III',request,16,width,height,30)
        self.stream.sendall(request)
        self.thread=threading.Thread(target=self.run,daemon=True); self.thread.start()
    def offer(self):
        offer=read(self.stream,32); assert offer[:5]==b'DMVO\x08',offer
        self.size=struct.unpack_from('>II',offer,8);self.stream.sendall(b'RDY8')
    def run(self):
        try:
            self.offer()
            while not self.stop:
                header=read(self.stream,24);sequence,length=struct.unpack_from('>II',header,4)
                if not length: self.offer();continue
                read(self.stream,length)
                self.stream.sendall(struct.pack('>4sIIIII',b'DMQ\x08',sequence,1,1000,1000,0))
                self.count+=1
        except Exception as error:
            if not self.stop:self.error=repr(error)
    def close(self):
        self.stop=True
        try:self.stream.shutdown(socket.SHUT_RDWR)
        except OSError:pass
        self.stream.close();self.thread.join(timeout=5)

try:
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as key:saved_layout=winreg.QueryValueEx(key,'Layout-127.0.0.1')
except FileNotFoundError:saved_layout=None
baseline=topology();assert 'dm_virtual=1' not in baseline,'Stop existing SideScreen server before this test'
with socket.socket() as port_probe:
    port_probe.bind(('127.0.0.1',0));port=port_probe.getsockname()[1]
clients=[];logs=[]
host=subprocess.Popen([str(BIN/'dm-view.exe'),'host','--lan','--bind','127.0.0.1','--port',str(port),'--allow-screen','--extend','--serve'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
def log():
    for line in host.stdout:logs.append(line.rstrip())
threading.Thread(target=log,daemon=True).start()
try:
    wait(lambda:any(line.startswith('LISTENING') for line in logs))
    assert topology()==baseline,'Start created an unsolicited monitor'
    first=Client(port,1280,720);clients.append(first)
    wait(lambda:first.count>10 or first.error)
    assert not first.error,(first.error,logs)
    second=Client(port,1920,1080);clients.append(second)
    wait(lambda:second.count>10 or second.error)
    assert not second.error,(second.error,logs)
    assert first.size==(1280,720) and second.size==(1920,1080),(first.size,second.size)
    assert topology().count('dm_virtual=1')==2,topology()
    time.sleep(1);assert first.error is None,(first.error,logs)
    before=second.count;first.close()
    wait(lambda:topology().count('dm_virtual=1')==1)
    wait(lambda:second.count>before+10 or second.error)
    assert not second.error,(second.error,logs)
    replacement=Client(port,1280,720);clients.append(replacement)
    wait(lambda:replacement.count>10 or replacement.error)
    assert not replacement.error,(replacement.error,logs)
    assert topology().count('dm_virtual=1')==2
    third=Client(port,1280,720);clients.append(third)
    wait(lambda:third.count>10 or third.error)
    assert not third.error,(third.error,logs)
    fourth=Client(port,1280,720);clients.append(fourth)
    wait(lambda:fourth.count>10 or fourth.error)
    assert not fourth.error,(fourth.error,logs)
    assert topology().count('dm_virtual=1')==4,topology()
    wait(lambda:second.size==(1920,1080) and all(c.size==(1280,720) for c in (replacement,third,fourth)))
    before=[c.count for c in (second,replacement,third,fourth)]
    time.sleep(2)
    assert all(c.count>n and not c.error for c,n in zip((second,replacement,third,fourth),before)),logs
    third.close();fourth.close();replacement.close();second.close()
    wait(lambda:topology()==baseline)
    assert host.poll() is None,'Server exited when clients disconnected'
    print(json.dumps({'independent_modes':True,'disconnect_isolated':True,'reconnect':True,'server_stays_running':True,'four_simultaneous_streams':True,'frames':[c.count for c in clients]},indent=2))
finally:
    for client in clients:client.close()
    if host.poll() is None:host.terminate();host.wait(timeout=10)
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as key:
        if saved_layout:winreg.SetValueEx(key,'Layout-127.0.0.1',0,saved_layout[1],saved_layout[0])
        else:
            try:winreg.DeleteValue(key,'Layout-127.0.0.1')
            except FileNotFoundError:pass
    (ROOT/'out/multi-client-test.log').write_text('\n'.join(logs))
    wait(lambda:topology()==baseline)
