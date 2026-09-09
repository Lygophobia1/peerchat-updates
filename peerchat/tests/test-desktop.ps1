param([string]$OutputDirectory='build\tests\desktop')
$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
 New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
 & (Join-Path $projectRoot 'scripts\build-desktop.ps1') -OutputDirectory $OutputDirectory
 $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
 $references=@('System.dll','System.Core.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:'+(Join-Path $framework $_) }
 & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$OutputDirectory\AdapterTest.exe" $references .\desktop\CoreAdapter.cs .\desktop\ChatViewModel.cs .\desktop\AdapterTest.cs
 if($LASTEXITCODE -ne 0){throw 'Adapter test build failed'}
 & ".\$OutputDirectory\AdapterTest.exe"
 if($LASTEXITCODE -ne 0){throw 'Adapter regression failed'}
} finally {Pop-Location}
