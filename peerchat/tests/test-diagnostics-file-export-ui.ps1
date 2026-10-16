param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/diagnostics-export-ui-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
$display=[regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $root 'shared\VersionInfo.cs')),'Display="([^"]+)"').Groups[1].Value
$updater=Join-Path $root ("build\development\$display\Updater\PeerChat.Updater.Core.dll")
if(-not(Test-Path -LiteralPath $updater)){throw 'Build matching updater before full managed UI compile'}
$references=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $framework $_)}
$sources=@(Get-ChildItem -LiteralPath (Join-Path $root 'desktop') -Filter '*.cs'|ForEach-Object FullName)
$binary=Join-Path $out 'DiagnosticsExportUI.exe'
& $compiler /nologo /target:exe /platform:x64 /main:DiagnosticsFileExportUiTest "/out:$binary" $references "/reference:$updater" /reference:System.Net.Http.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll (Join-Path $root 'shared\VersionInfo.cs') @sources (Join-Path $PSScriptRoot 'diagnostics_file_export_ui_test.cs')
if($LASTEXITCODE){throw 'Production Windows managed export compile failed'}
Copy-Item -LiteralPath $updater -Destination $out
& $binary
if($LASTEXITCODE){throw 'Windows complete diagnostics UI capture regression failed'}
"Windows export UI fixture: $out"
