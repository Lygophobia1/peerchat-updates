param([string]$MailboxFixture)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/eligibility-snapshot-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    if(!$MailboxFixture){
        $producer=Get-ChildItem -Path 'build/tests/mailbox-protocol-*/mailbox-protocol-test.exe' |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        if(!$producer){throw 'Run test-mailbox-protocol.ps1 to build the canonical native fixture producer'}
        $MailboxFixture=Join-Path $out 'mailbox'
        & $producer.FullName $MailboxFixture
        if($LASTEXITCODE){throw 'Canonical signed mailbox fixture generation failed'}
    }
    $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
    $sources=Get-ChildItem server -Filter '*.cs' -File |
        Where-Object {$_.Name -notin @('PeerChat.Server.Admin.cs','DiscoveryIntegrationTest.cs')} |
        ForEach-Object {$_.FullName}
    & "$fx/csc.exe" /nologo /target:exe /platform:x64 /main:MailboxEligibilitySnapshotServerTest "/out:$out/test.exe" "/reference:$fx/System.Web.Extensions.dll" .\shared\VersionInfo.cs $sources .\tests\mailbox_eligibility_snapshot_server_test.cs
    if($LASTEXITCODE){throw 'Eligibility snapshot fixture compilation failed'}
    & "$out/test.exe" $out $MailboxFixture
    if($LASTEXITCODE){throw 'Eligibility snapshot security/reuse regression failed'}
    Write-Output "Evidence: $out"
}finally{Pop-Location}
