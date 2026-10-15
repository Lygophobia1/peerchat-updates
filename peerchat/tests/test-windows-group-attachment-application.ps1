param([string]$FixtureRoot='F:\PeerChat-isolated-cache-tests',[string]$CandidateServer)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $FixtureRoot ('application-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence|Out-Null
Push-Location $sourceRoot
try {
 $previousCandidate=$env:PEERCHAT_TEST_CACHE_CANDIDATE_SERVER
 if($CandidateServer){$env:PEERCHAT_TEST_CACHE_CANDIDATE_SERVER=(Resolve-Path -LiteralPath $CandidateServer).Path}
 & dotnet build tests/portable/PeerChat.AttachmentApplicationHost.csproj -c Release --nologo -v quiet
 if($LASTEXITCODE){throw 'Application host compile failed'}
 & scripts/build-go-hpke.ps1 -OutputDirectory $evidence
 & g++ -std=c++17 -O1 -Wall -Wextra -Werror -Wno-cast-function-type -Wno-misleading-indentation -static-libgcc -static-libstdc++ -I. tests/windows_group_attachment_application_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp group_message_envelope.cpp -o "$evidence/application.exe" -lbcrypt -lcrypt32 -lncrypt -lws2_32
 if($LASTEXITCODE){throw 'Native Application fixture compile failed'}
 & "$evidence/application.exe" "$evidence/fixtures" (Resolve-Path tests/portable/bin/Release/net10.0/PeerChat.AttachmentApplicationHost.dll).Path 2>&1 | Tee-Object -FilePath "$evidence/results.log"
 if($LASTEXITCODE){throw 'Production Application integration fixture failed'}
 Write-Output "Evidence: $evidence. Isolated production-path validation only; no deployment, no shipping enablement."
} finally {$env:PEERCHAT_TEST_CACHE_CANDIDATE_SERVER=$previousCandidate;Pop-Location}
