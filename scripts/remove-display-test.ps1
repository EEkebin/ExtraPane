param([Parameter(Mandatory=$true)][string]$ExpectedThumbprint,[ValidateSet("Current","Previous")][string]$Record="Current")
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$recordPath = Join-Path $projectRoot $(if($Record -eq 'Previous') {'out\display-install-record-0.5.json'} else {'out\display-install-record.json'})
$record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
if ($record.certificateThumbprint -ne $ExpectedThumbprint -or $ExpectedThumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'Certificate identity does not match the installation record.' }
if ($record.status -ne 'staged' -or $record.publishedInf -notmatch '^oem[0-9]+\.inf$') { throw 'Installation record is not a staged package. Inspect before removing anything.' }
if (Get-Process dm-view,dm-display -ErrorAction SilentlyContinue) { throw 'End DM sessions and display tests before uninstalling.' }
$models = @(Get-WindowsDriver -Online -Driver $record.publishedInf)
if (-not $models.Count) { throw 'Recorded driver was not found. Inspect before removing certificates.' }
foreach ($model in $models) {
    if ([IO.Path]::GetFileName($model.OriginalFileName) -ine 'DMIndirectDisplay.inf' -or $model.ProviderName -ne 'DM Project') { throw 'Published driver identity changed. Refusing removal.' }
}
& pnputil.exe /delete-driver $record.publishedInf /uninstall
if ($LASTEXITCODE -ne 0) { throw 'Driver removal did not complete normally. Certificate retained; no reboot attempted.' }
foreach ($store in @('Root','TrustedPublisher')) {
    $wasAdded = if ($store -eq 'Root') { $record.addedRoot } else { $record.addedTrustedPublisher }
    $path = "Cert:\LocalMachine\$store\$ExpectedThumbprint"
    if ($wasAdded -and (Test-Path -LiteralPath $path)) { Remove-Item -LiteralPath $path }
}
$record.status='removed'
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $recordPath -Encoding UTF8
Write-Host 'Removed the recorded DM package and only the certificate-store entries added by that installation.'
