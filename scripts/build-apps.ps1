param([switch]$SkipEngine)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $SkipEngine){ & "$PSScriptRoot/build.ps1" -Configuration Release }
$vsRoot = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -property installationPath
$vsMajor=[int]((& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products * -property installationVersion).Split('.')[0])
$toolset=if($vsMajor -ge 18){'v145'}else{'v143'}
foreach($role in @('Client','Server')){
 & "$vsRoot/MSBuild/Current/Bin/MSBuild.exe" "$root/apps/winui/DM.vcxproj" /restore /p:RestoreLockedMode=true /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$toolset" "/p:DMRole=$role" /v:minimal /nologo
 if($LASTEXITCODE -ne 0){throw "WinUI $role build failed"}
 Copy-Item -LiteralPath "$root/assets/branding/SideScreen.ico" -Destination "$root/out/winui/$role/SideScreen.ico" -Force
 Copy-Item -LiteralPath "$root/out/build/windows-release/dm-view.exe" -Destination "$root/out/winui/$role/dm-view.exe" -Force
 if($role -eq 'Server'){Copy-Item -LiteralPath "$root/out/build/windows-release/dm-display.exe" -Destination "$root/out/winui/$role/dm-display.exe" -Force}
}
