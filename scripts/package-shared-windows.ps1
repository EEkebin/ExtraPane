param(
    [string]$UiBuild = 'out/shared-ui',
    [string]$EngineBuild = 'out/shared-engine',
    [string]$QtRoot = 'out/qt-sdk/6.8.3/msvc2022_64',
    [string]$Destination = 'out/shared-portable',
    [switch]$IncludeCamera,
    [switch]$IncludeMicrophone,
    [switch]$IncludeUsb,
    [string]$MicrophoneEndpoint = 'out/microphone-endpoint/release/extrapane-microphone-endpoint.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$destinationPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $Destination))
if (-not $destinationPath.StartsWith(($projectRoot + '\out\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Staging must be inside this checkout out directory.'
}
if (Test-Path -LiteralPath $destinationPath) { throw 'Use a new staging directory to avoid mixing package versions.' }
$uiPath = Join-Path $projectRoot $UiBuild
$enginePath = Join-Path $projectRoot $EngineBuild
$qtPath = Join-Path $projectRoot $QtRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio runtime discovery is unavailable.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'No MSVC installation found for runtime deployment.' }
$redistRoot = Join-Path $vsPath 'VC/Redist/MSVC'
$redistVersion = Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $redistVersion) { throw 'MSVC redistributable files are missing.' }
$crt = Get-ChildItem -LiteralPath (Join-Path $redistVersion.FullName 'x64') -Directory | Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } | Select-Object -First 1
if (-not $crt) { throw 'MSVC x64 runtime files are missing.' }
$files = @((Join-Path $uiPath 'ExtraPane.exe'), (Join-Path $enginePath 'dm-view.exe'), (Join-Path $enginePath 'ExtraPane.Service.exe'))
if ($IncludeCamera) {
    $files += (Join-Path $enginePath 'ExtraPane.Resources.exe'), (Join-Path $enginePath 'ExtraPane.CameraSetup.exe')
}
if ($IncludeMicrophone) {
    $files += (Join-Path $enginePath 'ExtraPane.Microphone.exe'), (Join-Path $enginePath 'ExtraPane.UsbSetup.exe')
    $endpoint = Join-Path $projectRoot $MicrophoneEndpoint
    if (-not (Test-Path -LiteralPath $endpoint)) { throw 'Build the pinned microphone endpoint helper before packaging.' }
}
if ($IncludeUsb) {
    $files += (Join-Path $enginePath 'ExtraPane.Usb.exe'), (Join-Path $enginePath 'ExtraPane.UsbHostSetup.exe'), (Join-Path $enginePath 'ExtraPane.UsbRecovery.exe')
    if (-not $IncludeMicrophone) { $files += (Join-Path $enginePath 'ExtraPane.UsbSetup.exe') }
    $usbSource = Join-Path $projectRoot 'out/usb-host/corresponding-source'
    foreach ($required in @('usbipd-upstream.tar','usbipd-loopback.patch','build-usb-host.ps1','BUILD.json','COPYING.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $usbSource $required))) { throw "Missing USB host corresponding source: $required" }
    }
}
foreach ($file in $files) { if (-not (Test-Path -LiteralPath $file)) { throw "Missing build output: $file" } }
New-Item -ItemType Directory -Path $destinationPath | Out-Null
foreach ($file in $files) { Copy-Item -LiteralPath $file -Destination $destinationPath }
if ($IncludeMicrophone) {
    Copy-Item -LiteralPath $endpoint -Destination (Join-Path $destinationPath 'ExtraPane.MicrophoneEndpoint.exe')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'apps/microphone-endpoint/USBIP-AUDIO-LICENSE.txt') -Destination $destinationPath
    Copy-Item -LiteralPath (Join-Path $projectRoot 'apps/usb-setup/USBIP-WIN2-LICENSE.txt') -Destination $destinationPath
}
if ($IncludeUsb) {
    Copy-Item -LiteralPath $usbSource -Destination (Join-Path $destinationPath 'usb-host-source') -Recurse
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/USB.md') -Destination (Join-Path $destinationPath 'USB-RECOVERY.md')
    if (-not $IncludeMicrophone) { Copy-Item -LiteralPath (Join-Path $projectRoot 'apps/usb-setup/USBIP-WIN2-LICENSE.txt') -Destination $destinationPath }
}
& (Join-Path $qtPath 'bin/windeployqt.exe') --release --no-translations --verbose 0 --qmldir (Join-Path $projectRoot 'apps/shared/ui') (Join-Path $destinationPath 'ExtraPane.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $destinationPath }
# windeployqt includes the interactive platform plugin only. The isolated
# package check explicitly selects offscreen and must not open an error dialog.
Copy-Item -LiteralPath (Join-Path $qtPath 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $destinationPath 'platforms/qoffscreen.dll')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/RESOURCE-SHARING.md') -Destination (Join-Path $destinationPath 'DEVELOPMENT-STATUS.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/IMPLEMENTATION-CHECKLIST.md') -Destination (Join-Path $destinationPath 'IMPLEMENTATION-CHECKLIST.md')
Write-Host "Staged development resource-sharing app: $destinationPath"
Write-Host 'No app was launched, no driver installed, and no connection was attempted.'
