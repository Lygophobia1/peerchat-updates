$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $root ('build/tests/download-save-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Push-Location $root
try {
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/windows_download_save_test.cpp -o "$run/save.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Download save fixture compile failed'}
 $fixture=Join-Path ([IO.Path]::GetTempPath()) ('peerchat-save-'+[Guid]::NewGuid().ToString('N'))
 & "$run/save.exe" $fixture
 if($LASTEXITCODE){throw 'Download save regression failed'}
 Write-Output "Evidence: $run; isolated fixture: $fixture"
} finally {Pop-Location}
