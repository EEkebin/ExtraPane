"""Developer-only deployment to the authorized test laptop. SSH is not used by the apps."""
import secrets,subprocess,os,shutil,winreg,time,json,argparse,ipaddress
from pathlib import Path
from windows_peer import WindowsPeer,ROOT,quote,local_ps
folder=ROOT/'out/build/windows-release'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--laptop-ip',default='10.0.20.119',type=ipaddress.IPv4Address)
parser.add_argument('--desktop-ip',default='10.0.0.210',type=ipaddress.IPv4Address)
parser.add_argument('--user',default='kebinBook')
args=parser.parse_args()
args.laptop_ip=str(args.laptop_ip);args.desktop_ip=str(args.desktop_ip)
peer=WindowsPeer(args.laptop_ip,args.user)
try:
 remote_session=peer.upload(folder/'dm-session.exe')
 local_peers=subprocess.check_output([str(folder/'dm-session.exe'),'peers'],text=True).splitlines()
 _,remote_peers,_=peer.run(f'& {quote(remote_session)} peers',interactive=True)
 if ('dm-lan' in local_peers)!=('dm-lan' in remote_peers.splitlines()):raise RuntimeError('Pair exists on only one computer; preserve it for explicit review')
 if 'dm-lan' not in local_peers:
  secret=secrets.token_hex(32)+'\n'
  local=subprocess.run([str(folder/'dm-session.exe'),'pair','--name','dm-lan','--secret-stdin'],input=secret,text=True,capture_output=True,check=True)
  try:peer.run(f'$secret | & {quote(remote_session)} pair --name dm-lan --secret-stdin',secret=secret,interactive=True)
  except:
   subprocess.run([str(folder/'dm-session.exe'),'revoke','--name','dm-lan'],capture_output=True);raise
  finally:del secret
  print('Persistent app pairing stored in Credential Manager on both computers',flush=True)
 dest=Path(os.environ['LOCALAPPDATA'])/'DM/app';dest.mkdir(parents=True,exist_ok=True)
 for name in ['dm-host.exe','dm-view.exe','dm-display.exe']:
  shutil.copy2(folder/name,dest/name)
 with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Host') as key:
  for name,value in [('Bind',args.desktop_ip),('Peer',args.laptop_ip),('Resolution','0'),('Fps','60')]:winreg.SetValueEx(key,name,0,winreg.REG_SZ,value)
 local_ps(f"$shell=New-Object -ComObject WScript.Shell; $shortcut=$shell.CreateShortcut([IO.Path]::Combine([Environment]::GetFolderPath('Desktop'),'DM Host.lnk')); $shortcut.TargetPath={quote(dest/'dm-host.exe')}; $shortcut.WorkingDirectory={quote(dest)}; $shortcut.Save()")
 for name in ['dm-receiver.exe','dm-view.exe']:
  path=peer.upload(folder/name)
  peer.run(f"$app=Join-Path $env:LOCALAPPDATA 'DM\\app'; New-Item -ItemType Directory -Force -Path $app | Out-Null; Copy-Item -LiteralPath {quote(path)} -Destination $app -Force")
 peer.run(r"""$app=Join-Path $env:LOCALAPPDATA 'DM\app'; $settings='HKCU:\Software\DM\Receiver'; New-Item $settings -Force | Out-Null; New-ItemProperty $settings Bind -Value '10.0.20.119' -PropertyType String -Force | Out-Null; New-ItemProperty $settings Peer -Value '10.0.0.210' -PropertyType String -Force | Out-Null; $shell=New-Object -ComObject WScript.Shell; $shortcut=$shell.CreateShortcut([IO.Path]::Combine([Environment]::GetFolderPath('Desktop'),'DM Receiver.lnk')); $shortcut.TargetPath=Join-Path $app 'dm-receiver.exe'; $shortcut.WorkingDirectory=$app; $shortcut.Save(); $test=Start-Process -FilePath (Join-Path $app 'dm-receiver.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru; if(-not $test.WaitForExit(10000) -or $test.ExitCode -ne 0){throw 'Receiver window state check failed'}; 'Receiver UI state passed'""".replace('10.0.20.119',str(args.laptop_ip)).replace('10.0.0.210',str(args.desktop_ip)),interactive=True)
 print('Both shortcuts installed; laptop UI state passed',flush=True)
 host=subprocess.Popen([str(dest/'dm-host.exe'),'--start'])
 time.sleep(2)
 _,remote_pid,_=peer.run("$app=Join-Path $env:LOCALAPPDATA 'DM\\app\\dm-receiver.exe'; $p=Start-Process -FilePath $app -ArgumentList '--start' -WindowStyle Hidden -PassThru; $p.Id",interactive=True)
 remote_pid=int(remote_pid.strip())
 print('Continuous GUI session started, with Debug off and no pattern window',flush=True)
 report={'receiver_pid':remote_pid,'host_pid':host.pid,'checks':[]}
 try:
  for delay in [30,50,55]:
   time.sleep(delay)
   topology=subprocess.check_output([str(folder/'dm-display.exe'),'list'],text=True)
   _,state,_=peer.run("Get-Process dm-receiver,dm-view -ErrorAction Stop | Select-Object Name,Id,WorkingSet64,MainWindowTitle | ConvertTo-Json -Compress",interactive=True)
   local_state=local_ps("Get-CimInstance Win32_Process -Filter \"Name = 'dm-view.exe'\" | Select-Object ProcessId,ParentProcessId,CommandLine | ConvertTo-Json -Compress")
   assert 'dm_virtual=1' in topology and any('--frames 0' in proc['CommandLine'].replace('"','') for proc in json.loads(local_state)),(topology,local_state)
   report['checks'].append({'topology':topology,'receiver_processes':json.loads(state),'host_processes':json.loads(local_state)})
   print('Continuous session remains connected; elapsed >= '+str(sum([30,50,55][:len(report['checks'])]))+' seconds',flush=True)
 finally:
  peer.run(f'Stop-Process -Id {remote_pid} -ErrorAction SilentlyContinue',interactive=True)
  time.sleep(3)
  host.terminate();host.wait(timeout=10)
  # A deliberate launcher-crash test can leave its narrowly scoped firewall rule.
  local_ps("$rule=Get-NetFirewallRule -Name 'DM Host LAN display' -ErrorAction SilentlyContinue; if($rule){$rule | Remove-NetFirewallRule}; exit 0")
  report['after']=subprocess.check_output([str(folder/'dm-display.exe'),'list'],text=True)
  assert 'dm_virtual=1' not in report['after']
  (ROOT/'out/gui-continuous-laptop.json').write_text(json.dumps(report,indent=2))
 print('GUI continuous session and launcher-loss cleanup passed; app pairing retained',flush=True)
finally:peer.close()
