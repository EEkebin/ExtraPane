param([string]$Version=$env:SIDESCREEN_VERSION)
$ErrorActionPreference='Stop'
if(-not $Version){$Version='0.13.0'}
if($Version -notmatch '^\d+\.\d+\.\d+(-[a-zA-Z0-9.-]+)?$'){throw 'Invalid version'}
$root=Split-Path -Parent $PSScriptRoot
$iscc=Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
if(-not (Test-Path -LiteralPath $iscc)){throw 'Install Inno Setup 6; GitHub windows-2025 includes it.'}
$env:SIDESCREEN_VERSION=$Version
$env:SIDESCREEN_DIST=Join-Path $root 'out\dist'
foreach($role in @('SideScreen')){
 $stage=Join-Path $root "out\installer-stage\$role"
 New-Item -ItemType Directory -Force -Path $stage | Out-Null
 Expand-Archive -LiteralPath (Join-Path $env:SIDESCREEN_DIST "SideScreen-$Version-win-x64.zip") -DestinationPath $stage -Force
 $env:SIDESCREEN_ROLE=$role
 $env:SIDESCREEN_STAGE=Join-Path $stage $role
 & $iscc (Join-Path $root 'packaging\SideScreen.iss')
 if($LASTEXITCODE -ne 0){throw "$role installer compilation failed"}
}
$files=Get-ChildItem -LiteralPath $env:SIDESCREEN_DIST -File | Where-Object {$_.Name -like "SideScreen-$Version-*" -and $_.Extension -in @('.exe','.zip')}
if(@($files).Count -ne 2){throw 'Expected one portable ZIP and one installer'}
$lines=$files | Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"}
$lines | Set-Content -LiteralPath (Join-Path $env:SIDESCREEN_DIST 'SHA256SUMS.txt') -Encoding ascii
