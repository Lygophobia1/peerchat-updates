param([string]$ApplicationDirectory='releases\v0.3-beta\Client')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $outputDirectory='build\tests\friend-ui-'+[Guid]::NewGuid().ToString('N')
    $dataA=Join-Path $outputDirectory 'alpha';$dataB=Join-Path $outputDirectory 'beta'
    New-Item -ItemType Directory -Force -Path $outputDirectory,$dataA,$dataB | Out-Null
    & .\scripts\build-server.ps1 -OutputDirectory $outputDirectory
    Copy-Item "$ApplicationDirectory\PeerChat.Desktop.exe","$ApplicationDirectory\PeerChat.Core.dll","$ApplicationDirectory\PeerChat.Desktop.exe.config","$ApplicationDirectory\peerchat-client.ico","$ApplicationDirectory\PeerChat.Updater.Core.dll","$ApplicationDirectory\PeerChat.Updater.exe","$ApplicationDirectory\PeerChat.Updater.exe.config","$ApplicationDirectory\update-feeds.json" $outputDirectory -Force
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:'+(Join-Path $framework $_) }
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$outputDirectory\FriendUiSmoke.exe" "/reference:$outputDirectory\PeerChat.Desktop.exe" $references .\tests\FriendUiSmoke.cs
    if($LASTEXITCODE -ne 0){throw 'Friend UI smoke build failed'}
    try {$cert=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1)}
    catch {throw "Could not create temporary test TLS certificate. $($_.Exception.Message)"}
    $fingerprint=($cert.GetCertHash([System.Security.Cryptography.HashAlgorithmName]::SHA256)|ForEach-Object {$_.ToString('X2')}) -join ''
    $database=Join-Path $outputDirectory 'friend-ui.sqlite';$serverLog=Join-Path $outputDirectory 'server.log';$serverError=Join-Path $outputDirectory 'server-error.log'
    $server=Start-Process -WindowStyle Hidden -PassThru -FilePath (Join-Path $outputDirectory 'PeerChat.Server.exe') -ArgumentList "--port 18443 --database `"$database`" --certificate-thumbprint $($cert.Thumbprint)" -RedirectStandardOutput $serverLog -RedirectStandardError $serverError
    Start-Sleep -Milliseconds 750
    if($server.HasExited){throw "Friend UI test server failed to start: $(Get-Content -Raw $serverError)"}
    try {& (Join-Path $outputDirectory 'FriendUiSmoke.exe') $dataA $dataB localhost $fingerprint 18443;if($LASTEXITCODE -ne 0){throw 'Friend UI smoke test failed'}} finally {Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue}
} finally {Pop-Location}
