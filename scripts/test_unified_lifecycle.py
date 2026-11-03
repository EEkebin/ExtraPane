"""Checks privileged-helper lifetime and rejection without altering pairing."""
from pathlib import Path
import subprocess,time,json
root=Path(__file__).resolve().parent.parent
app=root/'out/winui/ExtraPane/ExtraPane.exe'
helper=app.parent/'ExtraPane.Service.exe'
ps=lambda command: subprocess.check_output(['powershell','-NoProfile','-Command',command],text=True).strip()
# A helper must refuse an unrelated launcher before creating a firewall rule.
import os
bad=subprocess.run([str(helper),'--pipe',r'\\.\pipe\ExtraPane-invalid-test','--parent-pid',str(os.getpid())],timeout=10)
assert bad.returncode!=0
(app.parent/'server-session-check.log').unlink(missing_ok=True)
p=subprocess.Popen([str(app),'--server','--start','--test-report','--test-duration','30'])
try:
 until=time.monotonic()+15
 log=app.parent/'server-session-check.log'
 while time.monotonic()<until:
  if log.exists() and 'LISTENING' in log.read_text():break
  time.sleep(.1)
 else:raise RuntimeError('Server did not start')
 time.sleep(.5)
 service=ps("Get-CimInstance Win32_Process -Filter \"Name='ExtraPane.Service.exe'\" | Select-Object -ExpandProperty ProcessId")
 assert service
 p.terminate();p.wait(timeout=5)
 until=time.monotonic()+15
 while time.monotonic()<until:
  alive=ps(f"@(Get-Process -Id {service} -ErrorAction SilentlyContinue).Count")
  if alive=='0':break
  time.sleep(.2)
 else:raise RuntimeError('Helper survived launcher failure')
 rules=ps("@(Get-NetFirewallRule -DisplayName 'DM Host LAN display' -ErrorAction SilentlyContinue).Count")
 assert rules=='0',rules
 print('UNIFIED_LIFECYCLE passed: wrong parent rejected; launcher death stopped helper and removed firewall')
finally:
 if p.poll() is None:p.terminate();p.wait()
