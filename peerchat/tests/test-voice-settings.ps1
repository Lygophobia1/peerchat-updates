param([string]$OutputDirectory='build\tests\voice-settings')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Security.dll','System.Web.Extensions.dll') | ForEach-Object {'/reference:'+(Join-Path $framework $_)}
    & "$framework\csc.exe" /nologo /target:exe "/out:$OutputDirectory\VoicePreferencesTest.exe" $references .\desktop\VoicePreferences.cs .\tests\voice_preferences_test.cs
    if($LASTEXITCODE -ne 0){throw 'Voice preference test build failed.'}
    & (Join-Path $OutputDirectory 'VoicePreferencesTest.exe')
    if($LASTEXITCODE -ne 0){throw 'Voice preference persistence test failed.'}
} finally {Pop-Location}
