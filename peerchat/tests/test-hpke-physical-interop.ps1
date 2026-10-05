param([string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$token=[Guid]::NewGuid().ToString('N')
$out=Join-Path $root ('build\tests\hpke-physical-'+$token)
New-Item -ItemType Directory -Path $out|Out-Null
$cache=Join-Path $root 'build\deps\wolfcrypt-evaluation'
$native=Join-Path $cache 'mingw-x64'
$source=Join-Path $cache 'wolfssl-3c5eead44904df64e6a5a1f4ebdce377d35a849a'
$exe=Join-Path $out 'physical-interop.exe'
$remote='/sdcard/Android/data/com.peerchat.android/files/mailbox-interop/'+$token
function DevicePhase([string]$phase){
    $result=& $Adb shell am instrument -w -r -e class com.peerchat.android.HpkeInteropDeviceTest -e interopToken $token -e interopPhase $phase com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
    $result|Write-Output
    if($LASTEXITCODE -ne 0 -or (($result -join "`n") -notmatch 'OK \(1 test\)')){throw "Physical HPKE instrumentation failed: $phase"}
}
Push-Location $root
try {
    & $Adb get-state
    if($LASTEXITCODE -ne 0){throw 'Authorized Android device required'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -DWOLFCRYPT_ONLY -DNO_CERTS -DPEERCHAT_HPKE_EVALUATION -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I $native -I $source .\tests\hpke_device_interop_test.cpp .\platform\wolfcrypt_evaluation_hpke.cpp (Join-Path $native 'libwolfssl.a') -o $exe -lncrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Physical HPKE native harness compilation failed'}
    # APKs must already have been rebuilt with the current adapter/test. Replace,
    # never uninstall or clear the app; normal profile/identity are untouched.
    & $Adb install -r (Join-Path $root 'android\app\build\outputs\apk\debug\app-debug.apk')
    if($LASTEXITCODE -ne 0){throw 'Replacement application install failed'}
    & $Adb install -r (Join-Path $root 'android\app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk')
    if($LASTEXITCODE -ne 0){throw 'Replacement instrumentation install failed'}
    try {
        DevicePhase 'prepare'
        & $Adb pull ($remote+'/android-public.bin') (Join-Path $out 'android-public.bin')
        if($LASTEXITCODE -ne 0){throw 'Public-only test fixture transfer failed'}
        & $exe --seal $out $token
        if($LASTEXITCODE -ne 0){throw 'Windows seal failed'}
        foreach($file in @('windows-public.bin','windows-cipher.bin')){
            & $Adb push (Join-Path $out $file) ($remote+'/'+$file)
            if($LASTEXITCODE -ne 0){throw 'Public/ciphertext-only fixture transfer failed'}
        }
        DevicePhase 'exchange'
        & $Adb pull ($remote+'/android-cipher.bin') (Join-Path $out 'android-cipher.bin')
        if($LASTEXITCODE -ne 0){throw 'Ciphertext-only test fixture transfer failed'}
        & $exe --open $out $token
        if($LASTEXITCODE -ne 0){throw 'Windows opaque CNG open failed'}
        Write-Host 'PASS: 100 actual Windows -> Android Keystore and 100 Android -> Windows CNG HPKE envelopes; tamper rejection; no private export or normal identity creation.'
    } finally {
        try {DevicePhase 'cleanup'} finally {
            & $exe --cleanup $out $token
            if($LASTEXITCODE -ne 0){throw 'Test-only Windows CNG cleanup failed'}
        }
    }
} finally {Pop-Location}
