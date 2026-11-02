# Explicit installation step. User authorized this exact local package separately.
param([Parameter(Mandatory=$true)][string]$ExpectedThumbprint)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$package = Join-Path $projectRoot 'out\package\dm-display-0.7-test'
$recordPath = Join-Path $projectRoot 'out\display-install-record.json'
$previous = $null
if (Test-Path -LiteralPath $recordPath) {
    $previous = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
    if ($previous.status -ne 'staged' -or $previous.packageDirectory -notlike '*dm-display-0.5-test') { throw 'Only the recorded 0.5 installation can be upgraded.' }
    if (Get-Process dm-view,dm-display,dm-input-check -ErrorAction SilentlyContinue) { throw 'Stop DM sessions before upgrading the driver.' }
}
$manifest = Get-Content -LiteralPath (Join-Path $package 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.certificateThumbprint -ne $ExpectedThumbprint) { throw 'Approved certificate thumbprint does not match package.' }
foreach ($file in $manifest.files) {
    if ([IO.Path]::GetFileName($file.name) -ne $file.name) { throw 'Invalid package filename.' }
    if ((Get-FileHash -LiteralPath (Join-Path $package $file.name) -Algorithm SHA256).Hash -ne $file.sha256) { throw "Package hash mismatch: $($file.name)" }
}
$certificatePath = Join-Path $package 'DM-Local-Development.cer'
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
if ($certificate.Thumbprint -ne $ExpectedThumbprint -or $certificate.NotAfter -lt (Get-Date)) { throw 'Certificate mismatch or expiration.' }
$existing = @(Get-WindowsDriver -Online -All | Where-Object { [IO.Path]::GetFileName($_.OriginalFileName) -ieq 'DMIndirectDisplay.inf' })
if ($existing.Count -and (-not $previous -or $existing.Count -ne 1 -or $existing[0].Driver -ne $previous.publishedInf)) { throw 'Unexpected installed DM driver identity.' }
if ($previous) {
    $archive=Join-Path $projectRoot 'out/display-install-record-0.5.json'
    if(Test-Path -LiteralPath $archive) { throw 'Previous upgrade record already exists.' }
    Copy-Item -LiteralPath $recordPath -Destination $archive
}
$record = [ordered]@{
    certificateThumbprint=$ExpectedThumbprint
    packageDirectory=$package
    started=(Get-Date).ToString('o')
    addedRoot=$false; addedTrustedPublisher=$false
    publishedInf=$null; status='starting'
}
function Save-Record { $record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $recordPath -Encoding UTF8 }
Save-Record
foreach ($store in @('Root','TrustedPublisher')) {
    $entry = "Cert:\LocalMachine\$store\$ExpectedThumbprint"
    if (-not (Test-Path -LiteralPath $entry)) {
        Import-Certificate -FilePath $certificatePath -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
        if ($store -eq 'Root') { $record.addedRoot=$true } else { $record.addedTrustedPublisher=$true }
        Save-Record
    }
}
$signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $package 'dmindirectdisplay.cat')
if ($signature.Status -ne 'Valid') { $record.status='signature-rejected'; Save-Record; throw $signature.StatusMessage }
& pnputil.exe /add-driver (Join-Path $package 'DMIndirectDisplay.inf') /install
$exitCode = $LASTEXITCODE
$installed = @(Get-WindowsDriver -Online -All | Where-Object { [IO.Path]::GetFileName($_.OriginalFileName) -ieq 'DMIndirectDisplay.inf' -and $_.Version -eq '0.7.0.0' })
if ($installed.Count -eq 1) { $record.publishedInf=$installed[0].Driver }
$record.pnpExitCode=$exitCode
$record.status=if ($exitCode -eq 0 -and $installed.Count -eq 1) { 'staged' } else { 'needs-inspection' }
Save-Record
if ($record.status -ne 'staged') { throw 'Driver staging needs inspection. No reboot or boot-policy change was attempted.' }
Write-Host "Staged $($record.publishedInf). Rollback identity saved in $recordPath"
