param([string]$OutputDirectory='build\v0.3-beta\Client')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ ui_bridge.cpp share_id.cpp node.cpp tls.cpp storage.cpp group.cpp -o "$OutputDirectory/PeerChat.Core.dll" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt
    if ($LASTEXITCODE -ne 0) { throw 'Core adapter build failed' }
    $framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references = @('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:' + (Join-Path $framework $_) }
    & "$framework\csc.exe" /nologo /target:winexe /platform:x64 /optimize+ "/out:$OutputDirectory\PeerChat.Desktop.exe" /win32manifest:desktop\app.manifest /win32icon:desktop\Assets\Icons\peerchat-client.ico /resource:desktop\Main.xaml,Main.xaml $references .\shared\VersionInfo.cs .\desktop\CoreAdapter.cs .\desktop\ChatViewModel.cs .\desktop\DiscoveryClient.cs .\desktop\FirewallRules.cs .\desktop\LocalProfiles.cs .\desktop\App.cs
    if ($LASTEXITCODE -ne 0) { throw 'WPF desktop build failed' }
    Copy-Item desktop/App.config "$OutputDirectory/PeerChat.Desktop.exe.config" -Force
    Copy-Item desktop/Assets/Icons/peerchat-client.ico "$OutputDirectory/peerchat-client.ico" -Force
    Write-Host "Built $OutputDirectory/PeerChat.Desktop.exe with the native messaging core."
} finally { Pop-Location }
