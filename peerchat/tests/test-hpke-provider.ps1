param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
# Reuse the checksum/source-verified evaluation cache and independent oracles.
& (Join-Path $PSScriptRoot 'test-wolfcrypt-cng-evaluation.ps1')
$evaluation=Join-Path $root 'build\deps\wolfcrypt-evaluation'
$source=Join-Path $evaluation 'wolfssl-3c5eead44904df64e6a5a1f4ebdce377d35a849a'
$native=Join-Path $evaluation 'mingw-x64'
$exe=Join-Path $evaluation 'hpke-provider-test.exe'
& 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -DWOLFCRYPT_ONLY -DNO_CERTS -DPEERCHAT_HPKE_EVALUATION -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I $native -I $source (Join-Path $PSScriptRoot 'hpke_provider_test.cpp') (Join-Path $root 'platform\wolfcrypt_evaluation_hpke.cpp') (Join-Path $native 'libwolfssl.a') -o $exe -lncrypt -lcrypt32
if($LASTEXITCODE -ne 0){throw "Provider build failed: $LASTEXITCODE"}
& $exe
if($LASTEXITCODE -ne 0){throw "Provider regression failed: $LASTEXITCODE"}
$stock=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
$patched=Join-Path $root 'build\deps\bc-hpke-eacda831fbe15b272933e40841d8b0827b556fae\artifacts\4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C\peerchat-bc-hpke-1.86-external-dh-v1.jar'
& 'C:\Program Files\Java\jdk-22\bin\java.exe' -cp "$evaluation;$stock;$patched" WolfCryptCngEvaluation $exe --interop
if($LASTEXITCODE -ne 0){throw "Hardened provider interoperability failed: $LASTEXITCODE"}
