param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio Installer/vswhere was not found. Install Visual Studio C++ desktop tools.' }
$vsRoot = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio installation with x64 C++ tools was found.' }
$devShellModule = Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Install the Visual Studio C++ CMake tools component.' }
$preset = 'windows-' + $Configuration.ToLowerInvariant()
Push-Location $projectRoot
try {
    & $cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & $ctest --preset $preset
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
} finally {
    Pop-Location
}
