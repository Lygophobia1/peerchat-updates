param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\group-fanout-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$savedPath=$env:PATH
$certificate=$null
Push-Location $root
try{
    # An isolated short-lived localhost certificate; never touches the live server.
    $certificate=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N')
    $pfx=Join-Path $out 'test-server.pfx'
    Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force)|Out-Null
    & (Join-Path $fx 'csc.exe') /nologo /target:exe /platform:x64 /main:GroupCiphertextFanoutTest ("/out:"+(Join-Path $out 'group-fanout-test.exe')) ("/reference:"+(Join-Path $fx 'System.Web.Extensions.dll')) ("/reference:"+(Join-Path $fx 'System.Security.dll')) .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\PeerChat.Server.cs .\desktop\ProfileAccounts.cs .\desktop\DiscoveryClient.cs .\tests\group_roster_proof_test.cs .\tests\group_ciphertext_fanout_test.cs
    if($LASTEXITCODE -ne 0){throw 'Group fanout regression compilation failed.'}
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    & (Join-Path $out 'group-fanout-test.exe') (Join-Path $out 'state') $pfx $password
    if($LASTEXITCODE -ne 0){throw "Group fanout regression failed ($LASTEXITCODE)."}
}finally{
    $env:PATH=$savedPath;Pop-Location
    if($certificate){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)}
    if(Test-Path -LiteralPath (Join-Path $out 'test-server.pfx')){Remove-Item -LiteralPath (Join-Path $out 'test-server.pfx')}
}
