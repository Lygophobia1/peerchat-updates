param([Parameter(Mandatory=$true)][string]$OutputDirectory,
      [string]$ToolchainArchive=$env:PEERCHAT_GO_TOOLCHAIN_ARCHIVE)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cache=Join-Path $root 'build\go-toolchain'
New-Item -ItemType Directory -Force -Path $cache,$OutputDirectory | Out-Null
$archive=Join-Path $cache 'go1.26.8.windows-amd64.zip'
$hash='B92C3B2ADAE85A11BA71FE7216DAF0D84E82AF4C8AB6C5625807F28622043A59'
if(!(Test-Path -LiteralPath $archive)){
    if($ToolchainArchive){Copy-Item -LiteralPath $ToolchainArchive -Destination $archive}
    else {Invoke-WebRequest 'https://go.dev/dl/go1.26.8.windows-amd64.zip' -OutFile $archive -UseBasicParsing}
}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $hash){throw 'Pinned Go archive hash mismatch'}
$go=Join-Path $cache 'go\bin\go.exe'
if(!(Test-Path -LiteralPath $go)){Expand-Archive -LiteralPath $archive -DestinationPath $cache}
if((& $go version) -ne 'go version go1.26.8 windows/amd64'){throw 'Pinned Go version mismatch'}
$names=@('GOTOOLCHAIN','CGO_ENABLED','CC','GOCACHE','GOOS','GOARCH','GOFLAGS')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    $env:GOTOOLCHAIN='local';$env:CGO_ENABLED='1';$env:CC='C:\TDM-GCC-64\bin\gcc.exe'
    $env:GOCACHE=Join-Path $root 'build\go-cache';$env:GOOS='windows';$env:GOARCH='amd64';$env:GOFLAGS=''
    Push-Location (Join-Path $root 'platform\go_hpke')
    try {
        & $go build -trimpath -buildmode=c-shared '-ldflags=-s -w' -o (Join-Path $OutputDirectory 'PeerChat.Hpke.dll') .
        if($LASTEXITCODE -ne 0){throw "Go HPKE build failed ($LASTEXITCODE)"}
    } finally {Pop-Location}
    # cgo's header is a build product, not a runtime dependency.
    Remove-Item -LiteralPath (Join-Path $OutputDirectory 'PeerChat.Hpke.h')
    $licenses=Join-Path $OutputDirectory 'licenses';New-Item -ItemType Directory -Force -Path $licenses|Out-Null
    Copy-Item (Join-Path $cache 'go\LICENSE') (Join-Path $licenses 'Go-LICENSE.txt')
    foreach($module in @('crypto','sys')){Copy-Item (Join-Path $cache "go\src\vendor\golang.org\x\$module\LICENSE") (Join-Path $licenses "golang-x-$module-LICENSE.txt")}
    Copy-Item (Join-Path $cache 'go\src\crypto\internal\fips140\nistec\fiat\README') (Join-Path $licenses 'fiat-crypto-NOTICE.txt')
    foreach($notice in @('COPYING.MinGW-w64-runtime.txt','COPYING.RUNTIME-gcc-tdm.txt','COPYING3-gcc-tdm.txt')){Copy-Item (Join-Path 'C:\TDM-GCC-64' $notice) $licenses}
} finally {foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
