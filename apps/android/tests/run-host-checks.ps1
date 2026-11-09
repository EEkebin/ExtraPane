param([string]$Jdk = $env:JAVA_HOME)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $Jdk) { $Jdk = (Get-ChildItem (Join-Path $project 'out/android-tools/jdk') -Directory | Select-Object -First 1).FullName }
if (-not $Jdk) { throw 'Set JAVA_HOME to JDK 17 or 21.' }
$destination = Join-Path $project 'out/android-host-checks'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$source = Join-Path $project 'apps/android/app/src/main/java/io/github/eekebin/extrapane'
& (Join-Path $Jdk 'bin/javac.exe') -d $destination (Join-Path $PSScriptRoot 'stubs/Network.java') (Join-Path $PSScriptRoot 'stubs/ClientSession.java') (Join-Path $PSScriptRoot 'stubs/AccessChannel.java') (Join-Path $source 'PairingAttempt.java') (Join-Path $PSScriptRoot 'PairingAttemptCheck.java') (Join-Path $source 'AdmissionBroker.java') (Join-Path $source 'SocketCancellation.java') (Join-Path $source 'ConnectionValidation.java') (Join-Path $source 'CaptureRegistry.java') (Join-Path $source 'DeviceCaptureLease.java') (Join-Path $source 'CaptureAttempt.java') (Join-Path $source 'CaptureFanout.java') (Join-Path $source 'FanoutPackets.java') (Join-Path $source 'CameraPackets.java') (Join-Path $source 'SpeakerProtocol.java') (Join-Path $source 'SpeakerJitter.java') (Join-Path $source 'AudioRouteCatalog.java') (Join-Path $source 'SharingLifecycle.java') (Join-Path $source 'ProviderSessions.java') (Join-Path $source 'NearbyPeers.java') (Join-Path $PSScriptRoot 'NearbyPeersCheck.java') (Join-Path $PSScriptRoot 'ProviderSessionsCheck.java') (Join-Path $source 'PageHistory.java') (Join-Path $source 'DrawerSwipe.java') (Join-Path $PSScriptRoot 'DrawerSwipeCheck.java') (Join-Path $PSScriptRoot 'PageHistoryCheck.java') (Join-Path $PSScriptRoot 'SharingLifecycleCheck.java') (Join-Path $PSScriptRoot 'AudioRouteCatalogCheck.java') (Join-Path $PSScriptRoot 'SpeakerCheck.java') (Join-Path $PSScriptRoot 'CaptureFanoutCheck.java') (Join-Path $PSScriptRoot 'SocketCancellationCheck.java') (Join-Path $PSScriptRoot 'CaptureRegistryCheck.java') (Join-Path $PSScriptRoot 'AdmissionBrokerCheck.java') (Join-Path $PSScriptRoot 'ConnectionValidationCheck.java')
if ($LASTEXITCODE -ne 0) { throw 'Android host-check compilation failed.' }
foreach ($check in @('PairingAttemptCheck', 'ConnectionValidationCheck', 'AdmissionBrokerCheck', 'CaptureRegistryCheck', 'CaptureFanoutCheck', 'SocketCancellationCheck', 'SpeakerCheck', 'AudioRouteCatalogCheck', 'SharingLifecycleCheck', 'ProviderSessionsCheck', 'NearbyPeersCheck', 'PageHistoryCheck', 'DrawerSwipeCheck')) {
    & (Join-Path $Jdk 'bin/java.exe') -cp $destination "io.github.eekebin.extrapane.$check"
    if ($LASTEXITCODE -ne 0) { throw "$check failed." }
}
