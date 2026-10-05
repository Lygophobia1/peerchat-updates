param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& "$PSScriptRoot\test-android-profile-artifacts.ps1"
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach($variant in @('debug','release')){
    $relative=if($variant -eq 'debug'){'android/app/build/outputs/apk/debug/app-debug.apk'}else{'android/app/build/outputs/apk/release/app-release-unsigned.apk'}
    $zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $root $relative))
    try{
        $native=@($zip.Entries|Where-Object {$_.FullName -like 'lib/*'}|ForEach-Object {$_.FullName}|Sort-Object)
        if(($native -join ';') -ne 'lib/arm64-v8a/libandroidx.graphics.path.so;lib/arm64-v8a/libdatastore_shared_counter.so;lib/arm64-v8a/libpeerchat_jni.so;lib/arm64-v8a/libpeerchat_messaging.so'){throw 'Unexpected native component/ABI in APK'}
        # Firebase Messaging 25.1.3 / Installations 19.1.2 bring AndroidX
        # DataStore 1.1.7. Compare against its upstream ARM64 AAR bytes, not
        # a broad wildcard allowing arbitrary native libraries.
        $counter=$zip.GetEntry('lib/arm64-v8a/libdatastore_shared_counter.so').Open()
        $counterSha=[Security.Cryptography.SHA256]::Create()
        try{$counterHash=[BitConverter]::ToString($counterSha.ComputeHash($counter)).Replace('-','')}finally{$counter.Dispose();$counterSha.Dispose()}
        if($counterHash -ne 'D3E48717C9AA147E0AB21063BA0E8E0211CABF8BF40B222640829519EDBF58E1'){throw 'Pinned AndroidX DataStore native artifact changed'}
        # Existing Compose graphics-path 1.0.1 runtime, byte-for-byte identical
        # to its ARM64 AAR entry. Not a TLS dependency.
        $graphics=$zip.GetEntry('lib/arm64-v8a/libandroidx.graphics.path.so').Open()
        $sha=[Security.Cryptography.SHA256]::Create()
        try{$graphicsHash=[BitConverter]::ToString($sha.ComputeHash($graphics)).Replace('-','')}finally{$graphics.Dispose();$sha.Dispose()}
        if($graphicsHash -ne '41E9A793C43A0F4FDDB19E33F346BACE464F30F888BA7B9EAF96294EA115BFB6'){throw 'Existing Compose native artifact changed'}
        if(-not $zip.GetEntry('META-INF/LICENSE.md')){throw 'Upstream TLS license is missing'}
        foreach($entry in $zip.Entries){
            if($entry.FullName -match '\.(pfx|p12|pem|key|crt|cer|sqlite|jks)$' -or $entry.FullName -match 'compatibility\.bin|test-server|tls-host'){throw 'Unexpected credential/test material in APK'}
            if($entry.FullName -match '^classes[0-9]*\.dex$'){
                $stream=$entry.Open();$buffer=[IO.MemoryStream]::new()
                try{$stream.CopyTo($buffer);$text=[Text.Encoding]::ASCII.GetString($buffer.ToArray())}finally{$stream.Dispose();$buffer.Dispose()}
                foreach($fixture in @('Lcom/peerchat/android/PeerTlsTest;','Lcom/peerchat/android/platform/tls/PeerTlsProof;','Lcom/peerchat/android/platform/tls/NativeTlsProof;','LSigningProof;')){
                    if($text.Contains($fixture)){throw 'Host/device TLS test fixture packaged in application'}
                }
                if($text.Contains('-----BEGIN PRIVATE KEY-----')){throw 'Unexpected private-key material in APK'}
            }
        }
        Write-Host "PASS: $variant only intended ARM64 native components, license retained, no TLS test fixtures/credential files packaged. Static audit only."
    }finally{$zip.Dispose()}
}
if(-not $env:JAVA_HOME){$env:JAVA_HOME='C:\Program Files\Java\jdk-22'}
if(-not $env:ANDROID_HOME){$env:ANDROID_HOME=Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
$runtime=(& (Join-Path $root 'android/gradlew.bat') -p (Join-Path $root 'android') :app:dependencies --configuration releaseRuntimeClasspath --offline --console=plain | Out-String)
if($LASTEXITCODE -ne 0){throw 'TLS runtime dependency inspection failed'}
$modules=@([regex]::Matches($runtime,'org\.bouncycastle:([a-z0-9-]+):([0-9.]+)') | ForEach-Object {$_.Groups[1].Value+':'+$_.Groups[2].Value} | Sort-Object -Unique)
if(($modules -join ';') -ne 'bcpkix-jdk18on:1.86;bcprov-jdk18on:1.86;bctls-jdk18on:1.86.1;bcutil-jdk18on:1.86'){throw 'Unexpected TLS runtime module/version'}
Write-Host 'PASS: exact four pinned Java TLS runtime modules; old AGP-tooling BC is not an application runtime dependency.'
