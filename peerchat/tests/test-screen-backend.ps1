param([Parameter(Mandatory=$true)][string]$DevelopmentDirectory,[switch]$Capture)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$development=(Resolve-Path -LiteralPath $DevelopmentDirectory).Path
$adapter=Join-Path $development 'ScreenMedia\PeerChat.ScreenMedia.dll'
if(-not(Test-Path -LiteralPath $adapter)){throw "ScreenMedia DLL missing: $adapter"}
$output=Join-Path $development 'screen-backend-selection-test.exe'
Push-Location $root
try {
    & g++ -std=c++17 -Wall -Wextra -Werror -I. tests\screen_backend_selection_test.cpp -o $output
    if($LASTEXITCODE -ne 0){throw 'Screen backend selection test compilation failed'}
    & $output $adapter
    if($LASTEXITCODE -ne 0){throw 'Screen backend selection test failed'}
    if($Capture){
        $captureTest=Join-Path $development 'screen-backend-capture-test.exe'
        & g++ -std=c++17 -Wall -Wextra -Werror -I. tests\screen_backend_capture_test.cpp -o $captureTest
        if($LASTEXITCODE -ne 0){throw 'Screen backend capture test compilation failed'}
        & $captureTest $adapter
        if($LASTEXITCODE -ne 0){throw "Screen backend capture test failed (exit $LASTEXITCODE)"}
    }
} finally {Pop-Location}
