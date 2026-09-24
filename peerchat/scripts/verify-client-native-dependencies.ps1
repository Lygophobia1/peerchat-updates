param([string]$ClientDirectory='releases\v0.3-beta\Client',[switch]$RequireScreenMedia)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $client=Resolve-Path $ClientDirectory
    $requiredRuntime=@('PeerChat.Desktop.exe','PeerChat.Desktop.exe.config','PeerChat.Core.dll','PeerChat.Updater.exe','PeerChat.Updater.Core.dll','PeerChat.Updater.exe.config','update-feeds.json')
    $missing=@($requiredRuntime | Where-Object { -not (Test-Path (Join-Path $client $_)) })
    if($missing.Count -ne 0){throw 'Packaged Client is missing required runtime file(s): '+($missing -join ', ')}
    $screenMedia=Join-Path $client 'PeerChat.ScreenMedia.dll'
    if($RequireScreenMedia -and -not (Test-Path -LiteralPath $screenMedia)){throw 'Packaged Client is missing PeerChat.ScreenMedia.dll.'}
    $desktop=Join-Path $client 'PeerChat.Desktop.exe'
    try {
        # Load metadata from bytes so the verifier neither depends on reflection-only support in
        # the current PowerShell host nor holds a file handle in the staging directory.
        $references=[Reflection.Assembly]::Load([IO.File]::ReadAllBytes($desktop)).GetReferencedAssemblies() | ForEach-Object {$_.Name}
    } catch { throw 'Could not inspect managed PeerChat.Desktop.exe assembly references: '+$_.Exception.Message }
    if($references -contains 'PeerChat.Updater.Core' -and -not (Test-Path (Join-Path $client 'PeerChat.Updater.Core.dll'))){throw 'PeerChat.Desktop.exe references PeerChat.Updater.Core but the packaged assembly is missing.'}
    $core=Join-Path $client 'PeerChat.Core.dll'
    if(-not (Test-Path $core)){throw "Packaged native core was not found: $core"}
    $objdump=(Get-Command objdump -ErrorAction Stop).Source
    $imports=@(& $objdump -p $core | ForEach-Object {if($_ -match 'DLL Name:\s*(\S+)'){$Matches[1].ToLowerInvariant()}} | Sort-Object -Unique)
    if($LASTEXITCODE -ne 0){throw 'Could not inspect the packaged native import table.'}
    $windowsDlls=@('advapi32.dll','bcrypt.dll','crypt32.dll','gdi32.dll','iphlpapi.dll','kernel32.dll','msvcrt.dll','ncrypt.dll','ole32.dll','secur32.dll','shell32.dll','user32.dll','winmm.dll','ws2_32.dll')
    $unexpected=@($imports | Where-Object {$windowsDlls -notcontains $_})
    if($unexpected.Count -ne 0){throw 'Packaged core has non-Windows dynamic dependencies: '+($unexpected -join ', ')}
    if(Test-Path -LiteralPath $screenMedia){
        $screenImports=@(& $objdump -p $screenMedia | ForEach-Object {if($_ -match 'DLL Name:\s*(\S+)'){$Matches[1].ToLowerInvariant()}} | Sort-Object -Unique)
        if($LASTEXITCODE -ne 0){throw 'Could not inspect the packaged screen-media import table.'}
        $screenWindows=@('d3d11.dll','mfplat.dll','dwmapi.dll','user32.dll','kernel32.dll','oleaut32.dll')
        $unexpectedScreen=@($screenImports | Where-Object {-not $_.StartsWith('api-ms-win-') -and $screenWindows -notcontains $_})
        if($unexpectedScreen.Count -ne 0){throw 'Packaged screen-media adapter has unexpected dynamic dependencies: '+($unexpectedScreen -join ', ')}
    }
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $smoke=Join-Path $client 'PackageNativeLoadSmoke.exe'
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$smoke" /reference:"$framework\System.dll" .\tests\PackageNativeLoadSmoke.cs
    if($LASTEXITCODE -ne 0){throw 'Package native-load smoke build failed.'}
    $savedPath=$env:PATH
    try{$env:PATH=(Join-Path $env:WINDIR 'System32')+';'+$env:WINDIR;& $smoke $core;if($LASTEXITCODE -ne 0){throw 'Package native-load smoke failed.'}}
    finally{$env:PATH=$savedPath;Remove-Item -LiteralPath $smoke -Force -ErrorAction SilentlyContinue}
    Write-Host ('PASS: Client runtime includes managed updater dependencies; PeerChat.Core.dll imports Windows components only: '+($imports -join ', '))
    if(Test-Path -LiteralPath $screenMedia){Write-Host ('PASS: PeerChat.ScreenMedia.dll imports expected Windows components only: '+($screenImports -join ', '))}
} finally {Pop-Location}
