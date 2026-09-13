param([string]$OutputDirectory='build\v0.3-beta\Client',[string]$UpdaterCorePath='build\v0.3-beta\Updater\PeerChat.Updater.Core.dll')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $updaterDirectory=Split-Path $UpdaterCorePath -Parent
    $updaterRuntime=@('PeerChat.Updater.Core.dll','PeerChat.Updater.exe','PeerChat.Updater.exe.config')
    if(@($updaterRuntime | Where-Object { -not (Test-Path (Join-Path $updaterDirectory $_)) }).Count -ne 0) { & "$PSScriptRoot\build-updater.ps1" -OutputDirectory $updaterDirectory }
    if(@($updaterRuntime | Where-Object { -not (Test-Path (Join-Path $updaterDirectory $_)) }).Count -ne 0) { throw 'Updater runtime build failed or is incomplete.' }
    if(-not (Test-Path 'build\native\install\lib\libdatachannel.a') -or -not (Test-Path 'build\native\install\lib\libopus.a')) { & "$PSScriptRoot\build-voice-dependencies.ps1" }
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ -Ibuild\native\install\include ui_bridge.cpp share_id.cpp node.cpp tls.cpp storage.cpp group.cpp voice_media.cpp audio.cpp -Lbuild\native\install\lib -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o "$OutputDirectory/PeerChat.Core.dll" -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi
    if ($LASTEXITCODE -ne 0) { throw 'Core adapter build failed' }
    $framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references = @('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Net.Http.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:' + (Join-Path $framework $_) }
    & "$framework\csc.exe" /nologo /target:winexe /platform:x64 /optimize+ "/out:$OutputDirectory\PeerChat.Desktop.exe" /win32manifest:desktop\app.manifest /win32icon:desktop\Assets\Icons\peerchat-client.ico /resource:desktop\Main.xaml,Main.xaml "/reference:$UpdaterCorePath" $references .\shared\VersionInfo.cs .\desktop\CoreAdapter.cs .\desktop\ChatViewModel.cs .\desktop\DiscoveryClient.cs .\desktop\FriendConversations.cs .\desktop\ProfileAccounts.cs .\desktop\FirewallRules.cs .\desktop\LocalProfiles.cs .\desktop\UpdateClient.cs .\desktop\App.cs
    if ($LASTEXITCODE -ne 0) { throw 'WPF desktop build failed' }
    Copy-Item desktop/App.config "$OutputDirectory/PeerChat.Desktop.exe.config" -Force
    Copy-Item desktop/Assets/Icons/peerchat-client.ico "$OutputDirectory/peerchat-client.ico" -Force
    Copy-Item ($updaterRuntime | ForEach-Object { Join-Path $updaterDirectory $_ }) $OutputDirectory -Force
    Write-Host "Built $OutputDirectory/PeerChat.Desktop.exe with its native core and updater runtime."
} finally { Pop-Location }
