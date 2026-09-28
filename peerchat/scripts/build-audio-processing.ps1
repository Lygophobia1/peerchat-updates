[CmdletBinding()]
param(
    [switch]$CleanOutput
)

$ErrorActionPreference = 'Stop'

$expectedRevision = '6cf6bcb585b31414aac02377df2bc8fec770d0e8'
$repoRoot = Split-Path -Parent $PSScriptRoot
$webRtcRoot = Join-Path $repoRoot 'build\deps\webrtc-complete-src\src'
$gn = Join-Path $repoRoot 'build\tools\webrtc-gn\gn.exe'
$depotToolsRoot = Join-Path $repoRoot 'build\tools\depot_tools'
$pythonDirectory = Join-Path $repoRoot 'build\tools\depot_tools\.cipd_bin\3.11\bin'
$ninjaCommand = Get-Command ninja.exe -ErrorAction SilentlyContinue
$ninja = if ($null -ne $ninjaCommand) {
    $ninjaCommand.Source
} else {
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}
$sourceRoot = Join-Path $repoRoot 'audio_processing'
$targetRoot = Join-Path $webRtcRoot 'peerchat_audio_processing'
$outputRoot = Join-Path $repoRoot 'build\audio-processing\webrtc-6cf6bcb\x64'

if (-not (Test-Path -LiteralPath $webRtcRoot)) {
    throw "Clean pinned WebRTC checkout is missing: $webRtcRoot"
}
if (-not (Test-Path -LiteralPath $gn)) {
    throw "Pinned GN tool is missing: $gn"
}
if (-not (Test-Path -LiteralPath (Join-Path $pythonDirectory 'python3.exe'))) {
    throw "Pinned WebRTC Python runtime is missing: $pythonDirectory"
}
if (-not (Test-Path -LiteralPath $ninja)) {
    throw "Ninja is missing: $ninja"
}

$actualRevision = (& git -C $webRtcRoot rev-parse HEAD).Trim()
if ($actualRevision -ne $expectedRevision) {
    throw "WebRTC checkout is $actualRevision; expected $expectedRevision. Refusing to build a different revision."
}

# This isolated build uses bundled clang-cl and the locally installed SDK.
# Apply only exact, idempotent target-registration and SDK-version adaptations
# to the ignored pinned checkout; never patch WebRTC processing sources.
function Set-ExactBuildAdaptation {
    param([string]$Path, [string]$Expected, [string]$Replacement)
    $Expected = $Expected.Replace("`n", [Environment]::NewLine)
    $Replacement = $Replacement.Replace("`n", [Environment]::NewLine)
    $content = Get-Content -LiteralPath $Path -Raw
    if ($content.Contains($Replacement)) { return }
    if (-not $content.Contains($Expected)) {
        throw "The pinned WebRTC build file does not match the expected clean content: $Path"
    }
    [System.IO.File]::WriteAllText($Path, $content.Replace($Expected, $Replacement), [System.Text.UTF8Encoding]::new($false))
}

Set-ExactBuildAdaptation (Join-Path $webRtcRoot 'BUILD.gn') @'
import("webrtc.gni")

if (rtc_rust) {
'@ @'
import("webrtc.gni")

# PeerChat's standalone APM boundary. It is intentionally separate from the
# WebRTC calling/video targets and exposes only a C ABI.
group("peerchat_audio_processing") {
  deps = [ "//peerchat_audio_processing:peerchat_audio_processing" ]
}

group("peerchat_audio_processing_test") {
  testonly = true
  deps = [ "//peerchat_audio_processing:peerchat_audio_processing_test" ]
}

if (rtc_rust) {
'@
Set-ExactBuildAdaptation (Join-Path $webRtcRoot 'build\\config\\win\\BUILD.gn') 'NTDDI_VERSION=NTDDI_WIN11_BR' 'NTDDI_VERSION=0x0A000010'
Set-ExactBuildAdaptation (Join-Path $webRtcRoot 'build\\config\\win\\visual_studio_version.gni') 'visual_studio_runtime_dirs = []' 'visual_studio_runtime_dirs = "C:/Windows/System32"'
Set-ExactBuildAdaptation (Join-Path $webRtcRoot 'build\\toolchain\\win\\setup_toolchain.py') "SDK_VERSION = '10.0.28000.0'" "SDK_VERSION = '10.0.26100.0'"

New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null
foreach ($file in @('BUILD.gn', 'peerchat_audio_processing.h', 'peerchat_audio_processing.cpp', 'audio_processing_test.cpp')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $file) -Destination (Join-Path $targetRoot $file) -Force
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'webrtc_gclient_args.gni') -Destination (Join-Path $webRtcRoot 'build\config\gclient_args.gni') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'webrtc_lastchange_committime.txt') -Destination (Join-Path $webRtcRoot 'build\util\LASTCHANGE.committime') -Force

if ($CleanOutput -and (Test-Path -LiteralPath $outputRoot)) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Copy-Item -LiteralPath (Join-Path $sourceRoot 'webrtc_args.gn') -Destination (Join-Path $outputRoot 'args.gn') -Force
$env:PATH = "$pythonDirectory;$env:PATH"
$env:PYTHONPATH = "$depotToolsRoot;$env:PYTHONPATH"
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
Push-Location $webRtcRoot
try {
    & $gn gen $outputRoot
    if ($LASTEXITCODE -ne 0) { throw 'GN generation failed for the standalone audio-processing component.' }

    & $ninja -C $outputRoot peerchat_audio_processing peerchat_audio_processing_test
    if ($LASTEXITCODE -ne 0) { throw 'Standalone audio-processing component build failed.' }
} finally {
    Pop-Location
}

$dll = Join-Path $outputRoot 'PeerChat.AudioProcessing.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw 'Audio-processing DLL was not produced.' }

Write-Output "WebRTC revision: $actualRevision"
Write-Output "Audio-processing DLL: $dll"
Write-Output "Lifecycle test: $(Join-Path $outputRoot 'peerchat_audio_processing_test.exe')"
