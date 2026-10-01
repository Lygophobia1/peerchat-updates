param([Parameter(Mandatory=$true)][string]$ApplicationDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $package=(Resolve-Path -LiteralPath $ApplicationDirectory).Path
    $run=Join-Path $root ('build\tests\voice-settings-ui-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $run | Out-Null
    Copy-Item -Path (Join-Path $package '*') -Destination $run
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object {'/reference:'+(Join-Path $framework $_)}
    $exe=Join-Path $run 'VoiceSettingsUiTest.exe'
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$exe" "/reference:$run\PeerChat.Desktop.exe" $references .\tests\voice_settings_ui_test.cs
    if($LASTEXITCODE -ne 0){throw 'Voice settings UI regression build failed.'}
    $process=Start-Process -WindowStyle Hidden -FilePath $exe -ArgumentList ('"'+$run+'"') -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt') -PassThru
    if(-not $process.WaitForExit(60000)){try{$process.Kill()}catch{};throw "Voice settings UI test timed out; artifacts: $run"}
    $process.WaitForExit()
    Get-Content (Join-Path $run 'stdout.txt')
    Get-Content (Join-Path $run 'stderr.txt')
    Write-Host "Voice settings UI exit code: $($process.ExitCode); artifacts: $run"
    if($process.ExitCode -ne 0){throw 'Voice settings UI regression failed.'}
} finally {Pop-Location}
