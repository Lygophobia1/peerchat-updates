param([string]$Build='build\tests\dev190-initial-attachment-identity',[string]$ServerBuild='build\tests\dev190-cache-enabled-server')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
$cert=$null
try {
 $buildPath=(Resolve-Path -LiteralPath $Build).Path
 $run=Join-Path 'F:\PeerChat-isolated-cache-tests' ('fresh-contact-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $run | Out-Null
 Copy-Item -Path (Join-Path $buildPath 'Client\*') -Destination $run
 $cache=Join-Path $run 'server-cache';New-Item -ItemType Directory -Path $cache | Out-Null
 $instance=[guid]::NewGuid().ToString('N');[IO.File]::WriteAllText((Join-Path $cache '.peerchat-attachment-cache-instance'),$instance)
 $cert=New-SelfSignedCertificate -DnsName localhost -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)
 $pin=($cert.GetCertHash([Security.Cryptography.HashAlgorithmName]::SHA256)|ForEach-Object {$_.ToString('x2')}) -join ''
 $reservation=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$reservation.Start();$port=$reservation.LocalEndpoint.Port;$reservation.Stop()
 $config=@{DataDirectory=$run;DatabasePath=(Join-Path $run 'accounts.sqlite');LogDirectory=(Join-Path $run 'server-logs');ListenAddress='127.0.0.1';Port=$port;CertificateThumbprint=$cert.Thumbprint;AttachmentCache=@{Enabled=$true;Directory=$cache;ExpectedMountPoint='F:\';ExpectedFilesystem=((& mountvol.exe F:\ /L | Out-String).Trim());InstanceId=$instance;MaximumBytes=10GB;RetentionSeconds=3600;PartialIdleSeconds=300;MinimumFreeBytes=2GB;MinimumFreePercent=20}}
 $configPath=Join-Path $run 'server-config.json';$config|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $configPath -Encoding utf8
 $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
 $refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object {'/reference:'+(Join-Path $fx $_)}
 & "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$run\FreshCachedContactTest.exe" "/reference:$run\PeerChat.Desktop.exe" $refs tests\fresh_cached_contact_test.cs
 if($LASTEXITCODE){throw 'Fresh cached contact fixture compile failed'}
 $server=Start-Process -FilePath (Join-Path (Resolve-Path -LiteralPath $ServerBuild).Path 'Server\PeerChat.Server.exe') -ArgumentList @('--config',('"'+$configPath+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'server.log') -RedirectStandardError (Join-Path $run 'server-error.log')
 try {
  Start-Sleep -Milliseconds 1000
  if($server.HasExited){throw 'Isolated cache server failed to start'}
  & "$run\FreshCachedContactTest.exe" $run localhost $pin $port 2>&1|Tee-Object -FilePath (Join-Path $run 'results.log')
  if($LASTEXITCODE){throw "Fresh cached contact live test failed; evidence $run"}
 }finally{if(-not $server.HasExited){Stop-Process -Id $server.Id};Write-Output "Evidence: $run"}
}finally{if($cert -and $cert.Thumbprint -match '^[0-9A-Fa-f]{40}$'){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$cert.Thumbprint) -ErrorAction SilentlyContinue};Pop-Location}
