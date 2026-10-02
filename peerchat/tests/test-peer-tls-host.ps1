param([string]$JavaHome='C:\Program Files\Java\jdk-22')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$deps=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on'
$pins=@{
    'bcprov-jdk18on-1.86.jar'='2af190b300cbb0b35e248ccf5f4a06b6072030aeb3da7a98ec73abe5b4cb371f'
    'bcutil-jdk18on-1.86.jar'='1c268e15f785aafb1e4670d8d6e5f856f69475e96c162dadcd1ff9b350e9139f'
    'bcpkix-jdk18on-1.86.jar'='8d8b41a4b149bdae8d331f059a578870954d1e3ff29f8269a9eb3ee19a7e4ef7'
    'bctls-jdk18on-1.86.1.jar'='5ac3270a777fc6ebbfa38be8ffba24b1b45bb0aafefe1213a5771fb7eb5ecae5'
}
$jars=@($pins.Keys|Sort-Object|ForEach-Object {
    $path=Join-Path $deps $_
    if(-not(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $pins[$_]) {throw "Missing/invalid pinned TLS artifact: $_"}
    $path
})
$out=Join-Path $root ('build\tests\tls-host-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$classpath=$jars -join ';'
Push-Location $root
try {
    & "$JavaHome\bin\javac.exe" -encoding UTF-8 -cp $classpath -d $out android/app/src/main/java/com/peerchat/android/platform/tls/OpaqueTlsSigner.java android/app/src/main/java/com/peerchat/android/platform/tls/PeerTls.java tests/tls-host/SigningProof.java tests/tls-host/PeerTlsProof.java tests/tls-host/NativeTlsProof.java
    if($LASTEXITCODE -ne 0){throw 'TLS proof compilation failed'}
    & "$JavaHome\bin\java.exe" -cp "$out;$classpath" SigningProof "$out\compatibility.bin"
    if($LASTEXITCODE -ne 0){throw 'Signing proof failed'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/android_identity_compat_test.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -o "$out\windows-verifier.exe"
    if($LASTEXITCODE -ne 0){throw 'Windows identity verifier compilation failed'}
    & "$out\windows-verifier.exe" "$out\compatibility.bin"
    if($LASTEXITCODE -ne 0){throw 'Original-message signature failed unchanged Windows verifier'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -D__ANDROID__ -I. tests/native_tls_provider_test.cpp portable/tls_android.cpp -o "$out\native-tls-provider.exe"
    if($LASTEXITCODE -ne 0){throw 'Native TLS adapter compilation failed'}
    & "$out\native-tls-provider.exe"
    if($LASTEXITCODE -ne 0){throw 'Native TLS adapter regression failed'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -D__ANDROID__ -shared -static-libgcc -static-libstdc++ -I. ("-I$JavaHome\include") ("-I$JavaHome\include\win32") tests/tls_jni_host.cpp portable/tls_android.cpp -o "$out\tls-provider-proof.dll"
    if($LASTEXITCODE -ne 0){throw 'Native TLS JNI proof compilation failed'}
    & "$JavaHome\bin\java.exe" -Xcheck:jni "-Djava.library.path=$out" -cp "$out;$classpath" com.peerchat.android.platform.tls.NativeTlsProof
    if($LASTEXITCODE -ne 0){throw 'Native TLS JNI integration proof failed'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/peer_tls_interop.cpp tls.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -lncrypt -lsecur32 -o "$out\schannel-interop.exe"
    if($LASTEXITCODE -ne 0){throw 'Unchanged Schannel fixture compilation failed'}
    & "$JavaHome\bin\java.exe" -cp "$out;$classpath" com.peerchat.android.platform.tls.PeerTlsProof "$out\schannel-interop.exe" "$out\profile"
    if($LASTEXITCODE -ne 0){throw 'TLS handshake proof failed'}
} finally {Pop-Location}
