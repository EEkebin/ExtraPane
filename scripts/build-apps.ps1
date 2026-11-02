param([switch]$SkipEngine)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& python "$PSScriptRoot/release_version.py" --header "$root/out/generated/SideScreenVersion.h"
if($LASTEXITCODE -ne 0){throw 'Invalid build version'}
if(-not $SkipEngine){ & "$PSScriptRoot/build.ps1" -Configuration Release }
$vsRoot = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -property installationPath
$vsMajor=[int]((& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -property installationVersion).Split('.')[0])
$toolset=if($vsMajor -ge 18){'v145'}else{'v143'}
foreach($role in @('SideScreen')){
 & "$vsRoot/MSBuild/Current/Bin/MSBuild.exe" "$root/apps/winui/DM.vcxproj" /restore /p:RestoreLockedMode=true /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$toolset" "/p:DMRole=$role" /v:minimal /nologo
 if($LASTEXITCODE -ne 0){throw "WinUI $role build failed"}
 Copy-Item -LiteralPath "$root/assets/branding/SideScreen.ico" -Destination "$root/out/winui/$role/SideScreen.ico" -Force
 Copy-Item -LiteralPath "$root/out/build/windows-release/dm-view.exe" -Destination "$root/out/winui/$role/dm-view.exe" -Force
 foreach($helper in @('dm-display.exe','SideScreen.Service.exe')){Copy-Item -LiteralPath "$root/out/build/windows-release/$helper" -Destination "$root/out/winui/$role/$helper" -Force}
}

& python "$PSScriptRoot/release_version.py" | Set-Content "$root/out/winui/SideScreen/version.txt" -Encoding ascii
