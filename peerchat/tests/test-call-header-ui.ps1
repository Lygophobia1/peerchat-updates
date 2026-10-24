param([Parameter(Mandatory=$true)][string]$ApplicationDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$package=(Resolve-Path -LiteralPath $ApplicationDirectory).Path
$run=Join-Path $root ('build\tests\call-header-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -Path (Join-Path $package '*') -Destination $run -Recurse
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$references=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object {'/reference:'+(Join-Path $framework $_)}
$exe=Join-Path $run 'CallHeaderTest.exe'
& "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$exe" "/reference:$run\PeerChat.Desktop.exe" $references (Join-Path $PSScriptRoot 'call_header_ui_test.cs')
if($LASTEXITCODE -ne 0){throw 'Call header test compilation failed'}
$process=Start-Process -WindowStyle Hidden -FilePath $exe -ArgumentList ('"'+$run+'"') -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt') -PassThru
if(-not $process.WaitForExit(60000)){$process.Kill();throw 'Call header UI test timed out'}
$process.WaitForExit()
Get-Content (Join-Path $run 'stdout.txt')
Get-Content (Join-Path $run 'stderr.txt')
if($process.ExitCode -ne 0){throw 'Call header UI regression failed'}
Write-Host "Artifacts: $run"
