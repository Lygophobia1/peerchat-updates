param()
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-source-recovery-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_attachment_source_recovery_test.cpp group.cpp -o "$out/recovery.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Source recovery compile failed'}
 & "$out/recovery.exe" "$out/profiles"
 if($LASTEXITCODE){throw 'Source recovery regression failed'}
 Write-Output "Evidence: $out. Isolated native storage/process fixtures; no phone/Pi access."
}finally{Pop-Location}
