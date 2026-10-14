param([string]$ClientDirectory,[switch]$RebuildDesktop)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try{
    if(!$ClientDirectory){$ClientDirectory=Join-Path $root 'build\development\4.0.2-dev.149\Client'}
    $ClientDirectory=(Resolve-Path $ClientDirectory).Path
    $clientOut=$ClientDirectory;$updaterOut=Join-Path (Split-Path $clientOut -Parent) 'Updater';$generatedVersionSource=Join-Path (Split-Path $clientOut -Parent) 'VersionInfo.cs'
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $csc='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
    $r=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Net.Http.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
    function Run($Name,[scriptblock]$Block){& $Block;if($LASTEXITCODE){throw "$Name failed"}}
    if($RebuildDesktop){$source=Get-Content build.ps1 -Raw;$at=$source.IndexOf("Run 'Desktop' {");$end=$source.IndexOf(';Copy-Item .\desktop\App.config',$at);& ([scriptblock]::Create($source.Substring($at,$end-$at)))}
    $out=Join-Path $root ('build\tests\emoji-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $out|Out-Null
    Copy-Item "$ClientDirectory\*" $out
    foreach($test in @('emoji_feature','emoji_peer_roundtrip')){
        & $csc /nologo /target:exe /platform:x64 "/out:$out\$test.exe" "/reference:$out\PeerChat.Desktop.exe" $r "$PSScriptRoot\${test}_test.cs"
        if($LASTEXITCODE){throw "$test compilation failed"}
        & "$out\$test.exe" "$out\fixtures"
        if($LASTEXITCODE){throw "$test failed"}
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ "$PSScriptRoot\emoji_utf8_test.cpp" -o "$out\emoji-utf8.exe"
    if($LASTEXITCODE){throw 'Portable Unicode compilation failed'};& "$out\emoji-utf8.exe";if($LASTEXITCODE){throw 'Portable Unicode test failed'}
    $java='C:\Program Files\Android\Android Studio\jbr\bin'
    & "$java\javac.exe" -encoding UTF-8 -d $out "$PSScriptRoot\EmojiUtf8Test.java"
    if($LASTEXITCODE){throw 'JVM Unicode compilation failed'};& "$java\java.exe" -cp $out EmojiUtf8Test;if($LASTEXITCODE){throw 'JVM Unicode test failed'}
    Write-Host "PASS: emoji fixtures and screenshots: $out"
}finally{Pop-Location}
