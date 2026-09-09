param([int]$Port=7443,[string]$DatabasePath='build\v0.3-beta\Server\peerchat-server.sqlite')
$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & (Join-Path $PSScriptRoot 'build-server.ps1')
    $certificate=Get-ChildItem Cert:\CurrentUser\My | Where-Object {$_.Subject -eq 'CN=PeerChat Development Discovery Server' -and $_.NotAfter -gt (Get-Date).AddDays(7) -and $_.HasPrivateKey} | Sort-Object NotAfter -Descending | Select-Object -First 1
    if(!$certificate){$certificate=New-SelfSignedCertificate -DnsName 'localhost','PeerChat Development Discovery Server' -FriendlyName 'PeerChat Development Discovery Server' -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(1)}
    $fingerprint=($certificate.GetCertHash([System.Security.Cryptography.HashAlgorithmName]::SHA256)|ForEach-Object {$_.ToString('X2')}) -join ''
    $serverOutput=Join-Path $projectRoot 'build\v0.3-beta\Server'
    $databaseFullPath=if([System.IO.Path]::IsPathRooted($DatabasePath)){$DatabasePath}else{Join-Path $projectRoot $DatabasePath}
    $pinFile=Join-Path $serverOutput 'discovery-server-sha256.txt';Set-Content -LiteralPath $pinFile -Value $fingerprint -NoNewline
    Start-Process -FilePath (Join-Path $serverOutput 'PeerChat.Server.exe') -ArgumentList "--port $Port --database `"$databaseFullPath`" --certificate-thumbprint $($certificate.Thumbprint)" -WindowStyle Hidden
    Write-Host "PeerChat discovery server started on TLS port $Port."
    Write-Host "Client certificate SHA-256 fingerprint: $fingerprint"
    Write-Host "The same fingerprint is saved to $pinFile"
} finally { Pop-Location }
