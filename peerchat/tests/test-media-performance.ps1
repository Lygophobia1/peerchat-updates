param([string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\media-performance-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    foreach($name in @('media_performance','audio','audio_lifecycle')){
        $source=Join-Path $PSScriptRoot ($name+'_test.cpp');$exe=Join-Path $out ($name+'.exe')
        & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. $source '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -lwinmm -o $exe
        if($LASTEXITCODE){throw "$name compile failed"}; & $exe
        if($LASTEXITCODE){throw "$name regression failed"}
    }
    if($ClientDirectory){
        Copy-Item -Path "$ClientDirectory\*" -Destination $out
        $csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
        $fx=Split-Path $csc -Parent
        $refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object{'/reference:'+(Join-Path $fx $_)}
        & $csc /nologo /target:exe "/out:$out\summary.exe" "/reference:$out\PeerChat.Desktop.exe" $refs "$PSScriptRoot\media_diagnostics_test.cs" "$PSScriptRoot\connection_preview_ui_test.cs" "$PSScriptRoot\connections_live_test.cs" "$PSScriptRoot\transfer_diagnostics_test.cs" "$PSScriptRoot\dm_diagnostics_test.cs" "$PSScriptRoot\group_calls_diagnostics_test.cs"
        if($LASTEXITCODE){throw 'Media summary compile failed'}; & "$out\summary.exe" (Join-Path $out 'profile')
        if($LASTEXITCODE){throw 'Media summary regression failed'}
    }
}finally{Pop-Location}
