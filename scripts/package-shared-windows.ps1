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
$uiPath = [IO.Path]::GetFullPath((Join-Path $projectRoot $UiBuild))
$enginePath = [IO.Path]::GetFullPath((Join-Path $projectRoot $EngineBuild))
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
# Keep target/output pairs together: every native executable copied below must
# first pass through its dependency graph, including rarely used recovery tools.
# Timestamps alone miss relinks after static-library or generated-payload changes.
$nativeOutputs = [ordered]@{
    'dm-view' = 'dm-view.exe'
    'ExtraPane.Service' = 'ExtraPane.Service.exe'
    'ExtraPane.Session' = 'ExtraPane.Session.exe'
}
if ($IncludeCamera) {
    $nativeOutputs['extrapane-resources'] = 'ExtraPane.Resources.exe'
    $nativeOutputs['extrapane_camera_setup'] = 'ExtraPane.CameraSetup.exe'
}
if ($IncludeMicrophone) {
    $nativeOutputs['ExtraPane.Microphone'] = 'ExtraPane.Microphone.exe'
    $nativeOutputs['extrapane_usb_setup'] = 'ExtraPane.UsbSetup.exe'
    $endpoint = Join-Path $projectRoot $MicrophoneEndpoint
    if (-not (Test-Path -LiteralPath $endpoint)) { throw 'Build the pinned microphone endpoint helper before packaging.' }
}
if ($IncludeUsb) {
    $nativeOutputs['extrapane_usb'] = 'ExtraPane.Usb.exe'
    $nativeOutputs['extrapane_usb_host_setup'] = 'ExtraPane.UsbHostSetup.exe'
    $nativeOutputs['extrapane_usb_recovery_tool'] = 'ExtraPane.UsbRecovery.exe'
    $nativeOutputs['extrapane_usb_setup'] = 'ExtraPane.UsbSetup.exe'
    $usbSource = Join-Path $projectRoot 'out/usb-host/corresponding-source'
    foreach ($required in @('usbipd-upstream.tar','usbipd-loopback.patch','build-usb-host.ps1','BUILD.json','COPYING.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $usbSource $required))) { throw "Missing USB host corresponding source: $required" }
    }
}
function Get-ConfiguredCMake([string]$BuildPath, [string]$ExpectedSource) {
    if (-not $BuildPath.StartsWith(($projectRoot + '\out\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build directories must be inside this checkout out directory.'
    }
    $cache = Join-Path $BuildPath 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) { throw "Configure the build before packaging: $BuildPath" }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $cache) {
        if ($line -match '^(CMAKE_COMMAND|CMAKE_HOME_DIRECTORY):INTERNAL=(.*)$') { $values[$Matches[1]] = $Matches[2] }
    }
    if (-not $values.ContainsKey('CMAKE_HOME_DIRECTORY') -or
        [IO.Path]::GetFullPath($values['CMAKE_HOME_DIRECTORY']) -ne [IO.Path]::GetFullPath($ExpectedSource)) {
        throw "Build directory belongs to another source tree: $BuildPath"
    }
    if (-not $values.ContainsKey('CMAKE_COMMAND') -or -not (Test-Path -LiteralPath $values['CMAKE_COMMAND'] -PathType Leaf)) {
        throw "Configured CMake executable is unavailable: $BuildPath"
    }
    return $values['CMAKE_COMMAND']
}
$nativeCMake = Get-ConfiguredCMake $enginePath $projectRoot
$uiCMake = Get-ConfiguredCMake $uiPath (Join-Path $projectRoot 'apps/shared')
$devShell = Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1'
if (-not (Test-Path -LiteralPath $devShell)) { throw 'Visual Studio developer environment is unavailable.' }
& $devShell -VsInstallationPath $vsPath -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo | Out-Null
Write-Host 'Updating every included native helper and the shared UI before staging...'
& $nativeCMake --build $enginePath --target @($nativeOutputs.Keys) --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native package build failed; nothing was staged.' }
& $uiCMake --build $uiPath --target ExtraPane --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Shared UI package build failed; nothing was staged.' }
$files = @((Join-Path $uiPath 'ExtraPane.exe'))
foreach ($output in $nativeOutputs.Values) { $files += (Join-Path $enginePath $output) }
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
$revision = & git -C $projectRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record the development build source revision.' }
$changes = & git -C $projectRoot status --porcelain
@{
    project = 'ExtraPane'
    kind = 'development-resource-build'
    sourceRevision = $revision.Trim()
    workingTreeChanges = [bool]$changes
    stagedUtc = [DateTime]::UtcNow.ToString('o')
    camera = [bool]$IncludeCamera
    microphone = [bool]$IncludeMicrophone
    usbStorage = [bool]$IncludeUsb
    deviceVerified = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destinationPath 'BUILD-INFO.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/WINDOWS-RESOURCE-TEST.md') -Destination (Join-Path $destinationPath 'TEST-GUIDE.md')
Write-Host "Staged development resource-sharing app: $destinationPath"
Write-Host 'No app was launched, no driver installed, and no connection was attempted.'
