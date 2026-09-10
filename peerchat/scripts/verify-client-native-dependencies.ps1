param([string]$ClientDirectory='releases\v0.3-beta\Client')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $client=Resolve-Path $ClientDirectory
    $core=Join-Path $client 'PeerChat.Core.dll'
    if(-not (Test-Path $core)){throw "Packaged native core was not found: $core"}
    $objdump=(Get-Command objdump -ErrorAction Stop).Source
    $imports=@(& $objdump -p $core | ForEach-Object {if($_ -match 'DLL Name:\s*(\S+)'){$Matches[1].ToLowerInvariant()}} | Sort-Object -Unique)
    if($LASTEXITCODE -ne 0){throw 'Could not inspect the packaged native import table.'}
    $windowsDlls=@('advapi32.dll','bcrypt.dll','crypt32.dll','gdi32.dll','iphlpapi.dll','kernel32.dll','msvcrt.dll','ncrypt.dll','ole32.dll','secur32.dll','shell32.dll','user32.dll','winmm.dll','ws2_32.dll')
    $unexpected=@($imports | Where-Object {$windowsDlls -notcontains $_})
    if($unexpected.Count -ne 0){throw 'Packaged core has non-Windows dynamic dependencies: '+($unexpected -join ', ')}
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $smoke=Join-Path $client 'PackageNativeLoadSmoke.exe'
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$smoke" /reference:"$framework\System.dll" .\tests\PackageNativeLoadSmoke.cs
    if($LASTEXITCODE -ne 0){throw 'Package native-load smoke build failed.'}
    $savedPath=$env:PATH
    try{$env:PATH=(Join-Path $env:WINDIR 'System32')+';'+$env:WINDIR;& $smoke $core;if($LASTEXITCODE -ne 0){throw 'Package native-load smoke failed.'}}
    finally{$env:PATH=$savedPath;Remove-Item -LiteralPath $smoke -Force -ErrorAction SilentlyContinue}
    Write-Host ('PASS: PeerChat.Core.dll imports Windows components only: '+($imports -join ', '))
} finally {Pop-Location}
