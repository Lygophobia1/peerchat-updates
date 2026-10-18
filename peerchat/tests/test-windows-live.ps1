# Manual probe only. Example:
# $credential=Get-Credential codex
# ./tests/test-windows-live.ps1 -Profile ./build/tests/windows-live-profiles/codex -Account codex -Credential $credential -Command status
# The account password travels to the child process through stdin. Native profile keys and
# the production DPAPI-protected remembered-device credential stay in the isolated profile.
param(
    [Parameter(Mandatory=$true)][string]$Profile,
    [Parameter(Mandatory=$true)][ValidateSet('codex','codex1')][string]$Account,
    [Parameter(Mandatory=$true)][pscredential]$Credential,
    [Parameter(Mandatory=$true)][ValidateSet('status','create-group','room','send')][string]$Command,
    [string]$GroupId,
    [string]$Name,
    [string]$Message,
    [ValidateSet('codex','codex1')][string]$Invite,
    [switch]$Join,
    [switch]$Media,
    [ValidateRange(0,600)][int]$HoldSeconds=180,
    [string]$HostName='173.225.14.37',
    [int]$Port=7443,
    [string]$CertificateSha256='62a181282fe940f03f6a35541220a05f9ed2cb85a41f6de3b0df111286ba17e3',
    [string]$Package
)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(-not $Package){$Package=Join-Path $project 'build/development/4.0.2-dev.184/Package'}
$Package=(Resolve-Path -LiteralPath $Package).Path
$Profile=[IO.Path]::GetFullPath($Profile)
$allowedLocal=[IO.Path]::GetFullPath((Join-Path $project 'build/tests/windows-live-profiles'))
$allowedExternal=[IO.Path]::GetFullPath('F:\PeerChat-isolated-cache-tests')
$underLocal=$Profile.StartsWith(($allowedLocal.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)
$underExternal=$Profile.StartsWith(($allowedExternal.TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)
if(-not($underLocal -or $underExternal)){throw 'Probe profile must be under build/tests/windows-live-profiles or F:\PeerChat-isolated-cache-tests'}
if($Credential.UserName -ne $Account){throw 'Credential username must match -Account'}
if(-not(Test-Path -LiteralPath $Profile)){[void](New-Item -ItemType Directory -Path $Profile -Force)}
$marker=Join-Path $Profile 'windows-live-probe.marker'
if(-not(Test-Path -LiteralPath $marker)){
    if(@(Get-ChildItem -LiteralPath $Profile -Force).Count -ne 0){throw 'Existing nonempty profile lacks isolated probe marker'}
    [IO.File]::WriteAllText($marker,'PeerChat Windows live probe isolated profile')
}
if($Command -eq 'create-group' -and -not $Name){throw 'create-group requires -Name'}
if($Command -eq 'room' -and (-not $GroupId -or -not $Name)){throw 'room requires -GroupId and -Name'}
if($Command -eq 'send' -and (-not $GroupId -or -not $Message)){throw 'send requires -GroupId and -Message'}
if($Invite -and $Command -ne 'create-group'){throw '-Invite applies only to create-group'}
if($Join -and $Command -ne 'room'){throw '-Join applies only to room'}
if($Media -and ($Command -ne 'room' -or -not $Join -or $HoldSeconds -le 0)){throw '-Media requires -Command room, -Join, and positive -HoldSeconds'}
$framework=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$compiler=Join-Path $framework 'csc.exe'
if(-not(Test-Path -LiteralPath $compiler)){throw 'Windows .NET Framework compiler is unavailable'}
$runtime=Join-Path $project ('build/tests/windows-live-runtime-'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $runtime)
try {
    foreach($file in Get-ChildItem -LiteralPath $Package -File){
        if($file.Extension -eq '.dll' -or $file.Extension -eq '.exe' -or $file.Name -eq 'PeerChat.Desktop.exe.config'){
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $runtime $file.Name)
        }
    }
    $desktop=Join-Path $runtime 'PeerChat.Desktop.exe'
    if(-not(Test-Path -LiteralPath $desktop)){throw 'Package lacks PeerChat.Desktop.exe'}
    $probe=Join-Path $runtime 'windows_live_probe.exe'
    & $compiler /nologo /target:exe /platform:x64 "/out:$probe" "/reference:$desktop" "/reference:$(Join-Path $framework 'System.Web.Extensions.dll')" (Join-Path $PSScriptRoot 'windows_live_probe.cs')
    if($LASTEXITCODE -ne 0){throw 'Windows live probe compilation failed'}
    $extra=@()
    if($Command -eq 'create-group'){$extra=@($Name);if($Invite){$extra+= $Invite}}
    if($Command -eq 'room'){$extra=@($GroupId,$Name,$(if($Join){'join'}else{'observe'}),$HoldSeconds);if($Media){$extra+='media'}}
    if($Command -eq 'send'){$extra=@($GroupId,$Message)}
    $Credential.GetNetworkCredential().Password | & $probe $HostName $Port $CertificateSha256 $Profile $Account $Command @extra
    if($LASTEXITCODE -ne 0){throw "Windows live probe exited with code $LASTEXITCODE"}
} finally {
    Write-Host "Isolated Windows probe runtime retained at $runtime"
}
