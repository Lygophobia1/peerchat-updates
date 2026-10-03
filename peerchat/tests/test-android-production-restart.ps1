param([Parameter(Mandatory=$true)][string]$Serial,[int]$TlsRestarts=5,
    [string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe')
$ErrorActionPreference='Stop'
if($TlsRestarts -lt 1 -or $TlsRestarts -gt 20){throw 'Restart count must be 1..20'}
$profile='test_restart_'+[Guid]::NewGuid().ToString('N')
$runner='com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner'
function Invoke-Proof([string]$Class,[string[]]$Extra=@()){
    & $Adb -s $Serial shell am force-stop com.peerchat.android
    if($LASTEXITCODE -ne 0){throw 'Process stop failed'}
    $stopped=$false
    for($attempt=0;$attempt -lt 20;$attempt++){
        $running=(& $Adb -s $Serial shell pidof com.peerchat.android | Out-String).Trim()
        if(-not $running){$stopped=$true;break}
        Start-Sleep -Milliseconds 100
    }
    if(-not $stopped){throw 'PeerChat process did not stop'}
    $launch=(& $Adb -s $Serial shell am start -W -n com.peerchat.android/.MainActivity | Out-String)
    if($LASTEXITCODE -ne 0 -or $launch -notmatch 'Status: ok' -or $launch -notmatch 'LaunchState: COLD'){throw "Cold launch failed: $launch"}
    $result=(& $Adb -s $Serial shell am instrument -w -r -e class $Class @Extra $runner | Out-String)
    if($LASTEXITCODE -ne 0 -or $result -notmatch 'OK \([1-9][0-9]* tests?\)' -or $result -match 'FAILURES!!!|INSTRUMENTATION_FAILED|INSTRUMENTATION_STATUS_CODE: -[234]'){throw "Physical proof failed: $Class`n$result"}
}
try {
    foreach($phase in @('seed','pending','ack','verify','verify')){
        Invoke-Proof 'com.peerchat.android.NativeProfileTest#crossProcessStorageCheckpoint' @('-e','storage_profile',$profile,'-e','storage_phase',$phase)
        Write-Host "PASS: fresh-process production Storage phase=$phase"
    }
} finally {
    # Cleanup exclusively the driver-created UUID test profile, never default.
    Invoke-Proof 'com.peerchat.android.NativeProfileTest#crossProcessStorageCheckpoint' @('-e','storage_profile',$profile,'-e','storage_phase','cleanup')
    Write-Host 'PASS: namespaced restart-test identity/SQLite/artifacts cleaned; normal identity/data retained'
}
# The normal-profile hygiene assertion requires all test aliases gone. Complete
# and clean the cross-process fixture before checking normal identity/records.
for($i=1;$i -le $TlsRestarts;$i++){
    Invoke-Proof 'com.peerchat.android.NativeProfileTest#existingNormalIdentityIsNotReplaced,com.peerchat.android.PeerTlsTest#existingIdentityNativeMutualTls,com.peerchat.android.SecurityProviderTest#productionProfilePersistence'
    Write-Host "PASS: cold launch/reopen/TLS run $i; 100 native Keystore mutual handshakes; normal identity/protected record unchanged"
}
