param([Parameter(Mandatory=$true)][string]$ClientDirectory,[switch]$AndroidArtifacts,
      [string]$AndroidSdkRoot="$env:LOCALAPPDATA\Android\Sdk")
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\archive-application-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$client=(Resolve-Path -LiteralPath $ClientDirectory).Path
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ /reference:System.Web.Extensions.dll "/out:$out\archive-application-test.exe" "$PSScriptRoot\archive_application_boundary_test.cs" "$root\desktop\ArchiveEnrollmentExecution.cs"
if($LASTEXITCODE -ne 0){throw 'Archive application boundary compilation failed'}
& "$out\archive-application-test.exe" $client (Join-Path $out 'profile')
if($LASTEXITCODE -ne 0){throw 'Archive application boundary regression failed'}
if($AndroidArtifacts){
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $nm=Join-Path $AndroidSdkRoot 'ndk\27.0.12077973\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe'
    foreach($variant in @('debug','release')){
        $name=if($variant -eq 'debug'){'app-debug.apk'}else{'app-release-unsigned.apk'}
        $apk=Join-Path $root "android\app\build\outputs\apk\$variant\$name"
        $zip=[IO.Compression.ZipFile]::OpenRead($apk)
        try{
            $entry=$zip.GetEntry('lib/arm64-v8a/libpeerchat_messaging.so')
            if(!$entry -or $entry.Length -lt 1 -or $entry.Length -gt 64MB){throw 'Invalid ARM64 core artifact'}
            $library=Join-Path $out "$variant-archive.so"
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$library)
        }finally{$zip.Dispose()}
        $exports=& $nm -D --defined-only $library
        if($LASTEXITCODE -ne 0 -or ($exports -join "`n") -notmatch 'Java_com_peerchat_android_ArchiveBridge_request'){throw 'Archive JNI export absent'}
    }
    Write-Host 'PASS: debug/release ARM64 archive JNI application export. Static audit only; physical execution pending.'
}
