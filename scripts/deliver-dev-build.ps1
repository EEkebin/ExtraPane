<#
Installs the CI build of the current commit for testing.

Every pushed branch is built by .github/workflows/prerelease.yml with a CI-assigned
version (YYYY.MM.DD-pre.RUN.ATTEMPT). This script finds that run for HEAD,
downloads the Windows installer and signed Android APK, verifies their SHA-256
sums, copies the installer to the Desktop (it does not run it) and stream-installs
the APK over ADB, then confirms the installed version.

  ./scripts/deliver-dev-build.ps1             # wait for the run if it is still going
  ./scripts/deliver-dev-build.ps1 -SkipAndroid
  ./scripts/deliver-dev-build.ps1 -RunId 42792269814   # a specific CI run
  ./scripts/deliver-dev-build.ps1 -Device 10.0.20.153:46817
#>
param(
    # Install this CI run instead of the one for HEAD (for example after docs-only commits).
    [string]$RunId,
    [string]$Device,
    [switch]$SkipAndroid,
    [switch]$SkipWindows,
    [switch]$NoWait,
    [string]$Desktop = [Environment]::GetFolderPath('Desktop')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
function Need($ok, $why) { if (-not $ok) { throw $why } }

$run = $null
if ($RunId) {
    $run = gh run view $RunId --json databaseId,status,conclusion,headSha,workflowName | ConvertFrom-Json
    Need ($LASTEXITCODE -eq 0 -and $run.workflowName -eq 'Build and release') "Run $RunId is not a Build and release run."
    $sha = $run.headSha
}
else {
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
$sha = (git rev-parse HEAD).Trim()
Need ($branch -ne 'HEAD') 'Check out a branch first.'
git fetch --quiet origin $branch
$remote = (git rev-parse "origin/$branch" 2>$null)
Need ($remote -and $remote.Trim() -eq $sha) "Push $branch first: CI builds pushed commits only (HEAD $($sha.Substring(0,8)))."
}

# Find the build for exactly this commit.
for ($attempt = 0; $attempt -lt 30 -and -not $run; $attempt++) {
    $runs = gh run list --workflow prerelease.yml --branch $branch --commit $sha --event push --limit 5 --json databaseId,status,conclusion | ConvertFrom-Json
    Need ($LASTEXITCODE -eq 0) 'Cannot list CI runs (is gh logged in?).'
    $run = $runs | Select-Object -First 1
    if (-not $run) { if ($NoWait) { break }; Start-Sleep -Seconds 10 }
}
Need $run "No CI build found for $($sha.Substring(0,8)) on $branch."
if ($run.status -ne 'completed') {
    Need (-not $NoWait) "CI run $($run.databaseId) is still $($run.status)."
    Write-Host "Waiting for CI run $($run.databaseId)..."
    gh run watch $run.databaseId --exit-status --interval 30 | Out-Null
    $run = gh run view $run.databaseId --json databaseId,status,conclusion | ConvertFrom-Json
}
Need ($run.conclusion -eq 'success') "CI run $($run.databaseId) finished with '$($run.conclusion)'. See: gh run view $($run.databaseId) --log-failed"

$dir = Join-Path $root "out/ci-builds/$($run.databaseId)"
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
gh run download $run.databaseId -n release-metadata -D $dir
Need ($LASTEXITCODE -eq 0) 'Cannot download release metadata.'
$version = (Get-Content (Join-Path $dir 'release-metadata.json') -Raw | ConvertFrom-Json).version
Need $version 'Release metadata has no version.'
Write-Host "CI build $version (run $($run.databaseId), commit $($sha.Substring(0,8)))"

function Verify($file, $expected) {
    $actual = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    Need ($actual -eq $expected.ToLowerInvariant()) "Checksum mismatch: $file"
}

if (-not $SkipWindows) {
    $win = Join-Path $dir 'windows'
    gh run download $run.databaseId -n "ExtraPane-$version" -D $win
    Need ($LASTEXITCODE -eq 0) 'Cannot download the Windows artifact.'
    $installer = Get-ChildItem $win -Filter "ExtraPane-$version-Setup-win-x64.exe" | Select-Object -First 1
    Need $installer 'Windows installer missing from the artifact.'
    $sums = Get-Content (Join-Path $win 'SHA256SUMS.txt') | Where-Object { $_ -match "\s\*?$([regex]::Escape($installer.Name))$" }
    Need $sums 'Installer is not listed in SHA256SUMS.txt.'
    $expected = ($sums -split '\s+')[0]
    Verify $installer.FullName $expected
    $target = Join-Path $Desktop $installer.Name
    Copy-Item -LiteralPath $installer.FullName -Destination $target -Force
    Verify $target $expected
    Write-Host "Installer copied (not run): $target"
}

if (-not $SkipAndroid) {
    $apkDir = Join-Path $dir 'android'
    gh run download $run.databaseId -n "ExtraPane-Android-$version" -D $apkDir
    Need ($LASTEXITCODE -eq 0) 'Cannot download the signed Android artifact.'
    $apk = Get-ChildItem $apkDir -Filter "*.apk" | Select-Object -First 1
    Need $apk 'APK missing from the artifact.'
    Verify $apk.FullName ((Get-Content "$($apk.FullName).sha256" -Raw).Trim() -split '\s+')[0]
    $adb = (Get-Command adb -ErrorAction SilentlyContinue).Source
    if (-not $adb) { $adb = 'C:/Program Files/adb/platform-tools/adb.exe' }
    Need (Test-Path $adb) 'adb was not found.'
    if (-not $Device) {
        $devices = & $adb devices | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice$' } | ForEach-Object { ($_ -split '\s+')[0] }
        Need (@($devices).Count -eq 1) "Connect exactly one ADB device or pass -Device (found: $(@($devices) -join ', '))."
        $Device = @($devices)[0]
    }
    & $adb -s $Device install --streaming -r $apk.FullName
    Need ($LASTEXITCODE -eq 0) 'APK installation failed.'
    $installed = & $adb -s $Device shell dumpsys package io.github.eekebin.extrapane | Select-String 'versionName=' | Select-Object -First 1
    Need ($installed -and $installed.Line.Trim() -eq "versionName=$version") "Device reports '$($installed.Line.Trim())', expected $version."
    Write-Host "Android $version installed on $Device"
}
