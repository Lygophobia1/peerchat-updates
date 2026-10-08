$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\relay-retirement-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /warnaserror /r:System.Web.Extensions.dll "/out:$out\test.exe" (Join-Path $root 'desktop\RelayTransport.cs') (Join-Path $PSScriptRoot 'RelayRetirementTest.cs')
if($LASTEXITCODE -ne 0){throw 'Relay retirement compilation failed'}
& "$out\test.exe"
if($LASTEXITCODE -ne 0){throw 'Relay retirement regression failed'}
