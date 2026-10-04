param([string]$JavaHome='C:\Program Files\Java\jdk-22')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$jar=& (Join-Path $root 'scripts\build-bc-hpke-hook.ps1') -JavaHome $JavaHome
$stock=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
$out=Join-Path $root ('build\tests\opaque-hpke-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ "-I$JavaHome\include" "-I$JavaHome\include\win32" tests\recipient_key_jni_host.cpp -o "$out\recipient-key-proof.dll" -lncrypt
    if($LASTEXITCODE -ne 0){throw 'CNG opaque JNI fixture compile failed'}
    & "$JavaHome\bin\javac.exe" -encoding UTF-8 -cp "$jar;$stock" -d $out tests\tls-host\NativeCngHpkeFixture.java tests\tls-host\OpaqueHpkeHookProof.java
    if($LASTEXITCODE -ne 0){throw 'Opaque HPKE proof compile failed'}
    & "$JavaHome\bin\java.exe" -Xcheck:jni "-Djava.library.path=$out" -cp "$out;$jar;$stock" OpaqueHpkeHookProof
    if($LASTEXITCODE -ne 0){throw 'Opaque HPKE proof failed'}
    Get-FileHash -LiteralPath $jar -Algorithm SHA256
} finally {Pop-Location}
