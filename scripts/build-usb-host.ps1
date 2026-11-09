param(
    [string]$Repository = 'https://github.com/dorssel/usbipd-win.git',
    [string]$Destination = 'out/usb-host',
    [string]$Dotnet = 'dotnet',
    [switch]$Build,
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pin = '4431eba52a2de9f21e3a4ba1f7dc1e408cd57019'
$patch = Join-Path $root 'patches/usbipd-loopback.patch'
$outRoot = [IO.Path]::GetFullPath((Join-Path $root 'out'))
$target = [IO.Path]::GetFullPath((Join-Path $root $Destination))
if (-not $target.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The USB build destination must be inside this checkout/out.'
}
# Reject junction/symlink ancestors before creating or modifying a build tree.
$ancestor = $target
while ($ancestor) {
    if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Reparse-point build path refused: $ancestor"
    }
    $parent = Split-Path -Parent $ancestor
    if ($parent -eq $ancestor) { break }
    $ancestor = $parent
}
New-Item -ItemType Directory -Force -Path $target | Out-Null
$source = Join-Path $target 'source'
$bundle = Join-Path $target 'corresponding-source'
if (-not (Test-Path -LiteralPath $source)) {
    & git clone --no-hardlinks --no-checkout -- $Repository $source
    if ($LASTEXITCODE -ne 0) { throw 'Pinned usbipd source clone failed.' }
    & git -C $source checkout --detach $pin
    if ($LASTEXITCODE -ne 0) { throw 'Pinned usbipd commit is unavailable.' }
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $pin) { throw 'USB source is not at the reviewed commit.' }
$untracked = & git -C $source ls-files --others --exclude-standard
if ($untracked) { throw 'Unexpected untracked source files; use a new build destination.' }
$diff = (& git -C $source diff --binary HEAD) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect USB source changes.' }
if ($diff.Length -eq 0) {
    & git -C $source apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'USB hardening patch no longer applies.' }
    & git -C $source apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'USB hardening patch failed.' }
} else {
    $expected = [IO.File]::ReadAllText($patch).Replace("`r`n", "`n").TrimEnd()
    if ($diff.Replace("`r`n", "`n").TrimEnd() -ne $expected) {
        throw 'USB source has unexpected changes; refusing to overwrite them.'
    }
}
New-Item -ItemType Directory -Force -Path $bundle | Out-Null
$archive = Join-Path $bundle 'usbipd-upstream.tar'
& git -C $source archive --format=tar "--output=$archive" $pin
if ($LASTEXITCODE -ne 0) { throw 'Cannot archive corresponding source.' }
Copy-Item -LiteralPath $patch -Destination $bundle
Copy-Item -LiteralPath (Join-Path $source 'COPYING.md') -Destination $bundle
Copy-Item -LiteralPath (Join-Path $source 'LICENSES') -Destination $bundle -Recurse -Force
Copy-Item -LiteralPath $PSCommandPath -Destination $bundle
$manifest = [ordered]@{
    upstream = 'https://github.com/dorssel/usbipd-win'
    commit = $pin
    sdk = '10.0.403'
    patchSha256 = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
    sourceSha256 = (Get-FileHash -LiteralPath (Join-Path $bundle 'usbipd-upstream.tar') -Algorithm SHA256).Hash.ToLowerInvariant()
    changes = 'IPv4 loopback; local bus + instance allowlist; guarded exact-instance bind with optional preallocated ownership GUID; private registry; no auto-bind or global attachment cleanup; upstream singleton mutex retained.'
    limitations = 'Development backend process only; native ownership/admission and platform drivers are provided separately by ExtraPane. No physical USB compatibility test is performed by this build.'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'BUILD.json') -Encoding UTF8
if (-not ($Build -or $Publish)) { Write-Output "Prepared pinned USB backend source: $source"; return }
Push-Location $source
$oldCliHome = $env:DOTNET_CLI_HOME
$oldCertificate = $env:DOTNET_GENERATE_ASPNET_CERTIFICATE
$oldTelemetry = $env:DOTNET_CLI_TELEMETRY_OPTOUT
try {
    $env:DOTNET_CLI_HOME = Join-Path $target 'dotnet-home'
    $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    $sdk = & $Dotnet --version
    if ($LASTEXITCODE -ne 0 -or $sdk.Trim() -ne '10.0.403') { throw 'The pinned .NET SDK 10.0.403 is required.' }
    # Only the process project is compiled; no installer, service, or backend is run.
    if ($Publish) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ tools are required for native AOT publication.' }
        $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsRoot) { throw 'Visual Studio x64 C++ tools are unavailable.' }
        Import-Module (Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
        Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
        # Match upstream's two-phase build: global runtime/AOT overrides during
        # restore would incorrectly affect the netstandard source generator.
        & $Dotnet build Usbipd/Usbipd.csproj --configuration Release -p:Platform=x64 -p:RestoreLockedMode=true --maxcpucount:2
        if ($LASTEXITCODE -ne 0) { throw 'USB backend build before native publication failed.' }
        & $Dotnet publish Usbipd/Usbipd.csproj --configuration Release --no-build --runtime win-x64 -p:PublishProfile=x64 -p:IlcMaxDegreeOfParallelism=2 --maxcpucount:2 --output (Join-Path $target 'publish')
    } else {
        & $Dotnet build Usbipd/Usbipd.csproj --configuration Release -p:Platform=x64 -p:RestoreLockedMode=true --maxcpucount:2
    }
    if ($LASTEXITCODE -ne 0) { throw 'USB backend process build failed.' }
} finally {
    $env:DOTNET_CLI_HOME = $oldCliHome
    $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = $oldCertificate
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = $oldTelemetry
    Pop-Location
}
Write-Output "USB backend compiled. Keep $bundle with any redistributed binary; this script does not install or run it."
