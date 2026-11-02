"""Exercise extracted WinUI packages on the authorized desktop and Surface Book."""
import subprocess,time,zipfile,json,winreg
from pathlib import Path
from windows_peer import ROOT,WindowsPeer,quote,local_ps
peer=WindowsPeer('10.0.20.119','kebinBook')
b=ROOT/'out/build/windows-release';stage=ROOT/'out/portable-check'
host=pattern=None;remote_pid=None
baseline=subprocess.check_output([str(b/'dm-display.exe'),'list'],text=True)
try:
 with winreg.OpenKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:saved=winreg.QueryValueEx(k,'Layout')
except FileNotFoundError:saved=None
try:
 with zipfile.ZipFile(ROOT/'out/dist/SideScreen-0.13.0-win-x64.zip') as z:z.extractall(stage)
 server=stage/'SideScreen/SideScreen.exe'
 subprocess.run([str(server),'--smoke-test'],check=True,timeout=20)
 archive=peer.upload(ROOT/'out/dist/SideScreen-0.13.0-win-x64.zip');drive=peer.upload(b/'dm-input-drive.exe');check=peer.upload(b/'dm-window-check.exe');tray_check=peer.upload(b/'dm-tray-check.exe')
 _,remote,_=peer.run(f"$root=Join-Path $env:LOCALAPPDATA 'DM\\portable-check'; New-Item -ItemType Directory -Path $root -Force | Out-Null; Expand-Archive -LiteralPath {quote(archive)} -DestinationPath $root -Force; Join-Path $root 'SideScreen'")
 client=remote+'\\SideScreen.exe'
 peer.run(f"$p=Start-Process -FilePath {quote(client)} -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru; if(-not $p.WaitForExit(20000) -or $p.ExitCode -ne 0){{throw 'Portable Client render check failed'}}; 'LAPTOP_WINUI_RENDER passed'",interactive=True)
 print('Extracted Server and laptop Client UI checks passed',flush=True)
 with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:
  try:winreg.DeleteValue(k,'Layout')
  except FileNotFoundError:pass
 host=subprocess.Popen([str(server),'--server','--start','--test-report','--test-duration','65'])
 time.sleep(2)
 subprocess.run([str(b/'dm-tray-check.exe'),str(host.pid),'server','--keep-running'],check=True)
 _,pid,_=peer.run(f"$p=Start-Process -FilePath {quote(client)} -ArgumentList '--client --start --test-report --test-duration 55' -WindowStyle Hidden -PassThru; $p.Id",interactive=True)
 remote_pid=int(pid.strip())
 log=server.parent/'server-session-check.log';until=time.monotonic()+35
 while time.monotonic()<until:
  text=log.read_text() if log.exists() else ''
  if 'VIDEO_STARTED' in text:break
  time.sleep(.25)
 else:raise RuntimeError('GUI connection failed: '+text)
 assert 'requested=3000x2000@60 selected=3000x2000@60 fallback=0' in text,text
 assert 'VIDEO_STARTED 3000x2000 codec=H264 fps=60' in text,text
 print('GUI client negotiated native 3000x2000 at 60 fps',flush=True)
 pattern=subprocess.Popen([str(b/'dm-display.exe'),'pattern','90'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 _,window_result,_=peer.run(f"$engine=Get-CimInstance Win32_Process | Where-Object {{ $_.Name -eq 'dm-view.exe' -and $_.ExecutablePath -eq {quote(remote+'\\dm-view.exe')} }}; if(@($engine).Count -ne 1){{throw 'Expected one client engine'}}; & {quote(check)} $engine.ProcessId",interactive=True)
 print(window_result,flush=True)
 _,tray_result,_=peer.run(f'& {quote(tray_check)} {remote_pid} client --keep-running',interactive=True)
 print(tray_result,flush=True)
 _,input_result,_=peer.run(f'& {quote(drive)}',interactive=True)
 _,shortcut_result,_=peer.run(f"$engine=Get-CimInstance Win32_Process | Where-Object {{ $_.Name -eq 'dm-view.exe' -and $_.ExecutablePath -eq {quote(remote+'\\dm-view.exe')} }}; & {quote(check)} $engine.ProcessId --disconnect",interactive=True)
 print(shortcut_result,flush=True)
 time.sleep(2)
 assert host.poll() is None
 assert 'dm_virtual=1' in subprocess.check_output([str(b/'dm-display.exe'),'list'],text=True)
 assert 'DRIVER_WAITING' in log.read_text()

 host.wait(timeout=75)
 observed=pattern.communicate(timeout=10)[0]
 text=log.read_text()
 _,receiver,_=peer.run(f'Get-Content -Raw -LiteralPath {quote(remote+"\\client-session-check.log")}')
 assert all(t in observed for t in ['mouse=1','chars=1','touch=2','pen=1','pressure_max=897']),observed
 report={'server':text,'client':receiver,'window_check':window_result,'tray_check':tray_result,'disconnect_shortcut':shortcut_result,'input':observed,'portable_ui_render':True}
 (ROOT/'out/portable-laptop-test.json').write_text(json.dumps(report,indent=2))
 print(observed,flush=True);print('PORTABLE_LAPTOP passed',flush=True)
finally:
 if host and host.poll() is None:host.terminate();host.wait(timeout=8)
 if pattern and pattern.poll() is None:pattern.terminate();pattern.wait(timeout=8)
 if remote_pid:peer.run(f'Stop-Process -Id {remote_pid} -ErrorAction SilentlyContinue',required=False)
 peer.close()
 # A failed test may terminate the GUI before its normal scoped-rule cleanup.
 local_ps(f"$rule=Get-NetFirewallRule -DisplayName 'DM Host LAN display' -ErrorAction SilentlyContinue; if($rule){{$app=$rule | Get-NetFirewallApplicationFilter;if($app.Program -eq {quote(stage/'SideScreen/dm-view.exe')}){{$rule | Remove-NetFirewallRule -ErrorAction Stop}}}}; exit 0")
 with winreg.CreateKey(winreg.HKEY_CURRENT_USER,r'Software\DM\Display') as k:
  if saved:winreg.SetValueEx(k,'Layout',0,saved[1],saved[0])
  else:
   try:winreg.DeleteValue(k,'Layout')
   except FileNotFoundError:pass
 until=time.monotonic()+8
 while time.monotonic()<until:
  try:
   after=subprocess.check_output([str(b/'dm-display.exe'),'list'],text=True,stderr=subprocess.DEVNULL)
   if after==baseline:break
  except subprocess.CalledProcessError:pass
  time.sleep(.1)
 assert after==baseline,(after,baseline)
