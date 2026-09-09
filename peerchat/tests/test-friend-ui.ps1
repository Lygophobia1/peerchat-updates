$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $outputDirectory='build\tests\friend-ui'
    $dataA=Join-Path $outputDirectory 'alpha';$dataB=Join-Path $outputDirectory 'beta'
    Remove-Item -LiteralPath $outputDirectory -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $outputDirectory,$dataA,$dataB | Out-Null
    & .\scripts\build-server.ps1 -OutputDirectory $outputDirectory
    Copy-Item releases\v0.3-beta\Client\PeerChat.Desktop.exe,releases\v0.3-beta\Client\PeerChat.Core.dll,releases\v0.3-beta\Client\PeerChat.Desktop.exe.config,releases\v0.3-beta\Client\peerchat-client.ico $outputDirectory -Force
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
