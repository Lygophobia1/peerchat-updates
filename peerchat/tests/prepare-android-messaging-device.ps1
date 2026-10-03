param([ValidateSet('Prepare','Collect')][string]$Mode='Prepare',
    [string]$Serial='ZA2235NTXZ',
    [string]$ExpectedPeerId='61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b',
    [string]$Adb='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$Adb){
    $command=Get-Command adb -ErrorAction SilentlyContinue
    $Adb=if($command){$command.Source}else{Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'}
}
if(!(Test-Path -LiteralPath $Adb)){throw 'Android SDK platform-tools adb is required; no device changes made'}
if($ExpectedPeerId -notmatch '^[0-9a-f]{64}$'){throw 'Expected public identity must be supplied explicitly'}
function Invoke-Adb([string[]]$Arguments){
    $result=& $Adb -s $Serial @Arguments 2>&1
    $code=$LASTEXITCODE
    if($code -ne 0){throw "ADB operation failed (exit $code); no identity/data reset is attempted"}
    return ($result -join "`n")
}
$devices=& $Adb devices -l
if($LASTEXITCODE -ne 0 -or ($devices -join "`n") -notmatch ('(?m)^'+[regex]::Escape($Serial)+'\s+device\b')){throw 'Expected phone must be unlocked and authorized (device, not unauthorized)'}
$model=Invoke-Adb @('shell','getprop','ro.product.model')
$manufacturer=Invoke-Adb @('shell','getprop','ro.product.manufacturer')
$abi=Invoke-Adb @('shell','getprop','ro.product.cpu.abilist')
$api=Invoke-Adb @('shell','getprop','ro.build.version.sdk')
$android=Invoke-Adb @('shell','getprop','ro.build.version.release')
if($manufacturer.Trim() -ne 'motorola' -or $abi -notmatch 'arm64-v8a'){throw 'Expected Motorola ARM64 device required'}
Write-Host "Device: $manufacturer $model / Android $android / API $api / $abi"
$out=Join-Path $root ('build\tests\physical-messaging-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
if($Mode -eq 'Prepare'){
    $installed=Invoke-Adb @('shell','pm','list','packages','com.peerchat.android')
    if($installed -notmatch '(?m)^package:com\.peerchat\.android\s*$'){throw 'Existing normal installation required; this script never uninstalls, clears data or provisions another identity'}
    foreach($relative in @('android\app\build\outputs\apk\debug\app-debug.apk','android\app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk')){
        $apk=Join-Path $root $relative
        if(!(Test-Path -LiteralPath $apk)){throw "Missing current APK: $relative"}
        $result=Invoke-Adb @('install','-r',$apk)
        if($result -notmatch 'Success'){throw 'Replacement installation did not report success'}
    }
    $proof=Invoke-Adb @('shell','am','instrument','-w','-r','-e','class','com.peerchat.android.NativeProfileTest#existingNormalIdentityIsNotReplaced','com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner')
    if($proof -notmatch 'OK \(1 test\)' -or $proof -match 'FAILURES|INSTRUMENTATION_FAILED|INSTRUMENTATION_STATUS_CODE: -[234]'){throw 'Current normal-profile identity/reopen assertion failed'}
    # Existing test writes public representation + signature only, never a private key.
    $encoded=Invoke-Adb @('shell','run-as','com.peerchat.android','base64','files/provider-proof/native-compatibility.bin')
    $publicProof=[Convert]::FromBase64String(($encoded -replace '\s',''))
    if($publicProof.Length -ne 136){throw 'Invalid public compatibility proof length'}
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$peer=([BitConverter]::ToString($sha.ComputeHash([byte[]]$publicProof[0..71]))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
    if($peer -ne $ExpectedPeerId){throw 'Normal identity differs from the accepted phone identity; stop, do not replace or rotate it'}
    $launch=Invoke-Adb @('shell','am','start','-W','-n','com.peerchat.android/.MainActivity')
    if($launch -notmatch 'Status: ok'){throw 'Normal app launch failed'}
    "identity=$peer`ndevice=$model`nandroid=$android`napi=$api`nabi=$abi`nreplacement_install=passed`nnormal_profile_assertion=passed`nlaunch=passed`nphysical_messaging=NOT_YET_VALIDATED" | Set-Content -LiteralPath (Join-Path $out 'preparation.txt') -Encoding UTF8
    Write-Host 'Prepared normal native-backed app. Messaging is NOT marked passing by this script.'
    Write-Host 'Follow docs/ANDROID_MESSAGING_VALIDATION.md: configure verified server, sign in, accept friendship, exchange messages, restart and exchange again.'
} else {
    # Save bounded error counts only. Do not persist logcat messages, credentials,
    # message bodies, certificate bytes, or arbitrary provider exception strings.
    $log=Invoke-Adb @('logcat','-d','-t','2000','-v','brief')
    $patterns=@('UnsatisfiedLinkError','No implementation found','FATAL EXCEPTION','Fatal signal','SIGSEGV','SIGABRT','SQLiteException','KeyStoreException','SSLHandshakeException')
    $counts=$patterns|ForEach-Object {"$_="+[regex]::Matches($log,[regex]::Escape($_)).Count}
    $counts | Set-Content -LiteralPath (Join-Path $out 'bounded-logcat-error-counts.txt') -Encoding UTF8
    Write-Host 'Counts cover the bounded recent device log and require correlation; unrelated app errors can be included. No physical PASS is inferred.'
    $counts|Write-Host
}
Write-Host "Evidence folder: $out"
