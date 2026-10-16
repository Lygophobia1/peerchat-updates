param([string]$JavaHome='C:\Program Files\Java\jdk-22',[string]$ServerDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\reliable-profile-jni-'+[Guid]::NewGuid().ToString('N'))
$deps=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on'
$pins=@{
    'bcprov-jdk18on-1.86.jar'='2af190b300cbb0b35e248ccf5f4a06b6072030aeb3da7a98ec73abe5b4cb371f'
    'bcutil-jdk18on-1.86.jar'='1c268e15f785aafb1e4670d8d6e5f856f69475e96c162dadcd1ff9b350e9139f'
    'bcpkix-jdk18on-1.86.jar'='8d8b41a4b149bdae8d331f059a578870954d1e3ff29f8269a9eb3ee19a7e4ef7'
    'bctls-jdk18on-1.86.1.jar'='5ac3270a777fc6ebbfa38be8ffba24b1b45bb0aafefe1213a5771fb7eb5ecae5'
}
$classpath=(@($pins.Keys|Sort-Object|ForEach-Object{
    $path=Join-Path $deps $_
    if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $pins[$_]){throw "Missing/invalid pinned TLS artifact: $_"}
    $path
}) -join ';')
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    # Build a test-only source mirror. The ONLY source substitution is this
    # Android POSIX attachment include; messaging, auth, ACK and Storage stay
    # byte-for-byte production code. Fail if the source changed unexpectedly.
    $hostPortable=Join-Path $out 'portable'
    $hostTests=Join-Path $out 'tests'
    New-Item -ItemType Directory -Path $hostPortable,$hostTests|Out-Null
    $corePath=Join-Path $root 'portable\peerchat_core.cpp'
    $before=(Get-FileHash -LiteralPath $corePath -Algorithm SHA256).Hash
    $core=[IO.File]::ReadAllText($corePath)
    $original='#include "attachment_file_io.hpp"'
    if(([regex]::Matches($core,[regex]::Escape($original))).Count -ne 1){throw 'Expected exactly one Android attachment include in production core'}
    $core=$core.Replace($original,'#include "attachment_file_io_unavailable.hpp"')
    if((Get-FileHash -LiteralPath $corePath -Algorithm SHA256).Hash -ne $before){throw 'Production core changed during host fixture preparation'}
    [IO.File]::WriteAllText((Join-Path $hostPortable 'peerchat_core.cpp'),$core)
    Copy-Item -LiteralPath 'tests\portable\attachment_file_io_unavailable.hpp' -Destination $hostPortable
    Copy-Item -LiteralPath 'tests\reliable_jni_storage_host.cpp' -Destination $hostTests
    Copy-Item -LiteralPath 'storage.cpp' -Destination $out
    Write-Host 'JNI messaging host proof uses fail-closed attachment and voice adapters; Android POSIX I/O, Keystore, media and OS lifecycle are not covered.'
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -Wno-error=misleading-indentation -shared -static-libgcc -static-libstdc++ -D_WIN32_WINNT=0x0601 -D__ANDROID__ -DPEERCHAT_PORTABLE_TEXT '-I.' '-Itests' '-Iportable' '-include' 'windows.h' ("-I$JavaHome\include") ("-I$JavaHome\include\win32") (Join-Path $hostTests 'reliable_jni_storage_host.cpp') portable/tls_android.cpp group.cpp group_message_envelope.cpp android/voice-media/media_capabilities.cpp tests/portable/voice_media_unavailable.cpp -o "$out\peerchat_messaging.dll" ("-Wl,--out-implib,$out\libpeerchat_messaging.a") -lbcrypt -lcrypt32 2> "$out\compile.stderr.txt"
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath "$out\compile.stderr.txt" -TotalCount 50|Write-Host;throw 'Reliable production JNI host fixture compilation failed'}
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ '-Iportable' ("-I$JavaHome\include") ("-I$JavaHome\include\win32") android/app/src/main/cpp/peerchat_jni.cpp ("-L$out") -lpeerchat_messaging -o "$out\peerchat_jni.dll"
    if($LASTEXITCODE -ne 0){throw 'Foundation JNI host fixture compilation failed'}
    & "$JavaHome\bin\javac.exe" -encoding UTF-8 -cp $classpath -d $out tests/jni-host/com/peerchat/android/NativeBridge.java tests/jni-host/com/peerchat/android/ProfileBridge.java tests/jni-host/com/peerchat/android/PeerTlsBridge.java tests/jni-host/com/peerchat/android/ReliableBridge.java android/app/src/main/java/com/peerchat/android/platform/tls/OpaqueTlsSigner.java android/app/src/main/java/com/peerchat/android/platform/tls/PeerTls.java android/app/src/main/java/com/peerchat/android/platform/tls/ServerChannel.java android/app/src/main/java/com/peerchat/android/platform/tls/RelayFrames.java tests/tls-host/PeerTlsProof.java tests/tls-host/ReliableProfileProof.java tests/tls-host/ServerRelayProof.java
    if($LASTEXITCODE -ne 0){throw 'Reliable production JNI Java proof compilation failed'}
    & "$JavaHome\bin\javac.exe" -encoding UTF-8 -cp "$out;$classpath" -d $out tests/tls-host/ServerDeadlineProof.java
    if($LASTEXITCODE -ne 0){throw 'Platform deadline proof compilation failed'}
    & "$JavaHome\bin\java.exe" -cp "$out;$classpath" com.peerchat.android.platform.tls.ServerDeadlineProof
    if($LASTEXITCODE -ne 0){throw 'Platform socket/TLS cancellation proof failed'}
    & "$JavaHome\bin\java.exe" -Xcheck:jni "-Djava.library.path=$out" -cp "$out;$classpath" com.peerchat.android.platform.tls.ReliableProfileProof "$out\profiles"
    if($LASTEXITCODE -ne 0){throw 'Reliable production JNI/Profile messaging proof failed'}
    if($ServerDirectory){
        & "$PSScriptRoot\test-reliable-server-jni.ps1" -ServerDirectory $ServerDirectory -FixtureDirectory $out -Classpath $classpath -JavaHome $JavaHome
    }
}finally{Pop-Location}
