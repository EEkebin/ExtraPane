param([string]$Version=$env:EXTRAPANE_VERSION)
$ErrorActionPreference='Stop'
if(-not $Version){$Version=& python "$PSScriptRoot/release_version.py"}

$root=Split-Path -Parent $PSScriptRoot
$iscc=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if(-not (Test-Path -LiteralPath $iscc)){throw 'Install Inno Setup 6; GitHub windows-2025 includes it.'}
$env:EXTRAPANE_VERSION=$Version
$env:EXTRAPANE_NUMERIC_VERSION=& python "$PSScriptRoot/release_version.py" --numeric
if($LASTEXITCODE -ne 0){throw 'Invalid release version'}
$env:EXTRAPANE_DIST=Join-Path $root 'out\dist'
foreach($role in @('ExtraPane')){
 $stage=Join-Path $root "out\installer-stage\$role"
 New-Item -ItemType Directory -Force -Path $stage | Out-Null
 Expand-Archive -LiteralPath (Join-Path $env:EXTRAPANE_DIST "ExtraPane-$Version-win-x64.zip") -DestinationPath $stage -Force
 $env:EXTRAPANE_ROLE=$role
 $env:EXTRAPANE_STAGE=Join-Path $stage $role
 & $iscc (Join-Path $root 'packaging\ExtraPane.iss')
 if($LASTEXITCODE -ne 0){throw "$role installer compilation failed"}
}
$files=Get-ChildItem -LiteralPath $env:EXTRAPANE_DIST -File | Where-Object {$_.Name -like "ExtraPane-$Version-*" -and $_.Extension -in @('.exe','.zip')}
if(@($files).Count -ne 2){throw 'Expected one portable ZIP and one installer'}
$lines=$files | Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"}
$lines | Set-Content -LiteralPath (Join-Path $env:EXTRAPANE_DIST 'SHA256SUMS.txt') -Encoding ascii
