param([Parameter(Mandatory=$true)][string]$Stage,[string]$Package)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$stagePath=[IO.Path]::GetFullPath($Stage)
if(!$stagePath.StartsWith($root+'\out\',[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $stagePath -PathType Container)){throw 'Stage must be an existing build directory under out'}
if(!$Package){$Package=Join-Path $root 'out/driver-package'}
$package=[IO.Path]::GetFullPath($Package)
if(!(Test-Path -LiteralPath (Join-Path $package 'manifest.json'))){throw 'Build and package the driver first'}
$component=Join-Path $stagePath 'Components/Display'
$payload=Join-Path $component 'package'
New-Item -ItemType Directory -Force -Path $payload | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'packaging/manage-driver.ps1') -Destination $component -Force
Get-ChildItem -LiteralPath $package -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $payload -Force}
Write-Host 'Staged the display manager and package for ownership by the main ExtraPane installer.'
