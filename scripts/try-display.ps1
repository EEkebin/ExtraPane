param([ValidateRange(1,60)][int]$Fps = 60,[switch]$Control,[ValidateRange(10,300)][int]$Seconds=120)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$identity = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this script from an administrator PowerShell on the desktop.' }
if (Get-Process dm-view -ErrorAction SilentlyContinue) { throw 'A DM preview is already running. Wait for it to finish or close the laptop preview before starting another.' }
$frames = $Seconds * $Fps
Write-Host "Starts a $seconds-second third-monitor preview on the Surface Book 2 (3000x2000, requested $Fps fps)."
Write-Host 'Keep the laptop signed in and unlocked. On the desktop, use a spare window and press Win+Shift+Right once or twice to move it onto the laptop display.'
if ($Control) { Write-Host 'INPUT ENABLED: click/tap the laptop preview, then draw or type. F8 pauses/resumes forwarding. Focus loss releases input. F9 closes the fullscreen preview. Audio is not implemented.' }
else { Write-Host 'View only. Add -Control to explicitly enable laptop mouse, keyboard, touch and pen forwarding.' }
Read-Host 'Press Enter when ready to watch the laptop' | Out-Null
$extra = @()
if ($Control) { $extra += '--control' }
& python (Join-Path $PSScriptRoot 'test_video_laptop.py') --laptop-ip 10.0.20.119 --desktop-ip 10.0.0.210 --user kebinBook --frames $frames --fps $Fps --width 3000 --height 2000 --fullscreen --decoder-adapter 0 --extend --show-pattern @extra
if ($LASTEXITCODE -ne 0) { throw 'The bounded test ended early or failed. See the messages above; closing the preview early also ends the test.' }
