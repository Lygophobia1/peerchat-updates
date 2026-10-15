param([Parameter(Mandatory=$true)][string]$ApplicationDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $run=Join-Path $env:TEMP ('PeerChat-clipboard-attachment-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $run | Out-Null
    Copy-Item -Path (Join-Path (Resolve-Path -LiteralPath $ApplicationDirectory).Path '*') -Destination $run
    $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
    $refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF/WindowsBase.dll','WPF/PresentationCore.dll','WPF/PresentationFramework.dll') | ForEach-Object {'/reference:'+(Join-Path $fx $_)}
    & "$fx/csc.exe" /nologo /target:exe /platform:x64 "/out:$run/ClipboardAttachmentTest.exe" "/reference:$run/PeerChat.Desktop.exe" $refs .\tests\clipboard_attachment_test.cs
    if($LASTEXITCODE){throw 'Clipboard attachment test compile failed'}
    & "$run/ClipboardAttachmentTest.exe"
    if($LASTEXITCODE){throw 'Clipboard attachment regression failed'}
    Write-Host "Evidence: $run"
}finally{Pop-Location}
