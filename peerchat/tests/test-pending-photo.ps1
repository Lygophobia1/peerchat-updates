param([Parameter(Mandatory=$true)][string]$ApplicationDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $run=Join-Path $root ('build\tests\pending-photo-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $run | Out-Null
    Copy-Item -Path (Join-Path (Resolve-Path -LiteralPath $ApplicationDirectory).Path '*') -Destination $run
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object {'/reference:'+(Join-Path $fx $_)}
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$run\PendingPhotoTest.exe" "/reference:$run\PeerChat.Desktop.exe" $refs tests\pending_photo_test.cs
    if($LASTEXITCODE){throw 'Pending photo test compile failed'}
    & "$run\PendingPhotoTest.exe" "$run\fixtures"
    if($LASTEXITCODE){throw 'Pending photo regression failed'}
    Write-Output ("Fixtures: "+$run+"\fixtures")
}finally{Pop-Location}
