$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $outputDirectory='build\tests\server'
    & (Join-Path $projectRoot 'scripts\build-server.ps1') -OutputDirectory $outputDirectory
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:'+(Join-Path $framework $_) }
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 /optimize+ "/out:$outputDirectory\DiscoveryIntegrationTest.exe" /reference:"$framework\System.Web.Extensions.dll" .\desktop\DiscoveryClient.cs .\server\DiscoveryIntegrationTest.cs
    if($LASTEXITCODE -ne 0){throw 'Discovery test build failed'}
    # Some Windows installations fail CertEnroll certificate creation (0x80070002). Reuse the
    # configured development certificate in that case; the test database remains isolated.
    try { $cert=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyExportPolicy Exportable -NotAfter (Get-Date).AddDays(1) }
    catch {
        $configPath=Join-Path $env:ProgramData 'PeerChat\Server\Config\server-config.json'
        $configured=if(Test-Path -LiteralPath $configPath){(Get-Content -Raw -LiteralPath $configPath|ConvertFrom-Json).CertificateThumbprint}else{''}
        $cert=Get-ChildItem -Path 'Cert:\CurrentUser\My' | Where-Object {$_.Thumbprint -eq $configured -and $_.HasPrivateKey} | Select-Object -First 1
        if($null -eq $cert){throw "Could not create a test certificate or find the configured development certificate. $($_.Exception.Message)"}
        Write-Host 'Reusing configured development certificate for isolated discovery test.'
    }
    $fingerprint=($cert.GetCertHash([System.Security.Cryptography.HashAlgorithmName]::SHA256)|ForEach-Object {$_.ToString('X2')}) -join ''
    $database=Join-Path $PWD ($outputDirectory+'\test-server-'+[Guid]::NewGuid().ToString('N')+'.sqlite')
    $serverLog=Join-Path $PWD ($outputDirectory+'\test-server.log');$serverError=Join-Path $PWD ($outputDirectory+'\test-server-error.log')
    # Start-Process flattens an argument array. Quote the database path because this workspace has spaces.
    $managementPipe='PeerChatDiscoveryTest-'+[Guid]::NewGuid().ToString('N')
    $serverArguments="--port 17443 --database `"$database`" --certificate-thumbprint $($cert.Thumbprint) --management-pipe $managementPipe"
    $serverExecutable=Join-Path $PWD ($outputDirectory+'\PeerChat.Server.exe')
    $testExecutable=Join-Path $PWD ($outputDirectory+'\DiscoveryIntegrationTest.exe')
    $startServer={Start-Process -FilePath $serverExecutable -ArgumentList $serverArguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $serverLog -RedirectStandardError $serverError}
    $server=& $startServer;Start-Sleep -Milliseconds 700
    if($server.HasExited){throw "Discovery server exited during startup: $(Get-Content -Raw $serverError)"}
    try { & $testExecutable localhost $fingerprint 17443 $managementPipe;if($LASTEXITCODE -ne 0){throw 'Discovery integration test failed'} } finally { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    $server=& $startServer;Start-Sleep -Milliseconds 700
    if($server.HasExited){throw "Discovery server restart failed: $(Get-Content -Raw $serverError)"}
    try {
        & $testExecutable localhost $fingerprint 17443 --after-restart;if($LASTEXITCODE -ne 0){throw 'Discovery restart integration test failed'}
    } finally { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
} finally { Pop-Location }
