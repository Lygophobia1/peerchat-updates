param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if([string]::IsNullOrEmpty($OutputDirectory)){$OutputDirectory=Join-Path ([IO.Path]::GetTempPath()) ('peerchat-identity-adapter-'+[Guid]::NewGuid().ToString('N'))}
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /reference:System.Core.dll ("/out:"+(Join-Path $out 'adapter-test.exe')) (Join-Path $root 'desktop\DeviceIdentityDirectory.cs') (Join-Path $PSScriptRoot 'device_identity_windows_adapter_test.cs')
if($LASTEXITCODE){throw 'Windows identity adapter fixture did not compile.'}
& (Join-Path $out 'adapter-test.exe')
if($LASTEXITCODE){throw 'Windows identity adapter fixture failed.'}
