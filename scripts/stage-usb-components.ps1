param([string]$Destination = 'out/usb-components-stage')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$target = [IO.Path]::GetFullPath((Join-Path $root $Destination))
if (-not $target.StartsWith(($root + '\out\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Staging must stay in checkout/out.' }
if (Test-Path -LiteralPath $target) { throw 'Use an empty new staging directory.' }
$name = 'USBip-0.9.8.1-x64.exe'
$hash = '38cad6d4432b52d5bb9409d9ad03b72fdffc4ada4cd3a48fbeca1a2752a8518a'
New-Item -ItemType Directory -Path $target | Out-Null
$file = Join-Path $target $name
try {
    Invoke-WebRequest -Uri 'https://api.github.com/repos/vadimgrn/usbip-win2/releases/assets/592292138' -Headers @{ Accept = 'application/octet-stream' } -OutFile $file
} catch {
    Invoke-WebRequest -Uri "https://github.com/vadimgrn/usbip-win2/releases/download/v.0.9.8.1/$name" -OutFile $file
}
if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'USB/IP installer checksum mismatch.' }
$signature = Get-AuthenticodeSignature -LiteralPath $file
if ($signature.Status -ne 'Valid') { throw 'USB/IP installer signature is not valid.' }
@{
    name = 'usbip-win2'; version = '0.9.8.1'; architecture = 'x64'
    sha256 = $hash; signer = $signature.SignerCertificate.Subject
    source = 'https://github.com/vadimgrn/usbip-win2/tree/v.0.9.8.1'
    installed = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'component.json') -Encoding utf8
Write-Host "Verified component staged at $target. Nothing was installed or executed."
