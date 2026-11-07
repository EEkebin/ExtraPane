param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [string]$Version,
    [string]$Compiler,
    [string]$Destination='out/shared-installer'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$stagePath=[IO.Path]::GetFullPath((Join-Path $projectRoot $Stage))
$destinationPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $Destination))
foreach($path in @($stagePath,$destinationPath)) {
    if(-not $path.StartsWith(($projectRoot+'\out\'),[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Development installer paths must stay under this checkout out directory.'
    }
}
foreach($name in @('ExtraPane.exe','ExtraPane.DriverSetup.exe','ExtraPane.CameraSetup.exe','ExtraPane.UsbSetup.exe','ExtraPane.MicrophoneEndpoint.exe','ExtraPane.Usb.exe','ExtraPane.Session.exe')) {
    if(-not (Test-Path -LiteralPath (Join-Path $stagePath $name))) {throw "Missing staged output: $name"}
}
if(-not $Compiler) {$Compiler=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'}
if(-not (Test-Path -LiteralPath $Compiler)) {throw 'An Inno Setup 6 compiler is required. Pass -Compiler for an isolated toolchain.'}
if(-not $Version) {
    $Version=& python (Join-Path $PSScriptRoot 'release_version.py')
    if($LASTEXITCODE) {throw 'Release version resolution failed'}
}
$names=@('EXTRAPANE_ROLE','EXTRAPANE_VERSION','EXTRAPANE_NUMERIC_VERSION','EXTRAPANE_STAGE','EXTRAPANE_DIST')
$previous=@{}
foreach($name in $names){$previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    $env:EXTRAPANE_ROLE='ExtraPane'
    $env:EXTRAPANE_VERSION=$Version
    $env:EXTRAPANE_NUMERIC_VERSION=& python (Join-Path $PSScriptRoot 'release_version.py') --numeric
    if($LASTEXITCODE) {throw 'Invalid installer version'}
    $env:EXTRAPANE_STAGE=$stagePath
    $env:EXTRAPANE_DIST=$destinationPath
    & (Join-Path $stagePath 'ExtraPane.CameraSetup.exe') --check-package
    if($LASTEXITCODE) {throw 'Embedded camera package validation failed'}
    $readme=Join-Path $stagePath 'README.txt'
    if(-not (Test-Path -LiteralPath $readme)) {
        @'
ExtraPane shared-resource development build

Client only installs the per-user app without machine component changes.
Server + Client installs the same app, virtual-display driver, camera source,
and microphone/USB receiving components.
Camera setup requires administrator permission. Camera use remains subject to
Windows camera privacy settings. No physical camera is detached or replaced.

Server + Client also requires the virtual-display driver installer. That choice
requires a package containing DriverSetup; otherwise setup refuses that role.

The camera component stays installed when its server stops. App uninstall keeps
the shared camera component unless you explicitly select its removal, which
affects every user on this PC. Loaded camera files may require a Windows restart
to finish removal. See DEVELOPMENT-STATUS.md for remaining verification limits.
'@ | Set-Content -LiteralPath $readme -Encoding utf8
    }
    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
    & $Compiler (Join-Path $projectRoot 'packaging/ExtraPane.iss')
    if($LASTEXITCODE) {throw 'Shared app installer compilation failed'}
    Write-Host "Built development installer in $destinationPath"
    Write-Host 'No installer, camera registration, driver setup or app connection was run.'
} finally {
    foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$previous[$name],'Process')}
}
