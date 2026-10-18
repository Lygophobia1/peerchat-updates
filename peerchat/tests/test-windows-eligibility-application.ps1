param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if([string]::IsNullOrWhiteSpace($FixtureRoot)){$FixtureRoot=Join-Path $env:TEMP ('pc-eligibility-'+[Guid]::NewGuid().ToString('N'))}
$out=[System.IO.Path]::GetFullPath($FixtureRoot)
if([System.IO.Path]::GetPathRoot($out) -eq $out){throw 'An isolated fixture directory is required.'}
New-Item -ItemType Directory -Force -Path $out | Out-Null
& (Join-Path $root 'scripts\build-go-hpke.ps1') -OutputDirectory $out
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$references=@('System.dll','System.Core.dll','System.Web.Extensions.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
Push-Location $root
try {
    & $compiler -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/windows_eligibility_application_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp -o "$out\eligibility-native.exe" -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Native eligibility application fixture build failed'}
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:MailboxEligibilityApplicationHost "/out:$out\eligibility-host.exe" $references /reference:System.Net.Http.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DeviceNameMetadata.cs .\desktop\DiscoveryClient.cs .\desktop\ProfileAccounts.cs .\desktop\MailboxExecution.cs .\tests\mailbox_eligibility_application_host.cs
    if($LASTEXITCODE -ne 0){throw 'Managed authenticated eligibility host build failed'}
    $certificate=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N');$pfx=Join-Path $out 'test-server.pfx'
    try {
        Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force)|Out-Null
        & "$out\eligibility-host.exe" "$out\profiles" "$out\eligibility-native.exe" $pfx $password
        if($LASTEXITCODE -ne 0){throw 'Authenticated eligibility application regression failed'}
        Write-Host "Evidence: $out"
    } finally {
        Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)
        if(Test-Path -LiteralPath $pfx){Remove-Item -LiteralPath $pfx}
    }
} finally {Pop-Location}
