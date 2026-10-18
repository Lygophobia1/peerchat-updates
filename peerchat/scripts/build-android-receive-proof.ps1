param(
    [string]$SdkRoot='C:\Users\Livel\AppData\Local\Android\Sdk',
    [string]$JavaHome='C:\Program Files\Java\jdk-22',
    [string]$ReceiveDirectory='',
    [string]$TestKeystore='',
    [string]$ComparisonCodecArchive='',
    [ValidateSet('arm64-v8a','x86_64')][string]$Abi='arm64-v8a'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$ReceiveDirectory){$ReceiveDirectory=Join-Path $root 'build/android-neteq/arm64-v8a'}
$ndk=Join-Path $SdkRoot 'ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64'
$clang=Join-Path $ndk 'bin/clang++.exe'
$tools=Join-Path $SdkRoot 'build-tools/34.0.0'
$android=Join-Path $SdkRoot 'platforms/android-35/android.jar'
$archive=Join-Path $ReceiveDirectory 'libpeerchat_audio_receive.a'
foreach($required in @($clang,$android,$archive,(Join-Path $JavaHome 'bin/javac.exe'))){if(!(Test-Path -LiteralPath $required)){throw "Missing prerequisite: $required"}}
$expected=if($Abi -eq 'arm64-v8a'){'FCEF3218613460CA02EAE2BD83F997A5A7E6A3097440A7C9E2B19F6472BD47E4'}else{'671D90A6A95D4BFC4A8561DBD28196138662449A98CCEFD1A9334D9DA190A14E'}
if((Get-FileHash $archive).Hash -ne $expected){throw 'Unvalidated receive archive; update proof pin only after rebuilding and auditing'}
$target=if($Abi -eq 'arm64-v8a'){'aarch64-linux-android26'}else{'x86_64-linux-android26'}
$output=Join-Path $root ('build/tests/android-receive-proof-'+[guid]::NewGuid().ToString('N'))
foreach($dir in @('classes','dex',"lib/$Abi")){New-Item -ItemType Directory -Path (Join-Path $output $dir) -Force|Out-Null}
$flags=@("--target=$target",'-std=c++17','-O2','-fPIC','-fexceptions','-fno-rtti','-fvisibility=hidden',"-I$root/audio_receive","-I$ReceiveDirectory/include")
& $clang @flags '-Dmain=pc_receive_proof_main' -c "$root/audio_receive/audio_receive_test.cpp" -o "$output/proof.o"
if($LASTEXITCODE){throw 'Receive proof compilation failed'}
& $clang @flags -c "$root/tests/android-receive-proof/proof_jni.cpp" -o "$output/jni.o"
if($LASTEXITCODE){throw 'Receive proof JNI compilation failed'}
if($ComparisonCodecArchive){
    # Diagnostic only: force the comparison codec's definitions before pulling
    # NetEq's dependency closure. Never a production library selection option.
    $ComparisonCodecArchive=(Resolve-Path -LiteralPath $ComparisonCodecArchive).Path
    $comparisonRoot=[IO.Path]::GetFullPath((Join-Path $root 'build/tests'))+[IO.Path]::DirectorySeparatorChar
    if(!$ComparisonCodecArchive.StartsWith($comparisonRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Comparison codec must be an isolated test artifact'}
    $libraries=@('-Wl,--whole-archive',$ComparisonCodecArchive,'-Wl,--no-whole-archive',$archive)
    Get-FileHash -LiteralPath $ComparisonCodecArchive
    Write-Output 'DIAGNOSTIC COMPARISON: does not validate the production archive codec selection.'
}else{
    $libraries=@('-Wl,--whole-archive',$archive,'-Wl,--no-whole-archive')
}
& $clang "--target=$target" -shared -static-libstdc++ "$output/proof.o" "$output/jni.o" @libraries -llog -ldl -lm '-Wl,-z,defs,-z,max-page-size=16384,-z,relro,-z,now,--exclude-libs,ALL' "-Wl,-Map=$output/proof-link.map" -o "$output/lib/$Abi/libpeerchat_receive_proof.so"
if($LASTEXITCODE){throw 'App-NDK whole-archive link failed'}
& "$JavaHome/bin/javac.exe" --release 8 -cp $android -d "$output/classes" "$root/tests/android-receive-proof/ProofActivity.java"
if($LASTEXITCODE){throw 'Receive proof Java compilation failed'}
$oldJava=$env:JAVA_HOME
try{
    $env:JAVA_HOME=$JavaHome
    $classes=@(Get-ChildItem "$output/classes" -Recurse -Filter '*.class'|ForEach-Object FullName)
    & "$tools/d8.bat" --lib $android --min-api 26 --output "$output/dex" @classes
    if($LASTEXITCODE){throw 'Receive proof dex failed'}
    & "$tools/aapt2.exe" link -I $android --manifest "$root/tests/android-receive-proof/AndroidManifest.xml" -o "$output/unsigned.apk"
    if($LASTEXITCODE){throw 'Receive proof manifest link failed'}
    & "$JavaHome/bin/jar.exe" uf "$output/unsigned.apk" -C "$output/dex" classes.dex -C $output lib
    if($LASTEXITCODE){throw 'Receive proof APK assembly failed'}
    & "$tools/zipalign.exe" -p 4 "$output/unsigned.apk" "$output/aligned.apk"
    if($LASTEXITCODE){throw 'Receive proof alignment failed'}
    # This throwaway key is exclusively for the test package, never PeerChat.
    if($TestKeystore){
        if(!(Test-Path -LiteralPath $TestKeystore -PathType Leaf)){throw 'Missing existing receive-proof test key'}
    }else{
        $TestKeystore=Join-Path $output 'proof-test.p12'
        & "$JavaHome/bin/keytool.exe" -genkeypair -keystore $TestKeystore -storepass android -keypass android -alias proof -dname 'CN=PeerChat Isolated Receive Proof' -keyalg RSA -validity 30
        if($LASTEXITCODE){throw 'Receive proof test-key generation failed'}
    }
    & "$tools/apksigner.bat" sign --ks $TestKeystore --ks-key-alias proof --ks-pass pass:android --out "$output/receive-proof.apk" "$output/aligned.apk"
    if($LASTEXITCODE){throw 'Receive proof signing failed'}
    & "$tools/apksigner.bat" verify --verbose "$output/receive-proof.apk"
    if($LASTEXITCODE){throw 'Receive proof APK verification failed'}
}finally{$env:JAVA_HOME=$oldJava}
Get-FileHash "$output/lib/$Abi/libpeerchat_receive_proof.so","$output/receive-proof.apk"
Write-Output "Receive proof APK (not production PeerChat; no installation performed): $output/receive-proof.apk"
