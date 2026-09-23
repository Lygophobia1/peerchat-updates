param([switch]$Run)
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
$output = Join-Path $projectRoot 'build\stage8-probe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $output
try {
    $source = Join-Path $projectRoot 'tests\screen_media_local_pipeline_probe.cpp'
    $build = '"{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /MD /W4 /I"{1}" /Fo:screen_media_local_pipeline_probe.obj /Fe:screen_media_local_pipeline_probe.exe "{2}" /link windowsapp.lib d3d11.lib dxgi.lib mfplat.lib mfuuid.lib mfreadwrite.lib user32.lib' -f $vsDevCmd, $winrt, $source
    & cmd.exe /d /c $build
    if ($LASTEXITCODE -ne 0) { throw "Stage 8 GPU pipeline probe compilation failed ($LASTEXITCODE)." }
    if ($Run) {
        & (Join-Path $output 'screen_media_local_pipeline_probe.exe')
        if ($LASTEXITCODE -ne 0) { throw "Stage 8 GPU pipeline probe failed ($LASTEXITCODE)." }
    }
} finally { Pop-Location }
