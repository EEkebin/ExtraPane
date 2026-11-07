param([string]$Jdk = $env:JAVA_HOME)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $Jdk) { $Jdk = (Get-ChildItem (Join-Path $project 'out/android-tools/jdk') -Directory | Select-Object -First 1).FullName }
if (-not $Jdk) { throw 'Set JAVA_HOME to JDK 17 or 21.' }
$destination = Join-Path $project 'out/android-host-checks'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$source = Join-Path $project 'apps/android/app/src/main/java/io/github/eekebin/extrapane'
& (Join-Path $Jdk 'bin/javac.exe') -d $destination (Join-Path $PSScriptRoot 'stubs/Network.java') (Join-Path $PSScriptRoot 'stubs/ClientSession.java') (Join-Path $PSScriptRoot 'stubs/AccessChannel.java') (Join-Path $source 'AdmissionBroker.java') (Join-Path $source 'ConnectionValidation.java') (Join-Path $PSScriptRoot 'AdmissionBrokerCheck.java') (Join-Path $PSScriptRoot 'ConnectionValidationCheck.java')
if ($LASTEXITCODE -ne 0) { throw 'Android host-check compilation failed.' }
foreach ($check in @('ConnectionValidationCheck', 'AdmissionBrokerCheck')) {
    & (Join-Path $Jdk 'bin/java.exe') -cp $destination "io.github.eekebin.extrapane.$check"
    if ($LASTEXITCODE -ne 0) { throw "$check failed." }
}
