param([string]$Version=$env:EXTRAPANE_VERSION,[string]$Compiler,[string]$Stage)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$Version){$Version=& python "$PSScriptRoot/release_version.py"}
$env:EXTRAPANE_VERSION=$Version
$env:EXTRAPANE_NUMERIC_VERSION=& python "$PSScriptRoot/release_version.py" --numeric
if($LASTEXITCODE){throw 'Invalid release version'}
$env:EXTRAPANE_DRIVER_PACKAGE=Join-Path $root 'out/driver-package'
if(!(Test-Path -LiteralPath (Join-Path $env:EXTRAPANE_DRIVER_PACKAGE 'manifest.json'))){throw 'Build and package the driver first'}
$env:EXTRAPANE_DRIVER_DIST=Join-Path $root 'out/driver-installer'
$iscc=$Compiler
if(!$iscc){$iscc=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'}
if(!(Test-Path -LiteralPath $iscc)){throw 'Inno Setup 6 is required; it is included on the GitHub Windows runner'}
& $iscc (Join-Path $root 'packaging/ExtraPaneDriver.iss')
if($LASTEXITCODE){throw 'Driver installer compilation failed'}
if($Stage){
 $stagePath=[IO.Path]::GetFullPath((Join-Path $root $Stage))
 if(!$stagePath.StartsWith($root+'\out\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $stagePath -PathType Container)){throw 'Stage must be an existing build directory under out'}
 Copy-Item -LiteralPath (Join-Path $env:EXTRAPANE_DRIVER_DIST 'ExtraPane.DriverSetup.exe') -Destination (Join-Path $stagePath 'ExtraPane.DriverSetup.exe') -Force
}
