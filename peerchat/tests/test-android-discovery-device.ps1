param([string]$Serial='ZA2235NTXZ',[string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe',[string]$ServerDirectory='build\development\4.0.1-dev.13\Server')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$server=Join-Path (Join-Path $root $ServerDirectory) 'PeerChat.Server.exe'
if(-not(Test-Path -LiteralPath $server)){throw 'Build the current isolated server before running this proof'}
$configured=Get-Content -Raw -LiteralPath 'C:\ProgramData\PeerChat\Server\Config\server-config.json'|ConvertFrom-Json
$cert=Get-ChildItem Cert:\CurrentUser\My|Where-Object {$_.Thumbprint -eq $configured.CertificateThumbprint -and $_.HasPrivateKey}|Select-Object -First 1
if(-not $cert){throw 'Existing development certificate unavailable'}
$pin=($cert.GetCertHash([Security.Cryptography.HashAlgorithmName]::SHA256)|ForEach-Object {$_.ToString('x2')}) -join ''
$out=Join-Path $root ('build\tests\android-discovery-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$config=Join-Path $out 'config.json'
@{DataDirectory=$out;DatabasePath=(Join-Path $out 'test.sqlite');LogDirectory=(Join-Path $out 'logs');Port=$port;CertificateThumbprint=$cert.Thumbprint;TurnEnabled=$false}|ConvertTo-Json|Set-Content -LiteralPath $config -Encoding utf8
$process=Start-Process -FilePath $server -ArgumentList "--config `"$config`" --port $port --management-pipe AndroidDiscovery-$([Guid]::NewGuid().ToString('N'))" -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $out 'stdout.txt') -RedirectStandardError (Join-Path $out 'stderr.txt')
try {
    Start-Sleep -Milliseconds 800
    if($process.HasExited){throw 'Isolated server startup failed'}
    & $Adb -s $Serial reverse "tcp:$port" "tcp:$port"
    if($LASTEXITCODE -ne 0){throw 'ADB reverse failed'}
    $result=& $Adb -s $Serial shell am instrument -w -r -e serverPort $port -e serverPin $pin -e class com.peerchat.android.DiscoveryTest com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner 2>&1
    $code=$LASTEXITCODE;$result|Write-Host
    $report=$result -join "`n"
    if($code -ne 0 -or $report -notmatch 'OK \(1 test\)' -or $report -match 'FAILURES|INSTRUMENTATION_FAILED|INSTRUMENTATION_STATUS_CODE: -[234]'){throw 'Physical discovery proof failed'}
    Write-Host 'PASS: actual Android TLS discovery/register/login/presence/event reconnect and fail-closed checks'
} finally {
    & $Adb -s $Serial reverse --remove "tcp:$port"|Out-Null
    if(-not $process.HasExited){Stop-Process -Id $process.Id -Force}
}
