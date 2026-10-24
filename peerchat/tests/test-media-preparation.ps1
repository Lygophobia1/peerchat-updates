$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $output='build\tests\media-preparation'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    & $compiler /nologo /target:exe "/out:$output\MediaPreparationTest.exe" desktop\MediaPreparation.cs tests\media_preparation_test.cs
    if($LASTEXITCODE){throw 'Media preparation regression compilation failed'}
    & "$output\MediaPreparationTest.exe"
    if($LASTEXITCODE){throw 'Media preparation regression failed'}
} finally { Pop-Location }
