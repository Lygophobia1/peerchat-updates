param([string]$AndroidSdk=$env:ANDROID_HOME)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $AndroidSdk){$AndroidSdk=Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
$readelf=Join-Path $AndroidSdk 'ndk\27.0.12077973\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
if(-not(Test-Path -LiteralPath $readelf)){throw 'Pinned NDK readelf is required.'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$out=Join-Path $root ('build\tests\android-profile-audit-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
foreach($variant in @('debug','release')){
    $apk=Join-Path $root $(if($variant -eq 'debug'){'android\app\build\outputs\apk\debug\app-debug.apk'}else{'android\app\build\outputs\apk\release\app-release-unsigned.apk'})
    $zip=[IO.Compression.ZipFile]::OpenRead($apk)
    try {
        foreach($name in @('peerchat_messaging','peerchat_jni')){
            $entry=$zip.GetEntry("lib/arm64-v8a/lib$name.so")
            if(-not $entry){throw "$variant missing ARM64 $name"}
            $path=Join-Path $out "$variant-$name.so"
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$path)
            $header=(& $readelf -h $path|Out-String); if($LASTEXITCODE -ne 0 -or $header -notmatch 'AArch64'){throw 'Invalid native architecture'}
            $dynamic=(& $readelf -d $path|Out-String); if($LASTEXITCODE -ne 0){throw 'ELF dependency inspection failed'}
            $needed=[regex]::Matches($dynamic,'Shared library: \[([^\]]+)\]')|ForEach-Object {$_.Groups[1].Value}
            foreach($dependency in $needed){if($dependency -notin @('libc.so','libm.so','libdl.so','liblog.so','libpeerchat_messaging.so')){throw "Unexpected dependency: $dependency"}}
            $symbols=(& $readelf --dyn-syms --wide $path|Out-String); if($LASTEXITCODE -ne 0){throw 'ELF export inspection failed'}
            if($name -eq 'peerchat_messaging'){
                foreach($method in @('bind','publicKey','sign')){if($symbols -notmatch "Java_com_peerchat_android_ProfileBridge_$method\b"){throw "Missing ProfileBridge $method"}}
                $testHook=$symbols -match 'Java_com_peerchat_android_ProfileBridge_storageTest\b'
                if($testHook -ne ($variant -eq 'debug')){throw "$variant test-hook isolation failed"}
            }
            Write-Host "PASS: $variant ARM64 $name, dependencies=$($needed -join ',')"
        }
    } finally {$zip.Dispose()}
}
Write-Host 'PASS: production profile exports present; destructive test hook absent from release. Static artifact audit, not device execution.'
