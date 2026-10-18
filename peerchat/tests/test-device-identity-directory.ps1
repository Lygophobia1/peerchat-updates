param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/device-identity-directory-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/device_identity_directory_test.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -lncrypt -o "$out/directory.exe"
    if($LASTEXITCODE){throw 'Device identity directory compilation failed'}
    & "$out/directory.exe" "$out/profiles"
    if($LASTEXITCODE){throw 'Device identity directory regression failed'}
    & dotnet run --project tests/portable/PeerChat.DeviceIdentityEndorsementTests.csproj -c Release -- "$out/profiles/endorsement.bin"
    if($LASTEXITCODE){throw 'Device identity endorsement server interoperability failed'}
    Write-Output "Evidence: $out"
}finally{Pop-Location}
