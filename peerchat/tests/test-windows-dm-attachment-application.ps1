param([string]$FixtureRoot='F:\PeerChat-isolated-cache-tests',[string]$CandidateServer)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $FixtureRoot ('dm-application-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence|Out-Null
Push-Location $sourceRoot
try {
 $dmPreviousCandidate=$env:PEERCHAT_TEST_DM_CACHE_SERVER
 if($CandidateServer){$env:PEERCHAT_TEST_DM_CACHE_SERVER=(Resolve-Path -LiteralPath $CandidateServer).Path}
 & dotnet build tests/portable/PeerChat.DirectAttachmentApplicationHost.csproj -c Release --nologo -v quiet
 if($LASTEXITCODE){throw 'DM Application TLS host compile failed'}
 & scripts/build-go-hpke.ps1 -OutputDirectory $evidence
 & g++ -std=c++17 -O1 -Wall -Wextra -Werror -Wno-cast-function-type -Wno-misleading-indentation -static-libgcc -static-libstdc++ -I. tests/windows_dm_attachment_application_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp group_message_envelope.cpp -o "$evidence/dm-application.exe" -lbcrypt -lcrypt32 -lncrypt -lws2_32
 if($LASTEXITCODE){throw 'Native Windows DM Application fixture compile failed'}
 & "$evidence/dm-application.exe" "$evidence/fixtures" (Resolve-Path tests/portable/bin/Release/net10.0/PeerChat.DirectAttachmentApplicationHost.dll).Path 2>&1 | Tee-Object -FilePath "$evidence/results.log"
 if($LASTEXITCODE){throw 'Production Windows DM Application/TLS integration failed'}
 Write-Output "Evidence: $evidence. Isolated production paths; no deployment."
}finally{$env:PEERCHAT_TEST_DM_CACHE_SERVER=$dmPreviousCandidate;Pop-Location}
