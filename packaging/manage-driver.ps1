# Invoked by the elevated, separately registered driver installer/uninstaller.
param([ValidateSet('Install','Remove','Validate')][string]$Action='Validate')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Use only this Windows PowerShell installation's built-in modules. Setup may
# inherit PSModulePath from PowerShell 7 or another launching shell; those
# modules are not necessarily loadable in Windows PowerShell 5.1.
$env:PSModulePath=Join-Path $PSHOME 'Modules'
$package=Join-Path $PSScriptRoot 'package'
$state=Join-Path $PSScriptRoot 'state'
$log=Join-Path $PSScriptRoot 'driver-setup.log'
$mutex=$null;$held=$false
function Write-Log([string]$text){Add-Content -LiteralPath $log -Value ((Get-Date).ToString('o')+' '+$text);Write-Output $text}
function Verify-Package([string]$directory){
 $manifest=Get-Content -LiteralPath (Join-Path $directory 'manifest.json') -Raw | ConvertFrom-Json
 if($manifest.schema -ne 1 -or $manifest.certificateThumbprint -notmatch '^[A-Fa-f0-9]{40}$'){throw 'Invalid driver manifest'}
 $certificateName=if(@($manifest.files.name) -contains 'ExtraPane-Driver.cer'){'ExtraPane-Driver.cer'}else{'SideScreen-Driver.cer'}
 $expected=@('DMIndirectDisplay.dll','DMIndirectDisplay.inf','DMIndirectDisplay.cat',$certificateName,'LICENSE','UPSTREAM.md')
 if(@($manifest.files).Count -ne $expected.Count){throw 'Unexpected driver package contents'}
 $seen=@{}
 foreach($file in $manifest.files){
  if($file.name -notin $expected -or $seen.ContainsKey($file.name) -or $file.sha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Invalid or duplicate driver filename/hash'}
  $seen[$file.name]=$true
  if((Get-FileHash -LiteralPath (Join-Path $directory $file.name) -Algorithm SHA256).Hash -ne $file.sha256){throw "Driver package hash mismatch: $($file.name)"}
 }
 $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $directory $certificateName))
 try {
  if($cert.Thumbprint -ne $manifest.certificateThumbprint -or $cert.HasPrivateKey){throw 'Driver certificate identity mismatch'}
  if($Action -ne 'Remove' -and ($cert.NotAfter -lt (Get-Date) -or $cert.NotBefore -gt (Get-Date))){throw 'Driver certificate is expired or not valid yet'}
  $signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $directory 'DMIndirectDisplay.cat')
  if(!$signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $cert.Thumbprint){throw 'Driver catalog signer mismatch'}
 } finally {$cert.Dispose()}
 return $manifest
}
function Save-Record($record,[string]$path){$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath ($path+'.tmp') -Encoding UTF8;Move-Item -LiteralPath ($path+'.tmp') -Destination $path -Force}
function Find-ExactDriver([string]$infHash,[string]$catHash){
 @(Get-WindowsDriver -Online -All | Where-Object {
  [IO.Path]::GetFileName($_.OriginalFileName) -ieq 'DMIndirectDisplay.inf' -and $_.ProviderName -eq 'DM Project'
 } | Where-Object {
  $cat=Join-Path (Split-Path -Parent $_.OriginalFileName) 'DMIndirectDisplay.cat'
  (Get-FileHash -LiteralPath $_.OriginalFileName -Algorithm SHA256).Hash -eq $infHash -and (Test-Path -LiteralPath $cat) -and (Get-FileHash -LiteralPath $cat -Algorithm SHA256).Hash -eq $catHash
 })
}
function Remove-AddedTrust($record){
 foreach($store in @('Root','TrustedPublisher')){
  $added=if($store -eq 'Root'){$record.addedRoot}else{$record.addedTrustedPublisher}
  $entry="Cert:\LocalMachine\$store\$($record.thumbprint)"
  if($added -and (Test-Path -LiteralPath $entry)){Remove-Item -LiteralPath $entry;Write-Log "Removed installer-owned $store certificate $($record.thumbprint)"}
 }
}
try {
 if($Action -eq 'Validate'){$null=Verify-Package $package;Write-Output 'DRIVER_PACKAGE_VALID';exit 0}
 if(-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Driver installation/removal requires administrator permission'}
 # State and elevated scripts must live in the fixed, administrator-controlled directory.
 # Inno Setup is a 32-bit process; its child can inherit the x86 ProgramFiles
 # value even when launching 64-bit PowerShell. ProgramW6432 identifies the
 # native Program Files directory in both process environments.
 $expected=[IO.Path]::GetFullPath((Join-Path $env:ProgramW6432 'SideScreen Driver'))
 if([IO.Path]::GetFullPath($PSScriptRoot) -ne $expected){throw 'Driver manager must run from Program Files\SideScreen Driver'}
 foreach($dir in @($PSScriptRoot,$state)){
  if((Test-Path -LiteralPath $dir) -and ((Get-Item -LiteralPath $dir).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Driver management directory cannot be a reparse point'}
 }
 $mutex=[Threading.Mutex]::new($false,'Global\SideScreen.DriverSetup');$held=$mutex.WaitOne(0)
 if(!$held){throw 'Another ExtraPane driver installation is running'}
 if(Get-Process dm-view,dm-display,ExtraPane.Service,SideScreen.Service -ErrorAction SilentlyContinue){throw 'Close ExtraPane sessions and stop the server before changing the driver'}
 New-Item -ItemType Directory -Path $state -Force | Out-Null
 if($Action -eq 'Install'){
  $manifest=Verify-Package $package
  $infHash=(Get-FileHash -LiteralPath (Join-Path $package 'DMIndirectDisplay.inf') -Algorithm SHA256).Hash
  $catHash=(Get-FileHash -LiteralPath (Join-Path $package 'DMIndirectDisplay.cat') -Algorithm SHA256).Hash
  $recordPath=Join-Path $state ($catHash+'.json')
  if(Test-Path -LiteralPath $recordPath){$record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json}
  else {$record=[pscustomobject]@{schema=1;thumbprint=$manifest.certificateThumbprint;infHash=$infHash;catHash=$catHash;addedRoot=$false;addedTrustedPublisher=$false;ownedDriver=$false;publishedInf='';status='starting'}}
  if($record.thumbprint -ne $manifest.certificateThumbprint -or $record.infHash -ne $infHash -or $record.catHash -ne $catHash){throw 'Installation record identity mismatch'}
  $before=@(Find-ExactDriver $infHash $catHash)
  if($before.Count -gt 1){throw 'Ambiguous existing driver package'}
  if($before.Count -eq 1 -and $record.status -eq 'installed'){Write-Log "Driver already installed as $($before[0].Driver)";exit 0}
  $backup=Join-Path $state $catHash
  New-Item -ItemType Directory -Path $backup -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $package 'manifest.json') -Destination $backup -Force
  foreach($file in $manifest.files){Copy-Item -LiteralPath (Join-Path $package $file.name) -Destination $backup -Force}
  Save-Record $record $recordPath
  try {
   foreach($store in @('Root','TrustedPublisher')){
    $entry="Cert:\LocalMachine\$store\$($manifest.certificateThumbprint)"
    if(!(Test-Path -LiteralPath $entry)){
     # Record ownership first so an interrupted import is recoverable.
     if($store -eq 'Root'){$record.addedRoot=$true}else{$record.addedTrustedPublisher=$true};Save-Record $record $recordPath
     Import-Certificate -FilePath (Join-Path $package 'ExtraPane-Driver.cer') -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
    }
   }
   $signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $package 'DMIndirectDisplay.cat')
   if($signature.Status -ne 'Valid'){throw ('Windows rejected driver catalog trust: '+$signature.StatusMessage)}
   # SetupAPI validates the catalog membership and returns the exact published INF;
   # do not parse localized PnPUtil output or touch any boot/security policy.
   Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ExtraPaneDriverStore {
 [DllImport("setupapi.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool SetupCopyOEMInf(string source,string media,uint type,uint flags,StringBuilder destination,uint size,out uint required,IntPtr component);
}
'@
   $destination=[Text.StringBuilder]::new(1024);$required=[uint32]0
   $record.ownedDriver=$record.ownedDriver -or $before.Count -eq 0;Save-Record $record $recordPath
   if(![ExtraPaneDriverStore]::SetupCopyOEMInf((Join-Path $package 'DMIndirectDisplay.inf'),$package,1,0,$destination,1024,[ref]$required,[IntPtr]::Zero)){throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())}
   $installed=@(Find-ExactDriver $infHash $catHash)
   if($installed.Count -ne 1){throw 'Driver staging returned without an exact package match'}
   $record.publishedInf=$installed[0].Driver;$record.status='installed';Save-Record $record $recordPath
   Write-Log "DRIVER_INSTALLED $($record.publishedInf). No monitor was created; no boot policy was changed."
  } catch {
   $failure=$_; $installed=@(Find-ExactDriver $infHash $catHash)
   if($installed.Count -eq 0){Remove-AddedTrust $record;$record.addedRoot=$false;$record.addedTrustedPublisher=$false;$record.status='failed-no-driver'}
   else {$record.publishedInf=$installed[0].Driver;$record.status='needs-repair'}
   Save-Record $record $recordPath;throw $failure
  }
 } else {
  foreach($file in @(Get-ChildItem -LiteralPath $state -Filter '*.json' -File)){
   $record=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
   if($record.schema -ne 1 -or $record.catHash -notmatch '^[A-Fa-f0-9]{64}$' -or $file.BaseName -ne $record.catHash){throw 'Invalid driver ownership record'}
   $backup=Join-Path $state $record.catHash;$manifest=Verify-Package $backup
   if($record.thumbprint -ne $manifest.certificateThumbprint -or $record.infHash -ne (Get-FileHash -LiteralPath (Join-Path $backup 'DMIndirectDisplay.inf') -Algorithm SHA256).Hash -or $record.catHash -ne (Get-FileHash -LiteralPath (Join-Path $backup 'DMIndirectDisplay.cat') -Algorithm SHA256).Hash){throw 'Removal record does not match verified package'}
   $installed=@(Find-ExactDriver $record.infHash $record.catHash)
   if($installed.Count -gt 1){throw 'Ambiguous driver removal identity'}
   if($record.ownedDriver -and $installed.Count){
    $oem=$installed[0].Driver;if($oem -notmatch '^oem[0-9]+\.inf$'){throw 'Invalid published driver identity'}
    & "$env:SystemRoot\System32\pnputil.exe" /delete-driver $oem /uninstall | ForEach-Object {Write-Log $_}
    if($LASTEXITCODE -ne 0){throw "Driver removal returned $LASTEXITCODE. Restart if Windows requested it, then retry. Certificate trust was retained."}
    if(@(Find-ExactDriver $record.infHash $record.catHash).Count){throw 'Driver is still staged; certificate trust retained'}
   }
   # Never remove trust for a pre-existing package that is still installed.
   if(!@(Find-ExactDriver $record.infHash $record.catHash).Count){Remove-AddedTrust $record}
   $record.status='removed';Save-Record $record $file.FullName
  }
  Write-Log 'DRIVER_REMOVED: installer-owned packages removed; pre-existing drivers/trust preserved.'
 }
 exit 0
} catch {Write-Log ('ERROR: '+$_.Exception.Message);exit 1}
finally {if($held){$mutex.ReleaseMutex()};if($mutex){$mutex.Dispose()}}
