param([switch]$Checks)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(-not $env:JAVA_HOME){
    $localJdk=Get-ChildItem "$project/out/android-tools/jdk" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    if($localJdk){$env:JAVA_HOME=$localJdk.FullName}
}
if(-not $env:ANDROID_HOME -and (Test-Path "$project/out/android-tools/sdk")){$env:ANDROID_HOME="$project/out/android-tools/sdk"}
if(-not $env:JAVA_HOME -or -not $env:ANDROID_HOME){throw 'Set JAVA_HOME to JDK 17/21 and ANDROID_HOME to your Android SDK.'}
$tasks=@('assembleDebug')
if($Checks){$tasks+=@('assembleDebugAndroidTest','lintDebug')}
Push-Location "$project/apps/android"
try {
    $ErrorActionPreference='Continue' # javac writes informational deprecation notes to stderr.
    & ./gradlew.bat @tasks --console=plain
    $buildExit=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    if($buildExit -ne 0){throw 'Android build failed'}
} finally {$ErrorActionPreference='Stop'; Pop-Location}
Write-Output 'APK: apps/android/app/build/outputs/apk/debug/app-debug.apk. No device installation or tests run by this script.'
