param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
$dll=Join-Path $PackageDirectory 'PeerChat.Hpke.dll'
if(!(Test-Path -LiteralPath $dll)){throw 'Production HPKE runtime missing'}
$pe=@(& 'C:\TDM-GCC-64\bin\objdump.exe' -p $dll)
if($LASTEXITCODE -ne 0 -or ($pe -join "\n") -notmatch 'pei-x86-64'){throw 'HPKE must be Windows x64 PE'}
$imports=@($pe | ForEach-Object {if($_ -match 'DLL Name:\s*(\S+)'){$Matches[1].ToLowerInvariant()}})
if(@($imports|Where-Object {@('kernel32.dll','msvcrt.dll') -notcontains $_}).Count){throw 'Unexpected HPKE runtime dependency'}
$exports=@($pe | ForEach-Object {if($_ -match '^\s*\[\s*\d+\]\s+(\S+)$'){$Matches[1]}})
if($exports.Count -ne 3){throw 'Unexpected HPKE export surface'}
foreach($name in @('pc_go_hpke_ping','pc_go_hpke_seal','pc_go_hpke_open')){if($exports -notcontains $name){throw "HPKE ABI missing: $name"}}
foreach($name in @('Go-LICENSE.txt','golang-x-crypto-LICENSE.txt','golang-x-sys-LICENSE.txt','fiat-crypto-NOTICE.txt','COPYING.MinGW-w64-runtime.txt','COPYING.RUNTIME-gcc-tdm.txt','COPYING3-gcc-tdm.txt')){
 if(!(Test-Path -LiteralPath (Join-Path $PackageDirectory "licenses\$name"))){throw "Missing notice: $name"}
}
$unexpected=@(Get-ChildItem -LiteralPath $PackageDirectory -File -Recurse | Where-Object {$_.Extension -match '^\.(a|o|obj|h|go|pdb|jar|apk)$' -or $_.Name -match 'test|probe|fixture|evaluation|go\.exe'})
if($unexpected.Count){throw ('Build/test artifacts in runtime: '+($unexpected.Name -join ', '))}
Write-Host 'PASS: production Go HPKE x64 ABI, system-only imports, notices and runtime-only contents'
