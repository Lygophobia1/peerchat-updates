param([string]$OracleDependencies = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\deps'))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\tests\go-hpke'
& (Join-Path $root 'scripts\build-go-hpke.ps1') -OutputDirectory $out
$exe=Join-Path $out 'hpke-provider-test.exe'
& 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0602 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -static-libgcc -static-libstdc++ (Join-Path $PSScriptRoot 'hpke_provider_test.cpp') (Join-Path $root 'platform\go_hpke_provider.cpp') -o $exe -lncrypt -lcrypt32
if($LASTEXITCODE -ne 0){throw "Provider build failed: $LASTEXITCODE"}
& $exe
if($LASTEXITCODE -ne 0){throw "Provider regression failed: $LASTEXITCODE"}
$stock=Join-Path $OracleDependencies 'bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
$patched=Join-Path $OracleDependencies 'bc-hpke-eacda831fbe15b272933e40841d8b0827b556fae\artifacts\4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C\peerchat-bc-hpke-1.86-external-dh-v1.jar'
if((Get-FileHash $stock).Hash -ne '2AF190B300CBB0B35E248CCF5F4A06B6072030AEB3DA7A98EC73ABE5B4CB371F' -or (Get-FileHash $patched).Hash -ne '4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C'){throw 'Pinned host oracle hash mismatch'}
$java='C:\Program Files\Java\jdk-22\bin';$cp="$stock;$patched"
& "$java\javac.exe" -encoding UTF-8 -cp $cp -d $out (Join-Path $PSScriptRoot 'tls-host\BcGoInterop.java') (Join-Path $root 'android\app\src\main\java\com\peerchat\android\platform\OpaqueRecipientHpke.java') (Join-Path $root 'android\app\src\main\java\com\peerchat\android\platform\RecipientPublicKey.java')
if($LASTEXITCODE -ne 0){throw 'Android host adapter compile failed'}
& "$java\java.exe" -cp "$out;$cp" BcGoInterop $exe
if($LASTEXITCODE -ne 0){throw 'Production Go / Android BC interop failed'}
$go=Join-Path $root 'build\go-toolchain\go\bin\go.exe'
$savedToolchain=$env:GOTOOLCHAIN
try {
 $env:GOTOOLCHAIN='local'
 Push-Location (Join-Path $root 'platform\go_hpke')
 try {
  & $go test -count=1 -v .
  if($LASTEXITCODE -ne 0){throw 'Go public/opaque API regression failed'}
  & $go test -count=1 -v crypto/hpke -run 'TestVectors/rfc9180/mode_0000_kem_0010_kdf_0001_aead_0001'
  if($LASTEXITCODE -ne 0){throw 'Official RFC 9180 vectors failed'}
 } finally {Pop-Location}
} finally {$env:GOTOOLCHAIN=$savedToolchain}
