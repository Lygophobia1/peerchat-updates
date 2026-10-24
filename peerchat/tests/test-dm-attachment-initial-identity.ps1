param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root ('build/tests/cache-initial-identity-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
Push-Location $root
try {
 & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -O2 -Wall -Wextra -Werror -I. tests/dm_attachment_initial_identity_test.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -lncrypt -o "$OutputDirectory/identity.exe"
 if($LASTEXITCODE){throw 'Initial cached identity regression compilation failed'}
 & "$OutputDirectory/identity.exe" "$OutputDirectory/profiles"
 if($LASTEXITCODE){throw 'Initial cached identity regression failed'}
}finally{Pop-Location}
