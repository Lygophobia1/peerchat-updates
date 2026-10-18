param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\device-manager-adapter-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$state=Join-Path $env:TEMP ('pc-dm-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $state | Out-Null
$client=(Resolve-Path -LiteralPath $ClientDirectory).Path
Copy-Item -Path "$client\*" -Destination $out
$certificate=$null
try {
    $certificate=New-SelfSignedCertificate -DnsName localhost -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N');$pfx=Join-Path $out 'server.pfx'
    Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force) | Out-Null
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $csc=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
    if(!(Test-Path -LiteralPath $csc)){$csc=Join-Path $fx 'csc.exe'}
    $sources=Get-ChildItem -LiteralPath (Join-Path $root 'server') -Filter *.cs | Where-Object {$_.Name -notin @('DiscoveryIntegrationTest.cs','PeerChat.Server.Admin.cs')} | ForEach-Object FullName
    $refs=@('System.dll','System.Core.dll','System.Web.Extensions.dll','System.Security.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
    & $csc /nologo /target:exe /platform:x64 /main:DeviceManagerAdapterTest "/out:$out\device-manager-adapter.exe" "/reference:$out\PeerChat.Desktop.exe" $refs @sources "$PSScriptRoot\device_manager_adapter_test.cs"
    if($LASTEXITCODE -ne 0){throw 'Device Manager adapter fixture compilation failed'}
    & "$out\device-manager-adapter.exe" $state $pfx $password
    if($LASTEXITCODE -ne 0){throw 'Device Manager adapter fixture failed'}
    Write-Host "Evidence: $out"
    Write-Host "Isolated server/profile evidence: $state"
}finally{
    if($certificate){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)}
    if(Test-Path -LiteralPath (Join-Path $out 'server.pfx')){Remove-Item -LiteralPath (Join-Path $out 'server.pfx')}
}
