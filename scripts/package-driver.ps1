# Build output only: never installs certificates or drivers on the build machine.
param([string]$Destination)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$Destination){$Destination=Join-Path $root 'out/driver-package'}
$source=Join-Path $root 'out/driver/DMIndirectDisplay'
if(Test-Path -LiteralPath $Destination){throw 'Driver package already exists. Use a new output directory to preserve its signing identity.'}
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach($name in @('DMIndirectDisplay.dll','DMIndirectDisplay.inf','DMIndirectDisplay.cat')){Copy-Item -LiteralPath (Join-Path $source $name) -Destination $Destination}
foreach($name in @('LICENSE','UPSTREAM.md')){Copy-Item -LiteralPath (Join-Path $root "drivers/idd/$name") -Destination $Destination}
$rsa=[Security.Cryptography.RSA]::Create(3072)
try {
 $request=[Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=ExtraPane Development Driver',$rsa,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
 $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
 $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,$true))
 $usage=[Security.Cryptography.OidCollection]::new();$null=$usage.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
 $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usage,$true))
 $cert=$request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5),[DateTimeOffset]::UtcNow.AddYears(5))
 try {
  $cat=Join-Path $Destination 'DMIndirectDisplay.cat'
  $null=Set-AuthenticodeSignature -LiteralPath $cat -Certificate $cert -HashAlgorithm SHA256
  $signature=Get-AuthenticodeSignature -LiteralPath $cat
  if(!$signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $cert.Thumbprint){throw 'Driver catalog signing failed'}
  [IO.File]::WriteAllBytes((Join-Path $Destination 'ExtraPane-Driver.cer'),$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
  $inf=Get-Content -LiteralPath (Join-Path $Destination 'DMIndirectDisplay.inf') -Raw
  if($inf -notmatch '(?im)^DriverVer\s*=\s*([^,\r\n]+),\s*([0-9.]+)'){throw 'Missing stamped driver version'}
  $manifest=[ordered]@{schema=1;driverVersion=$Matches[2];driverDate=$Matches[1];certificateThumbprint=$cert.Thumbprint;certificateExpires=$cert.NotAfter.ToUniversalTime().ToString('o');signing='Development certificate; not Microsoft release signed';files=@()}
  $manifest.files=@(Get-ChildItem -LiteralPath $Destination -File | Sort-Object Name | ForEach-Object {@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding UTF8
  Write-Host "Prepared driver package: $Destination ($($cert.Thumbprint)). Private key discarded; no trust or driver changes."
 } finally {if($cert){$cert.Dispose()}}
} finally {$rsa.Dispose()}
