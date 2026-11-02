"""Stream a bounded encrypted desktop preview to the laptop's signed-in session."""
import argparse
import ipaddress
import json
import secrets
import subprocess
import uuid
from concurrent.futures import ThreadPoolExecutor

from windows_peer import ROOT, WindowsPeer, TestFirewall, quote


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--laptop-ip', required=True, type=ipaddress.IPv4Address)
    p.add_argument('--desktop-ip', required=True, type=ipaddress.IPv4Address)
    p.add_argument('--user', required=True)
    p.add_argument('--headless', action='store_true')
    p.add_argument('--fullscreen',action='store_true')
    p.add_argument('--codec', choices=('h264','jpeg'), default='h264')
    p.add_argument('--frames', type=int, default=180)
    p.add_argument('--fps', type=int, default=30)
    p.add_argument('--width',type=int,default=1280)
    p.add_argument('--height',type=int,default=720)
    p.add_argument('--decoder-adapter', type=int)
    p.add_argument('--display', help='Host GDI display name from dm-display list')
    p.add_argument('--extend',action='store_true',help='Create a session-owned DM monitor on the host')
    p.add_argument('--show-pattern',action='store_true',help='Show a temporary diagnostic window on the DM monitor')
    p.add_argument('--control',action='store_true',help='Explicitly grant and request mouse/keyboard/touch/pen control')
    p.add_argument('--exercise-input',action='store_true',help='Generate native test input on the laptop preview and verify host delivery')
    args = p.parse_args()
    if args.extend and args.display: p.error('Choose --extend or --display')
    if args.show_pattern and not args.extend: p.error('--show-pattern requires --extend')
    if args.control and (args.headless or not args.extend): p.error('--control requires a visible extended-display test')
    if args.exercise_input and not (args.control and args.show_pattern): p.error('--exercise-input requires --control --show-pattern')
    laptop_ip, desktop_ip = str(args.laptop_ip), str(args.desktop_ip)
    if not 1<=args.frames<=36000 or not 1<=args.fps<=60:
        raise ValueError('Frames must be 1-36000 and fps must be 1-60')
    if args.decoder_adapter is not None and not 0<=args.decoder_adapter<=255:
        raise ValueError('Decoder adapter must be 0-255')
    folder = ROOT / 'out/build/windows-release'
    session, video = folder / 'dm-session.exe', folder / 'dm-view.exe'
    peer = WindowsPeer(laptop_ip, args.user)
    name = 'video-test-' + uuid.uuid4().hex
    secret = secrets.token_hex(32) + '\n'
    remote_session = None
    results = {}
    host = None
    pattern = None
    exercise=None
    pool=None
    baseline = subprocess.check_output([str(folder/'dm-display.exe'),'list'],text=True) if args.extend else None
    try:
        remote_session = peer.upload(session)
        remote_video = peer.upload(video)
        remote_drive=peer.upload(folder/'dm-input-drive.exe') if args.exercise_input else None
        result = subprocess.run([str(session), 'pair', '--name', name, '--secret-stdin'], input=secret,
                                capture_output=True, text=True, timeout=5)
        if result.returncode:
            raise RuntimeError('Local pairing failed: ' + result.stderr)
        peer.run(f'$secret | & {quote(remote_session)} pair --name {name} --secret-stdin', secret=secret, interactive=True)
        _, results['laptop_hardware'], _ = peer.run(f'& {quote(remote_video)} info', interactive=True)
        print(results['laptop_hardware'], flush=True)
        with TestFirewall(peer, desktop_ip, video, remote_video, 43824):
            host = subprocess.Popen([str(video), 'host', '--name', name, '--bind', desktop_ip,
                                     '--allow-screen', '--codec', args.codec, '--frames', str(args.frames), '--fps', str(args.fps), '--width',str(args.width),'--height',str(args.height), '--timeout-ms', '30000']+(['--display',args.display] if args.display else [])+(['--extend'] if args.extend else [])+(['--allow-input'] if args.control else []),
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            ready = host.stdout.readline()
            if not ready.startswith('LISTENING '):
                raise RuntimeError('Video host startup failed: ' + ready)
            if args.show_pattern:
                pattern=subprocess.Popen([str(folder/'dm-display.exe'),'pattern',str(min(600,args.frames//args.fps+5))],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            command = f'& {quote(remote_video)} receive --name {name} --bind {laptop_ip} --peer {desktop_ip}'
            if args.headless:
                command += ' --headless'
            if args.decoder_adapter is not None:
                command += f' --decoder-adapter {args.decoder_adapter}'
            if args.control: command+=' --control'
            if args.fullscreen: command+=' --fullscreen'
            if remote_drive:
                pool=ThreadPoolExecutor(max_workers=1)
                exercise=pool.submit(peer.run,f'& {quote(remote_drive)}',interactive=True)
            receiver_code, receiver_out, receiver_error = peer.run(command, interactive=True,required=False,timeout=max(45,(args.frames+args.fps-1)//args.fps+45))
            host_out, host_error = host.communicate(timeout=15)
            if exercise:
                _,results['receiver_exercise'],_=exercise.result(timeout=5)
            if host.returncode or receiver_code or f'VIDEO_SENT frames={args.frames} ' not in host_out or f'VIDEO_RECEIVED decoded_frames={args.frames} ' not in receiver_out:
                raise RuntimeError(f'Video test failed: {host_out} {host_error} {receiver_out} {receiver_error}')
            if args.codec=='h264' and (f'gpu_surface_frames={args.frames} ' not in receiver_out or 'hardware-MFT' not in host_out):
                raise RuntimeError('Hardware encode / GPU-surface decode was not confirmed')
            if args.codec=='h264' and ('D3D11-NV12-surface-input' not in host_out or 'raw_pixel_readbacks=0' not in host_out or 'raw_pixel_readbacks=0' not in receiver_out):
                raise RuntimeError('GPU-resident raw-pixel path was not confirmed')
            if not args.headless and 'PREVIEW_PRESENTED frames=' not in receiver_out:
                raise RuntimeError('Native preview did not confirm presentation')
            if args.codec=='h264' and not args.headless and 'cpu_pixel_uploads=0' not in receiver_out:
                raise RuntimeError('Preview unexpectedly uploaded raw CPU pixels')
            results['host'] = host_out.strip()
            if args.control and ('INPUT_GRANTED caps=' not in host_out or 'INPUT_CAPTURE caps=' not in receiver_out): raise RuntimeError('Input permission negotiation was not confirmed')
            results['receiver'] = receiver_out
            results['mode'] = 'headless decode' if args.headless else 'native D3D11 preview'
            print('Host:', host_out.strip(), flush=True)
            print('Receiver:', receiver_out, flush=True)
            if args.extend:
                import time
                until=time.monotonic()+8
                while time.monotonic()<until:
                    after=subprocess.check_output([str(folder/'dm-display.exe'),'list'],text=True)
                    if after==baseline: break
                    time.sleep(0.1)
                if after!=baseline: raise RuntimeError('Physical display baseline was not restored')
                results['topology_restored']=True
    finally:
        if pattern:
            try: pattern_out,pattern_err=pattern.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                pattern.terminate();pattern_out,pattern_err=pattern.communicate(timeout=4)
            results['input_observed']=pattern_out.strip()
        if host and host.poll() is None:
            host.kill()
            host.communicate(timeout=4)
        subprocess.run([str(session), 'revoke', '--name', name], capture_output=True, timeout=5)
        try:
            if remote_session:
                peer.run(f'& {quote(remote_session)} revoke --name {name}', interactive=True)
        finally:
            if pool: pool.shutdown(wait=False,cancel_futures=True)
            peer.close()
            del secret
    if args.exercise_input:
        import re
        observed=dict((name,int(value)) for name,value in re.findall(r'(mouse|chars|touch|pen|pressure_max)=(\d+)',results.get('input_observed','')))
        if not (observed.get('mouse',0)>=1 and observed.get('chars',0)>=1 and observed.get('touch',0)>=2 and observed.get('pen',0)>=1 and observed.get('pressure_max',0)>=750):
            raise RuntimeError('Receiver input did not reach the host diagnostic app: '+repr(results))
        print('Native receiver-to-host input verified:',results['input_observed'],flush=True)
    results['scope'] = f'TLS1.3 {args.codec} prototype; GPU-resident raw pixels; input permitted={args.control}; audio absent; generated input is not a physical hardware test'
    results['source'] = 'session-owned DM virtual display' if args.extend else (args.display or 'primary DXGI')
    suffix='' if args.decoder_adapter is None else f'-adapter-{args.decoder_adapter}'
    if args.display: suffix+='-selected-display'
    if args.extend: suffix+=f'-extend-{args.frames}frames-{args.fps}fps'
    suffix+=f'-{args.width}x{args.height}'
    if args.control: suffix+='-input'
    (ROOT / f'out/laptop-{args.codec}-gpu{suffix}-test.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('Video laptop check passed; disposable credentials revoked', flush=True)


if __name__ == '__main__':
    main()
