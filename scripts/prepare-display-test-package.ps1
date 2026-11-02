# Creates a test signature in the output folder only. Does not trust or install it.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot 'out\driver\DMIndirectDisplay'
$destination = Join-Path $projectRoot 'out\package\dm-display-0.7-test'
if (-not (Test-Path (Join-Path $source 'dmindirectdisplay.cat'))) { throw 'Run build-driver.ps1 first.' }
if (Test-Path -LiteralPath $destination) { throw 'Package already exists; preserve its signing identity.' }
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($name in @('DMIndirectDisplay.dll', 'DMIndirectDisplay.inf', 'dmindirectdisplay.cat')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'drivers\idd\LICENSE') -Destination $destination -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'drivers\idd\UPSTREAM.md') -Destination $destination -Force
# An ephemeral RSA key: no private key export and no certificate-store changes.
$rsa = [System.Security.Cryptography.RSA]::Create(3072)
try {
    $request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=DM Local Display Development', $rsa,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $request.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new([System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true))
    $usages = [System.Security.Cryptography.OidCollection]::new()
    $null = $usages.Add([System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
    $request.CertificateExtensions.Add([System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usages, $true))
    $certificate = $request.CreateSelfSigned([DateTimeOffset]::Now.AddMinutes(-5), [DateTimeOffset]::Now.AddDays(30))
    try {
        $catalog = Join-Path $destination 'dmindirectdisplay.cat'
        $signature = Set-AuthenticodeSignature -LiteralPath $catalog -Certificate $certificate -HashAlgorithm SHA256
        $verified = Get-AuthenticodeSignature -LiteralPath $catalog
        if (-not $verified.SignerCertificate -or $verified.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
            throw "Catalog signing failed: $($signature.Status) $($signature.StatusMessage)"
        }
        [IO.File]::WriteAllBytes((Join-Path $destination 'DM-Local-Development.cer'), $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
        $manifest = [ordered]@{
            version = '0.7.0.0'; purpose = 'Local development test only; not Microsoft release signed'
            certificateThumbprint = $certificate.Thumbprint
            certificateExpires = $certificate.NotAfter.ToString('o')
            signatureStatusBeforeTrust = [string]$verified.Status
            signatureMessageBeforeTrust = $verified.StatusMessage
            files = @(Get-ChildItem -LiteralPath $destination -File | Where-Object Name -ne 'manifest.json' | ForEach-Object {
                @{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
            })
        }
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding UTF8
        Write-Host "Prepared $destination"
        Write-Host "Certificate thumbprint: $($certificate.Thumbprint)"
        Write-Host "Catalog status before trust: $($verified.Status)"
        Write-Host 'No certificate was trusted or installed. No driver or boot configuration changed.'
    } finally { if ($certificate) { $certificate.Dispose() } }
} finally { $rsa.Dispose() }
