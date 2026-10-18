param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/delivery-projection-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$compiler=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/Roslyn/csc.exe'
$version=[regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $root 'shared/VersionInfo.cs')),'Display="([^"]+)"').Groups[1].Value
$updater=Join-Path $root "build/development/$version/Updater/PeerChat.Updater.Core.dll"
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Net.Http.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','WPF/WindowsBase.dll','WPF/PresentationCore.dll','WPF/PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
$sources=@(Get-ChildItem -LiteralPath (Join-Path $root 'desktop') -Filter '*.cs'|ForEach-Object FullName)
Push-Location $root
try {
    & $compiler /nologo /target:exe /platform:x64 /main:AdapterTest "/out:$out/DeliveryProjectionTest.exe" $refs "/reference:$updater" ./shared/VersionInfo.cs @sources
    if($LASTEXITCODE){throw 'Delivery projection test compile failed'}
    & "$out/DeliveryProjectionTest.exe" --delivery-projection-only
    if($LASTEXITCODE){throw 'Delivery projection regression failed'}
    [xml](Get-Content -Raw -LiteralPath './desktop/Main.xaml')|Out-Null
    Write-Output "PASS XAML parse; evidence: $out"
}finally{Pop-Location}
