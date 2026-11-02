param([switch]$SkipEngine)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $SkipEngine){ & "$PSScriptRoot/build.ps1" -Configuration Release }
$vsRoot = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -property installationPath
foreach($role in @('Client','Server')){
 & "$vsRoot/MSBuild/Current/Bin/MSBuild.exe" "$root/apps/winui/DM.vcxproj" /restore /p:RestoreLockedMode=true /p:Configuration=Release /p:Platform=x64 "/p:DMRole=$role" /v:minimal /nologo
 if($LASTEXITCODE -ne 0){throw "WinUI $role build failed"}
 Copy-Item -LiteralPath "$root/out/build/windows-release/dm-view.exe" -Destination "$root/out/winui/$role/dm-view.exe" -Force
 if($role -eq 'Server'){Copy-Item -LiteralPath "$root/out/build/windows-release/dm-display.exe" -Destination "$root/out/winui/$role/dm-display.exe" -Force}
}
