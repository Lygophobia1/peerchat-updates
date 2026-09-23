param([string]$OutputDirectory = 'build\screen-media')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio discovery tool is unavailable.' }
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'MSVC x64 toolchain is unavailable.' }
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$sdk = Get-ChildItem -LiteralPath $sdkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK headers are unavailable.' }
$vsDevCmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
$winrt = Join-Path $sdk.FullName 'cppwinrt'
$source = Join-Path $projectRoot 'windows_media\peerchat_screen_media.cpp'
$output = Join-Path $projectRoot $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $output
try {
    $build = '"{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /MT /W4 /LD /I"{1}" /Fo:peerchat_screen_media.obj /Fe:PeerChat.ScreenMedia.dll "{2}" /link windowsapp.lib d3d11.lib dxgi.lib dwmapi.lib mfplat.lib mfuuid.lib user32.lib gdi32.lib' -f $vsDevCmd, $winrt, $source
    & cmd.exe /d /c $build
    if ($LASTEXITCODE -ne 0) { throw "Windows screen-media adapter build failed ($LASTEXITCODE)." }
} finally { Pop-Location }
