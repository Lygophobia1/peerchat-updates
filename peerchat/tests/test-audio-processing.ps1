[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $repoRoot 'scripts\build-audio-processing.ps1') -DependencyRoot $env:PEERCHAT_DEPENDENCY_ROOT -ToolRoot $env:PEERCHAT_WEBRTC_TOOLS_ROOT -OutputRoot $env:PEERCHAT_APM_OUTPUT_ROOT
if ($LASTEXITCODE -ne 0) { throw 'Standalone audio-processing build failed.' }

$outputRoot = if ($env:PEERCHAT_APM_OUTPUT_ROOT) { $env:PEERCHAT_APM_OUTPUT_ROOT } else { 'C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64' }
$test = Join-Path $outputRoot 'peerchat_audio_processing_test.exe'
if (-not (Test-Path -LiteralPath $test)) { throw "Lifecycle test missing: $test" }

$originalPath = $env:PATH
try {
    $env:PATH = "$outputRoot;$env:SystemRoot\System32"
    & $test
    if ($LASTEXITCODE -ne 0) { throw 'PeerChat Audio Processing lifecycle test failed.' }
} finally {
    $env:PATH = $originalPath
}

$dll = Join-Path $outputRoot 'PeerChat.AudioProcessing.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw 'Audio-processing DLL missing after build.' }

if (Get-Command objdump.exe -ErrorAction SilentlyContinue) {
    $imports = @(& objdump.exe -p $dll | Select-String 'DLL Name:' | ForEach-Object {
        ($_.ToString() -replace '^.*DLL Name:\s*', '').Trim()
    })
    $unexpected = @($imports | Where-Object {
        $_ -notmatch '^(api-ms-win-|KERNEL32\.dll$|USER32\.dll$|WINMM\.dll$)'
    })
    if ($unexpected.Count -ne 0) {
        throw "Audio-processing DLL has unexpected runtime imports: $($unexpected -join ', ')"
    }
    $imports | ForEach-Object { Write-Output "DLL Name: $_" }
}

Write-Output 'PASS: standalone WebRTC Audio Processing component and restricted-PATH lifecycle test.'
