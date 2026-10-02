param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\profile-jni-'+[Guid]::NewGuid().ToString('N'))
$java='C:\Program Files\Java\jdk-22'
$compiler='C:\TDM-GCC-64\bin\g++.exe'
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ -D__ANDROID__ -DPEERCHAT_PORTABLE_TEXT '-Iportable' ("-I$java\include") ("-I$java\include\win32") portable\peerchat_core.cpp portable\tls_android.cpp storage.cpp group.cpp -o "$out\peerchat_messaging.dll" ("-Wl,--out-implib,$out\libpeerchat_messaging.a") -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Profile JNI host library compilation failed.'}
    & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ '-Iportable' ("-I$java\include") ("-I$java\include\win32") android\app\src\main\cpp\peerchat_jni.cpp ("-L$out") -lpeerchat_messaging -o "$out\peerchat_jni.dll"
    if($LASTEXITCODE -ne 0){throw 'Foundation JNI host library compilation failed.'}
    & "$java\bin\javac.exe" -encoding UTF-8 -d $out tests\jni-host\com\peerchat\android\NativeBridge.java tests\jni-host\com\peerchat\android\ProfileBridge.java tests\jni-host\com\peerchat\android\PeerTlsBridge.java
    if($LASTEXITCODE -ne 0){throw 'Host Java proof compilation failed.'}
    & "$java\bin\java.exe" -Xcheck:jni "-Djava.library.path=$out" -cp $out com.peerchat.android.ProfileBridge
    if($LASTEXITCODE -ne 0){throw 'Profile JNI failure-boundary proof failed.'}
    & "$java\bin\java.exe" -Xcheck:jni "-Djava.library.path=$out" -cp $out com.peerchat.android.NativeBridge "$out\peerchat_jni.dll" "$out\peerchat_messaging.dll"
    if($LASTEXITCODE -ne 0){throw 'Foundation JNI regression failed.'}
} finally {Pop-Location}
