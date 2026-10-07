param([switch]$PhysicalTurn,[string]$ServerDirectory)
$ErrorActionPreference='Stop'
# PowerShell hosts do not consistently preload the .NET Framework DPAPI type.
# The isolated server fixture deliberately protects its temporary TURN test secret.
Add-Type -AssemblyName System.Security
$projectRoot = Split-Path $PSScriptRoot -Parent
if($PhysicalTurn) {
    Push-Location $projectRoot
    try {
        & (Join-Path $projectRoot 'scripts\run-tests.ps1') -TurnInfrastructureBuildOnly
        if($LASTEXITCODE -ne 0){throw 'TURN infrastructure probe build failed'}
        $outputDirectory='build\tests\server'
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
        $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
        & "$framework\csc.exe" /nologo /target:exe /platform:x64 /optimize+ "/out:$outputDirectory\DiscoveryIntegrationTest.exe" /reference:"$framework\System.Web.Extensions.dll" .\desktop\DiscoveryClient.cs .\server\DiscoveryIntegrationTest.cs
        if($LASTEXITCODE -ne 0){throw 'Discovery TURN physical-proof harness build failed'}
        & (Join-Path $projectRoot "$outputDirectory\DiscoveryIntegrationTest.exe") --physical-turn
        if($LASTEXITCODE -ne 0){throw 'Discovery-issued TURN physical proof failed'}
    } finally { Pop-Location }
    return
}
Push-Location $projectRoot
try {
    $outputDirectory='build\tests\server'
    if($ServerDirectory){
        New-Item -ItemType Directory -Force -Path $outputDirectory|Out-Null
        $currentServer=Join-Path (Resolve-Path -LiteralPath $ServerDirectory).Path 'PeerChat.Server.exe'
        if(-not(Test-Path -LiteralPath $currentServer)){throw 'Current-worktree Server executable missing.'}
        Copy-Item -LiteralPath $currentServer -Destination (Join-Path $outputDirectory 'PeerChat.Server.exe') -Force
        foreach($runtime in @("PeerChat.Server.dll","PeerChat.Server.deps.json","PeerChat.Server.runtimeconfig.json")){if(Test-Path -LiteralPath (Join-Path $ServerDirectory $runtime)){Copy-Item -LiteralPath (Join-Path $ServerDirectory $runtime) -Destination (Join-Path $outputDirectory $runtime) -Force}}
    }else{& (Join-Path $projectRoot 'scripts\build-server.ps1') -OutputDirectory $outputDirectory}
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
    $turnSecret='PeerChat-integration-test-TURN-secret-2026'
    $turnConfig=Join-Path $PWD ($outputDirectory+'\test-server-turn-config.json')
    $invalidTurnConfig=Join-Path $PWD ($outputDirectory+'\test-server-invalid-turn-config.json')
    $protectedTurnSecret=[Convert]::ToBase64String([System.Security.Cryptography.ProtectedData]::Protect([System.Text.Encoding]::UTF8.GetBytes($turnSecret),$null,[System.Security.Cryptography.DataProtectionScope]::CurrentUser))
    function Write-TestTurnConfig([bool]$enabled,[string]$path,[string]$turnHost='turn.test.invalid') {
        [ordered]@{
            DataDirectory=(Join-Path $PWD ($outputDirectory+'\test-server-data'))
            DatabasePath=$database
            LogDirectory=(Join-Path $PWD ($outputDirectory+'\test-server-logs'))
            CertificateThumbprint=$cert.Thumbprint
            TurnEnabled=$enabled
            TurnPublicHost=$turnHost
            TurnUdpPort=3478
            TurnCredentialTtlSeconds=600
            TurnRealm='peerchat-test'
            TurnRestSharedSecretProtected=$protectedTurnSecret
        } | ConvertTo-Json | Set-Content -LiteralPath $path -Encoding UTF8
    }
    # Start-Process flattens an argument array. Quote the database path because this workspace has spaces.
    $managementPipe='PeerChatDiscoveryTest-'+[Guid]::NewGuid().ToString('N')
    $serverExecutable=Join-Path $PWD ($outputDirectory+'\PeerChat.Server.exe')
    $testExecutable=Join-Path $PWD ($outputDirectory+'\DiscoveryIntegrationTest.exe')
    $previousTurnTestSecret=$env:PEERCHAT_TEST_TURN_SECRET;$env:PEERCHAT_TEST_TURN_SECRET=$turnSecret
    Write-TestTurnConfig $false $turnConfig
    $disabledArguments="--config `"$turnConfig`" --port 17443 --database `"$database`" --certificate-thumbprint $($cert.Thumbprint) --management-pipe $managementPipe"
    $startDisabledServer={Start-Process -FilePath $serverExecutable -ArgumentList $disabledArguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $serverLog -RedirectStandardError $serverError}
    $server=& $startDisabledServer;Start-Sleep -Milliseconds 700
    if($server.HasExited){throw "Disabled TURN discovery server exited during startup: $(Get-Content -Raw $serverError)"}
    try { & $testExecutable localhost $fingerprint 17443 --turn-disabled;if($LASTEXITCODE -ne 0){throw 'Disabled TURN integration test failed'} } finally { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    Write-TestTurnConfig $true $turnConfig
    Write-TestTurnConfig $true $invalidTurnConfig 'bad host'
    & $serverExecutable --config $invalidTurnConfig
    if($LASTEXITCODE -eq 0){throw 'Malformed TURN configuration unexpectedly started the discovery server'}
    $serverArguments="--config `"$turnConfig`" --port 17443 --database `"$database`" --certificate-thumbprint $($cert.Thumbprint) --management-pipe $managementPipe"
    $startServer={Start-Process -FilePath $serverExecutable -ArgumentList $serverArguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $serverLog -RedirectStandardError $serverError}
    $server=& $startServer;Start-Sleep -Milliseconds 700
    if($server.HasExited){throw "Discovery server exited during startup: $(Get-Content -Raw $serverError)"}
    try {
        & $testExecutable localhost $fingerprint 17443 $managementPipe
        if($LASTEXITCODE -ne 0){throw 'Discovery integration test failed'}
        if(-not (Select-String -LiteralPath $serverLog -SimpleMatch -Pattern 'direct candidates=False' -Quiet)){throw 'Relay-only integration clients did not publish direct candidates=False'}
        if(Select-String -LiteralPath $serverLog -SimpleMatch -Pattern 'Server-observed Internet address' -Quiet){throw 'Relay-only integration clients caused a server-observed direct candidate'}
    } finally { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    $server=& $startServer;Start-Sleep -Milliseconds 700
    if($server.HasExited){throw "Discovery server restart failed: $(Get-Content -Raw $serverError)"}
    try {
        & $testExecutable localhost $fingerprint 17443 --after-restart;if($LASTEXITCODE -ne 0){throw 'Discovery restart integration test failed'}
    } finally { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    if((Get-Content -Raw -LiteralPath $turnConfig).Contains($turnSecret)){throw 'TURN shared secret was persisted in plaintext in the server test configuration'}
    if(Select-String -LiteralPath $serverLog,$serverError -SimpleMatch -Pattern $turnSecret -Quiet){throw 'TURN shared secret appeared in discovery server test output'}
} finally { if($null -eq $previousTurnTestSecret){Remove-Item Env:PEERCHAT_TEST_TURN_SECRET -ErrorAction SilentlyContinue}else{$env:PEERCHAT_TEST_TURN_SECRET=$previousTurnTestSecret}; Pop-Location }
