param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\tests\device-keys'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $root
try {
    foreach($test in @('device_key_manager_test','protected_key_registry_test','windows_recipient_key_test')) {
        & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. "tests\$test.cpp" -o "$out\$test.exe" -lcrypt32 -lncrypt
        if($LASTEXITCODE -ne 0){throw "$test compile failed ($LASTEXITCODE)"}
        & "$out\$test.exe"
        if($LASTEXITCODE -ne 0){throw "$test failed ($LASTEXITCODE)"}
    }
    $jar=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
    if((Get-FileHash -LiteralPath $jar -Algorithm SHA256).Hash -ne '2AF190B300CBB0B35E248CCF5F4A06B6072030AEB3DA7A98EC73ABE5B4CB371F'){throw 'Pinned HPKE artifact changed'}
    $java='C:\Program Files\Java\jdk-22\bin'
    & "$java\javac.exe" -encoding UTF-8 -cp $jar -d $out android/app/src/main/java/com/peerchat/android/platform/RecipientPublicKey.java tests/tls-host/RecipientHpkeStandardProof.java
    if($LASTEXITCODE -ne 0){throw 'Standard HPKE proof compilation failed'}
    & "$java\java.exe" -cp "$out;$jar" RecipientHpkeStandardProof "$out\windows_recipient_key_test.exe"
    if($LASTEXITCODE -ne 0){throw 'Standard HPKE/CNG interoperability proof failed'}
} finally { Pop-Location }
