"""Interactive hardware transport checks: receiver loss, host loss, clean restart.

Uses loopback, a disposable Credential Manager entry, and the active primary
display. No files containing screenshots, keys, or media are saved.
"""
import json
import argparse
from pathlib import Path
import secrets
import socket
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parent.parent


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--display',help='Explicit GDI monitor name; exercises WGC instead of default DXGI capture')
    parser.add_argument('--extend',action='store_true',help='Exercise installed DM driver and assert monitor lifetime/topology')
    parser.add_argument('--native',action='store_true',help='Use 3000x2000 at 60 fps (requires --extend)')
    args=parser.parse_args()
    if args.native and not args.extend: parser.error('--native requires --extend')
    if args.extend and args.display: parser.error('Choose --display or --extend')
    folder=ROOT/'out/build/windows-release'
    session,view=folder/'dm-session.exe',folder/'dm-view.exe'
    name='recovery-'+uuid.uuid4().hex
    processes=[]
    results={}
    display_tool=folder/'dm-display.exe'
    def topology():
        return subprocess.check_output([str(display_tool),'list'],text=True,timeout=5).strip().splitlines()
    baseline=topology() if args.extend else None
    def check_extended():
        if not args.extend: return
        current=topology()
        physical=[line for line in current if 'dm_virtual=0' in line]
        if physical!=[line for line in baseline if 'dm_virtual=0' in line] or sum('dm_virtual=1' in line for line in current)!=1:
            raise RuntimeError('Expected one independent DM display with unchanged physical monitors: '+repr(current))
        results['active_topology']=current
    def check_removed():
        if not args.extend: return
        until=time.monotonic()+8
        while time.monotonic()<until:
            if topology()==baseline: return
            time.sleep(0.1)
        raise RuntimeError('Original display topology did not return after session termination')

    def free_port():
        with socket.socket() as s:
            s.bind(('127.0.0.1',0))
            return str(s.getsockname()[1])

    def start_host(port,frames):
        p=subprocess.Popen([str(view),'host','--name',name,'--bind','127.0.0.1',
                            '--port',port,'--allow-screen','--frames',str(frames),'--fps','60' if args.native else '30']+(['--width','3000','--height','2000'] if args.native else [])+(['--display',args.display] if args.display else [])+(['--extend'] if args.extend else []),
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        processes.append(p)
        if not p.stdout.readline().startswith('LISTENING '):
            raise RuntimeError('Host failed to start: '+p.stderr.read())
        return p

    def start_receiver(port):
        p=subprocess.Popen([str(view),'receive','--name',name,'--bind','127.0.0.1',
                            '--peer','127.0.0.1','--port',port,'--headless'],
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        processes.append(p)
        while True:
            line=p.stdout.readline()
            if line.startswith('VIDEO_RECEIVING '):
                break
            if not line:
                receiver_error=p.stderr.read()
                host_out,host_error=processes[-2].communicate(timeout=5)
                raise RuntimeError('Receiver failed before video: '+receiver_error+' Host: '+host_out+host_error)
        return p

    try:
        r=subprocess.run([str(session),'pair','--name',name,'--secret-stdin'],input=secrets.token_hex(32)+'\n',
                         capture_output=True,text=True,timeout=5)
        if r.returncode:
            raise RuntimeError(r.stderr)
        port=free_port()
        host=start_host(port,180)
        receiver=start_receiver(port)
        check_extended()
        time.sleep(0.2)
        started=time.monotonic()
        receiver.kill(); receiver.communicate(timeout=3)
        out,err=host.communicate(timeout=8)
        if host.returncode==0 or 'VIDEO_SENT' in out:
            raise RuntimeError('Host incorrectly reported success after receiver loss')
        results['receiver_loss']={'host_error':err.strip(),'exit_seconds':round(time.monotonic()-started,3)}
        check_removed()
        print('Receiver loss terminated the host:',err.strip(),flush=True)

        # Reuse the same listener port; recovery must not require switching ports.
        host=start_host(port,180)
        receiver=start_receiver(port)
        check_extended()
        time.sleep(0.2)
        started=time.monotonic()
        host.kill(); host.communicate(timeout=3)
        out,err=receiver.communicate(timeout=8)
        if receiver.returncode==0 or 'VIDEO_RECEIVED' in out:
            raise RuntimeError('Receiver incorrectly reported success after host loss')
        results['host_loss']={'receiver_error':err.strip(),'exit_seconds':round(time.monotonic()-started,3)}
        check_removed()
        print('Host loss terminated the receiver:',err.strip(),flush=True)

        host=start_host(port,30)
        receiver=start_receiver(port)
        check_extended()
        receiver_out,receiver_err=receiver.communicate(timeout=12)
        host_out,host_err=host.communicate(timeout=8)
        if host.returncode or receiver.returncode or 'VIDEO_SENT frames=30 ' not in host_out or 'VIDEO_RECEIVED decoded_frames=30 ' not in receiver_out:
            raise RuntimeError('Recovery run failed: '+host_out+host_err+receiver_out+receiver_err)
        results['restart']={'host':host_out.strip(),'receiver':receiver_out.strip()}
        check_removed()
        print('Fresh H264 session decoded all 30 frames after both failures',flush=True)
    finally:
        for p in processes:
            if p.poll() is None:
                p.kill(); p.communicate(timeout=3)
        subprocess.run([str(session),'revoke','--name',name],capture_output=True,timeout=5)
    results['source']='session-owned DM virtual display' if args.extend else (args.display or 'primary DXGI')
    suffix='-extend' if args.extend else ('-selected-display' if args.display else '')
    (ROOT/f'out/video-recovery{suffix}-test.json').write_text(json.dumps(results,indent=2),encoding='utf-8')


if __name__=='__main__':
    main()
