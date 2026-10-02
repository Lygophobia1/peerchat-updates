param(
    [string]$SdkRoot=$env:ANDROID_HOME,
    [string]$Gradle='gradle',
    [switch]$ConnectedTests
)
$ErrorActionPreference='Stop'
if(-not $SdkRoot){$SdkRoot=$env:ANDROID_SDK_ROOT}
if(-not $SdkRoot){$SdkRoot=Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
$required=@('platforms\android-35\android.jar','build-tools\34.0.0\aapt2.exe','ndk\27.0.12077973\build\cmake\android.toolchain.cmake','cmake\3.22.1\bin\cmake.exe')
if($ConnectedTests){$required+='platform-tools\adb.exe'}
$missing=@($required | Where-Object {-not (Test-Path -LiteralPath (Join-Path $SdkRoot $_))})
if(-not (Get-Command $Gradle -ErrorAction SilentlyContinue)){$missing+='Gradle 8.9 (not installed; no wrapper has been downloaded)'}
if(-not (Get-Command java -ErrorAction SilentlyContinue)){$missing+='JDK 17+ java executable'}
if($missing.Count){throw "Android preflight failed at '$SdkRoot': $($missing -join '; '). No tools installed or build attempted."}
$version=& $Gradle --version
if($LASTEXITCODE -ne 0 -or ($version -join "`n") -notmatch '(?m)^Gradle 8\.9\s*$'){throw 'This source proof pins Gradle 8.9.'}
$previousSdk=$env:ANDROID_HOME
Push-Location $PSScriptRoot
try {
    $env:ANDROID_HOME=$SdkRoot
    $tasks=@(':app:assembleDebug')
    if($ConnectedTests){$tasks+=':app:connectedDebugAndroidTest'}
    & $Gradle @tasks
    if($LASTEXITCODE -ne 0){throw "Android build failed with exit code $LASTEXITCODE"}
    Write-Host "Debug APK: $(Join-Path $PSScriptRoot 'app\build\outputs\apk\debug\app-debug.apk')"
    Write-Host 'arm64 libraries: APK lib/arm64-v8a/libpeerchat_messaging.so and libpeerchat_jni.so'
} finally {$env:ANDROID_HOME=$previousSdk;Pop-Location}
