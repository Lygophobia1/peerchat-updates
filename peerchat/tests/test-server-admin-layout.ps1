param([Parameter(Mandatory=$true)][string]$DevelopmentDirectory)
$ErrorActionPreference='Stop'
$adminDirectory=Join-Path $DevelopmentDirectory 'ServerAdmin'
$admin=Join-Path $adminDirectory 'PeerChat.Server.Admin.exe'
$server=Join-Path $adminDirectory 'PeerChat.Server.exe'
foreach($path in @($admin,$server)){if(-not(Test-Path -LiteralPath $path)){throw "ServerAdmin runtime file is missing: $path"}}
$runtimeRoot=Join-Path (Join-Path $PSScriptRoot '..\build\tests\runtime') ('server-admin-'+[Guid]::NewGuid().ToString('N'))
$dataRoot=Join-Path $runtimeRoot 'data'
New-Item -ItemType Directory -Force -Path (Join-Path $runtimeRoot 'config')|Out-Null
$listener=[System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
$listener.Start();$port=([System.Net.IPEndPoint]$listener.LocalEndpoint).Port;$listener.Stop()
$config=[ordered]@{DataDirectory=$dataRoot;ListenAddress='127.0.0.1';PublicHostname='localhost';Port=$port;TurnEnabled=$false}
$configPath=Join-Path $runtimeRoot 'config\server-config.json'
[System.IO.File]::WriteAllText($configPath,($config|ConvertTo-Json -Depth 4),[System.Text.UTF8Encoding]::new($false))
$previousConfig=$env:PEERCHAT_SERVER_CONFIG
try{
    $env:PEERCHAT_SERVER_CONFIG=$configPath
    $start=[System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName=$admin
    $start.Arguments='--smoke'
    $start.WorkingDirectory=$adminDirectory
    $start.UseShellExecute=$false
    $process=[System.Diagnostics.Process]::Start($start)
    if(-not $process.WaitForExit(20000)){$process.Kill();throw 'ServerAdmin smoke timed out while starting/stopping the staged server.'}
    if($process.ExitCode -ne 0){throw "ServerAdmin smoke failed with exit code $($process.ExitCode). Verify that its colocated PeerChat.Server.exe starts and answers management status."}
    Write-Host 'PASS: ServerAdmin opened, started the colocated Server, received management status, and stopped it cleanly.'
}finally{
    $env:PEERCHAT_SERVER_CONFIG=$previousConfig
}
