param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/diagnostics-file-export-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$binary=Join-Path $out 'DiagnosticsFileExport.exe'
& (Join-Path $framework 'csc.exe') /nologo /target:exe /platform:x64 "/out:$binary" "/reference:$framework\System.Web.Extensions.dll" (Join-Path $root 'desktop\Diagnostics.cs') (Join-Path $PSScriptRoot 'diagnostics_file_export_test.cs')
if($LASTEXITCODE){throw 'Diagnostics file export fixture compile failed'}
& $binary (Join-Path $out 'reports')
if($LASTEXITCODE){throw 'Diagnostics file export regression failed'}
"Diagnostic export artifacts: $out"
