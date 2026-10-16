param([Parameter(Mandatory)][string]$ServerDirectory,[Parameter(Mandatory)][string]$FixtureDirectory,
      [Parameter(Mandatory)][string]$Classpath,[string]$JavaHome='C:\Program Files\Java\jdk-22')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=(Resolve-Path -LiteralPath $FixtureDirectory).Path
$serverExecutable=Join-Path (Resolve-Path -LiteralPath $ServerDirectory).Path 'PeerChat.Server.exe'
if(!(Test-Path -LiteralPath $serverExecutable)){throw 'Current-worktree Server executable missing'}
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
& "$framework\csc.exe" /nologo /target:exe /platform:x64 /optimize+ "/out:$out\relay-auth-fixture.exe" "/reference:$framework\System.Web.Extensions.dll" "$root\desktop\DiscoveryClient.cs" "$root\desktop\ArchiveEnrollmentExecution.cs" "$root\desktop\ProfileAccounts.cs" "$root\tests\RelayAccountFixture.cs"
if($LASTEXITCODE -ne 0){throw 'Relay account host fixture compilation failed'}
# Read public certificate metadata only. Reuse the existing configured developer
# certificate; never alter production config or create a second server identity.
$config=Join-Path $env:ProgramData 'PeerChat\Server\Config\server-config.json'
if(!(Test-Path -LiteralPath $config)){throw 'Configured developer certificate metadata unavailable'}
$thumbprint=(Get-Content -Raw -LiteralPath $config|ConvertFrom-Json).CertificateThumbprint
$certificate=Get-ChildItem -Path 'Cert:\CurrentUser\My'|Where-Object {$_.Thumbprint -eq $thumbprint -and $_.HasPrivateKey}|Select-Object -First 1
if(!$certificate){throw 'Configured developer certificate unavailable to current Windows user'}
$pin=($certificate.GetCertHash([Security.Cryptography.HashAlgorithmName]::SHA256)|ForEach-Object {$_.ToString('X2')}) -join ''
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$configPath=Join-Path $out 'isolated-server.json'
New-Item -ItemType Directory -Path (Join-Path $out 'server-data') -Force|Out-Null
[ordered]@{DataDirectory=(Join-Path $out 'server-data');DatabasePath=(Join-Path $out 'server-data\server.sqlite');LogDirectory=(Join-Path $out 'server-data\logs');ListenAddress='127.0.0.1';PublicHostname='localhost';Port=$port;CertificateThumbprint=$thumbprint;TurnEnabled=$false;ManagementPipe=('peerchat-jni-test-'+[Guid]::NewGuid().ToString('N'))}|ConvertTo-Json|Set-Content -LiteralPath $configPath -Encoding UTF8
$start=[Diagnostics.ProcessStartInfo]::new($serverExecutable)
$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
$start.ArgumentList.Add('--config');$start.ArgumentList.Add($configPath)
$server=[Diagnostics.Process]::Start($start)
$stdout=$server.StandardOutput.ReadToEndAsync();$stderr=$server.StandardError.ReadToEndAsync()
try {
    $watch=[Diagnostics.Stopwatch]::StartNew();$ready=$false
    while($watch.ElapsedMilliseconds -lt 10000 -and !$ready){
        if($server.HasExited){throw ('Isolated relay server exited during startup: '+$stderr.GetAwaiter().GetResult().Trim())}
        $probe=[Net.Sockets.TcpClient]::new()
        try{$probe.Connect('127.0.0.1',$port);$ready=$true}catch{Start-Sleep -Milliseconds 50}finally{$probe.Dispose()}
    }
    if(!$ready){throw 'Isolated relay server startup timed out'}
    & "$JavaHome\bin\java.exe" -Xcheck:jni "-Djava.library.path=$out" -cp "$out;$Classpath" com.peerchat.android.platform.tls.ServerRelayProof "$out\server-profiles" "$out\relay-auth-fixture.exe" localhost $port $pin
    if($LASTEXITCODE -ne 0){throw 'Production JNI/TLS/actual server relay proof failed'}
}finally {
    if(!$server.HasExited){$server.Kill()}
    $server.WaitForExit();$stdout.GetAwaiter().GetResult()|Out-Null;$stderr.GetAwaiter().GetResult()|Out-Null;$server.Dispose()
}
