param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/device-management-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$cert=$null
Push-Location $root
try {
    $producer=Get-ChildItem -Path 'build/tests/mailbox-protocol-*/mailbox-protocol-test.exe' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if(!$producer){throw 'Run test-mailbox-protocol.ps1 first to build canonical fixture producer'}
    $fixture=Join-Path $out 'mailbox'
    & $producer.FullName $fixture
    if($LASTEXITCODE){throw 'Canonical mailbox fixture failed'}
    $cert=New-SelfSignedCertificate -DnsName localhost -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N');$pfx=Join-Path $out 'test.pfx'
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force) | Out-Null
    $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
    $sources=Get-ChildItem server -Filter '*.cs' -File | Where-Object {$_.Name -notin @('PeerChat.Server.Admin.cs','DiscoveryIntegrationTest.cs')} | ForEach-Object {$_.FullName}
    & "$fx/csc.exe" /nologo /target:exe /platform:x64 /main:DeviceManagementServerTest "/out:$out/test.exe" "/reference:$fx/System.Web.Extensions.dll" "/reference:$fx/System.Security.dll" .\shared\VersionInfo.cs $sources .\tests\device_management_server_test.cs
    if($LASTEXITCODE){throw 'Device management fixture compilation failed'}
    & "$out/test.exe" "$out/state" $pfx $password $fixture
    if($LASTEXITCODE){throw 'Device management security regression failed'}
    Write-Output "Evidence: $out"
} finally {
    Pop-Location
    if($cert){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$cert.Thumbprint)}
    if(Test-Path -LiteralPath (Join-Path $out 'test.pfx')){Remove-Item -LiteralPath (Join-Path $out 'test.pfx')}
}
