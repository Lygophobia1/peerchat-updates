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
    & dotnet build tests/portable/PeerChat.DeviceIdentityEndorsementTests.csproj -c Release --nologo -v quiet "-p:BaseIntermediateOutputPath=$out/obj/" "-p:MSBuildProjectExtensionsPath=$out/obj/" "-p:OutputPath=$out/managed/"
    if($LASTEXITCODE){throw 'Device identity server parser compilation failed'}
    & dotnet "$out/managed/PeerChat.DeviceIdentityEndorsementTests.dll" "$out/profiles/endorsement.bin"
    if($LASTEXITCODE){throw 'Device identity endorsement server interoperability failed'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/device_identity_application_test.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -lncrypt -o "$out/application.exe"
    if($LASTEXITCODE){throw 'Device identity application compilation failed'}
    & "$out/application.exe" "$out/application-profiles"
    if($LASTEXITCODE){throw 'Device identity application regression failed'}
    Write-Output "Evidence: $out"
}finally{Pop-Location}
