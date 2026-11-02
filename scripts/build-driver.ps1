$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& python (Join-Path $PSScriptRoot 'restore-driver-tools.py')
if ($LASTEXITCODE -ne 0) { throw 'Driver tool restore failed.' }
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools are required.' }
Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild (Join-Path $projectRoot 'drivers\idd\DMIndirectDisplay.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Driver build failed.' }
Write-Host 'Built an unsigned prototype. No driver, certificate, or boot setting was installed or changed.'
