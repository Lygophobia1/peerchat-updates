param([Parameter(Mandatory=$true)][string]$ApplicationDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $run=Join-Path $root ('build\tests\local-deletion-ui-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $run | Out-Null
    Copy-Item -Path (Join-Path (Resolve-Path -LiteralPath $ApplicationDirectory).Path '*') -Destination $run
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object {'/reference:'+(Join-Path $fx $_)}
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$run\LocalDeletionUiTest.exe" "/reference:$run\PeerChat.Desktop.exe" $refs tests\local_deletion_ui_test.cs
    if($LASTEXITCODE){throw 'Local deletion UI test compile failed'}
    & "$run\LocalDeletionUiTest.exe" "$run\fixtures"
    if($LASTEXITCODE){throw 'Local deletion UI regression failed'}
}finally{Pop-Location}
