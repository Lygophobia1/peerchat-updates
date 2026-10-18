param([Parameter(Mandatory=$true)][string]$ClientDirectory,[switch]$CompileCurrentDesktop)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\device-manager-ui-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$client=(Resolve-Path -LiteralPath $ClientDirectory).Path
Copy-Item -Path "$client\*" -Destination $out
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$csc=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
if(!(Test-Path -LiteralPath $csc)){$csc=Join-Path $fx 'csc.exe'}
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Net.Http.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
if($CompileCurrentDesktop){
    $sources=Get-ChildItem -LiteralPath (Join-Path $root 'desktop') -Filter *.cs | Where-Object {$_.Name -notin @('AdapterTest.cs','UiSmoke.cs')} | ForEach-Object FullName
    & $csc /nologo /target:winexe /platform:x64 "/out:$out\PeerChat.Desktop.exe" "/reference:$out\PeerChat.Updater.Core.dll" ("/resource:"+(Join-Path $root 'desktop\Main.xaml')+",Main.xaml") $refs (Join-Path $root 'shared\VersionInfo.cs') @sources
    if($LASTEXITCODE -ne 0){throw 'Current Device Manager desktop compilation failed'}
}
& $csc /nologo /target:exe /platform:x64 "/out:$out\device-manager-ui.exe" "/reference:$out\PeerChat.Desktop.exe" $refs "$PSScriptRoot\device_manager_ui_test.cs"
if($LASTEXITCODE -ne 0){throw 'Device Manager fixture compilation failed'}
$process=Start-Process -WindowStyle Hidden -PassThru -FilePath "$out\device-manager-ui.exe" -ArgumentList ('"'+$out+'"') -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
$null=$process.Handle
if(!$process.WaitForExit(60000)){throw "Device Manager fixture timeout, process=$($process.Id)"}
Get-Content -LiteralPath "$out\stdout.log","$out\stderr.log"
if($process.ExitCode -ne 0){throw 'Device Manager fixture failed'}
Write-Host "Evidence: $out"
