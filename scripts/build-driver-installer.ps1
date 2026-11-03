param([string]$Version=$env:SIDESCREEN_VERSION)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$Version){$Version=& python "$PSScriptRoot/release_version.py"}
$env:SIDESCREEN_VERSION=$Version
$env:SIDESCREEN_NUMERIC_VERSION=& python "$PSScriptRoot/release_version.py" --numeric
if($LASTEXITCODE){throw 'Invalid release version'}
$env:SIDESCREEN_DRIVER_PACKAGE=Join-Path $root 'out/driver-package'
if(!(Test-Path -LiteralPath (Join-Path $env:SIDESCREEN_DRIVER_PACKAGE 'manifest.json'))){throw 'Build and package the driver first'}
$env:SIDESCREEN_DRIVER_DIST=Join-Path $root 'out/driver-installer'
$iscc=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
if(!(Test-Path -LiteralPath $iscc)){throw 'Inno Setup 6 is required; it is included on the GitHub Windows runner'}
& $iscc (Join-Path $root 'packaging/SideScreenDriver.iss')
if($LASTEXITCODE){throw 'Driver installer compilation failed'}
Copy-Item -LiteralPath (Join-Path $env:SIDESCREEN_DRIVER_DIST 'SideScreen.DriverSetup.exe') -Destination (Join-Path $root 'out/winui/SideScreen/SideScreen.DriverSetup.exe') -Force
