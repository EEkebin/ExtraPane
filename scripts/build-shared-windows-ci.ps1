# Build-only CI entry point. No app session, driver installation or trust changes.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vsRoot){throw 'MSVC C++ tools are unavailable'}
& (Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $vsRoot -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo | Out-Null
$qt=Join-Path $root 'out/qt-sdk/6.8.3/msvc2022_64'
$env:PATH=(Join-Path $qt 'bin')+';'+$env:PATH
& (Join-Path $PSScriptRoot 'stage-usb-components.ps1') -Destination out/usb-components
& (Join-Path $PSScriptRoot 'build-usb-host.ps1') -Publish
& cargo +1.88.0 test --locked --manifest-path apps/microphone-endpoint/Cargo.toml --jobs 2
if($LASTEXITCODE){throw 'Microphone endpoint checks failed'}
& cargo +1.88.0 build --locked --release --manifest-path apps/microphone-endpoint/Cargo.toml --jobs 2
if($LASTEXITCODE){throw 'Microphone endpoint build failed'}
& cmake -S . -B out/shared-engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DEXTRAPANE_SESSION_TESTS=ON -DEXTRAPANE_RESOURCE_SECURITY_TESTS=ON -DEXTRAPANE_MICROPHONE_TESTS=ON -DEXTRAPANE_USB_SETUP=ON -DEXTRAPANE_USB_HOST_SETUP=ON
if($LASTEXITCODE){throw 'Native engine configure failed'}
& cmake --build out/shared-engine --parallel 2
if($LASTEXITCODE){throw 'Native engine build failed'}
& ctest --test-dir out/shared-engine --output-on-failure -R 'camera-|resource-.*-config|microphone-|speaker-|usb-inventory|usb-storage|usb-host-backend|usb-recovery|usb-source-config|usb-receiver-config|usb-config-reject|grouped-admission|resource-security|access-code-security|usb-monitor-policy|usb-setup-policy|usb-setup-payload|usb-host-setup-payload'
if($LASTEXITCODE){throw 'Isolated native component checks failed'}
& cmake -S apps/shared -B out/shared-ui -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt"
if($LASTEXITCODE){throw 'Shared UI configure failed'}
& cmake --build out/shared-ui --parallel 2
if($LASTEXITCODE){throw 'Shared UI build failed'}
& ctest --test-dir out/shared-ui --output-on-failure
if($LASTEXITCODE){throw 'Shared UI checks failed'}
& (Join-Path $PSScriptRoot 'build-driver.ps1')
& (Join-Path $PSScriptRoot 'package-driver.ps1')
