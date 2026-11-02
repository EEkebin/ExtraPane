"""Independent OpenSSL TLS peer tests input grants, epochs, bounds, and teardown.

Requires Python 3.13+ PSK support, interactive Windows, and the installed DM driver.
Only sends reset events; does not inject keys or touch into unrelated apps.
"""
import json
from pathlib import Path
import secrets
import socket
import ssl
import struct
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parent.parent
BIN=ROOT/'out/build/windows-release'


def read(stream,size):
    out=b''
    while len(out)<size:
        part=stream.recv(size-len(out))
        if not part: raise EOFError('TLS stream closed')
        out+=part
    return out


def main():
    if not hasattr(ssl.SSLContext,'set_psk_client_callback'): raise RuntimeError('Run with Python 3.13+ for TLS PSK support')
    name='input-test-'+uuid.uuid4().hex
    key=secrets.token_bytes(32)
    baseline=subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True)
    reports={}
    paired=subprocess.run([str(BIN/'dm-session.exe'),'pair','--name',name,'--secret-stdin'],input=key.hex()+'\n',text=True,capture_output=True,timeout=5)
    if paired.returncode: raise RuntimeError(paired.stderr)
    try:
        for case in ['denied','wrong-epoch','wrong-sequence','oversize-count','invalid-pen-pressure','valid-reset']:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
            command=[str(BIN/'dm-view.exe'),'host','--name',name,'--bind','127.0.0.1','--port',str(port),'--allow-screen','--extend','--frames','3','--fps','30']
            if case!='denied': command+=['--allow-input']
            host=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                if not host.stdout.readline().startswith('LISTENING '): raise RuntimeError('Host did not listen')
                context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);context.check_hostname=False
                context.minimum_version=context.maximum_version=ssl.TLSVersion.TLSv1_3
                context.set_psk_client_callback(lambda hint:(name,key))
                with context.wrap_socket(socket.create_connection(('127.0.0.1',port),timeout=5)) as stream:
                    stream.settimeout(20)
                    stream.sendall(b'DMVR'+bytes([7,1,3,1,15])+bytes(23))
                    if case=='denied':
                        try: read(stream,32)
                        except (EOFError,ssl.SSLError): pass
                    else:
                        offer=read(stream,32);epoch=struct.unpack_from('>Q',offer,24)[0]
                        if not epoch or offer[7]!=15: raise RuntimeError('Expected full native input grant')
                        stream.sendall(b'RDY7')
                        for sequence in range(3):
                            header=read(stream,24);length=struct.unpack_from('>I',header,8)[0];read(stream,length)
                            sent_epoch=epoch^(1 if case=='wrong-epoch' else 0)
                            sent_sequence=sequence+(1 if case=='wrong-sequence' else 0)
                            count=129 if case=='oversize-count' else 1
                            packet=struct.pack('>4sIQIIII',b'DMIN',1,sent_epoch,sent_sequence,count,count*64,0)
                            payload=bytearray(64) # valid reset
                            if case=='invalid-pen-pressure':
                                payload[0:4]=bytes([4,1,0,3]);struct.pack_into('>I',payload,4,1);struct.pack_into('>I',payload,24,1025)
                            stream.sendall(packet+payload)
                            if case!='valid-reset':break
                        if case=='valid-reset':stream.sendall(b'DONE');stream.unwrap()
                out,err=host.communicate(timeout=6)
                success=case=='valid-reset'
                if (host.returncode==0)!=success:raise RuntimeError(case+': '+out+err)
                if case=='valid-reset' and 'INPUT_SESSION events=3 granted=1' not in out:raise RuntimeError('Input reset batch count mismatch')
                reports[case]={'exit':host.returncode,'output':out.strip(),'error':err.strip()}
                print(case,'passed',flush=True)
            except Exception as failure:
                if host.poll() is None:host.kill()
                out,err=host.communicate(timeout=5)
                raise RuntimeError(f'{case}: {failure}; host exit={host.returncode}; {out}; {err}') from failure
            finally:
                if host.poll() is None:host.kill();host.communicate(timeout=5)
            until=time.monotonic()+8
            while time.monotonic()<until:
                try:
                    if subprocess.check_output([str(BIN/'dm-display.exe'),'list'],text=True,stderr=subprocess.DEVNULL)==baseline:break
                except subprocess.CalledProcessError:pass
                time.sleep(.1)
            else:raise RuntimeError('Monitor topology not restored after '+case)
    finally:
        subprocess.run([str(BIN/'dm-session.exe'),'revoke','--name',name],capture_output=True,timeout=5)
    (ROOT/'out/input-transport-test.json').write_text(json.dumps(reports,indent=2),encoding='utf-8')


if __name__=='__main__':main()
