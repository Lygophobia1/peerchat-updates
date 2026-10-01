param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$client=(Resolve-Path -LiteralPath $ClientDirectory).Path
$core=Join-Path $client 'PeerChat.Core.dll'
if(-not(Test-Path -LiteralPath $core)){throw "Current-worktree PeerChat.Core.dll is missing: $core"}
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$csc=Join-Path $fx 'csc.exe'
$output=Join-Path $root 'build\tests\group-registration'
$native=Join-Path $output 'native'
New-Item -ItemType Directory -Force -Path $native|Out-Null
Push-Location $root
try{
    & $csc /nologo /target:exe /platform:x64 ("/out:"+(Join-Path $output 'group-registration-test.exe')) ('/reference:'+(Join-Path $fx 'System.Web.Extensions.dll')) ('/reference:'+(Join-Path $fx 'System.Security.dll')) .\desktop\ProfileAccounts.cs .\desktop\DiscoveryClient.cs .\tests\group_registration_test.cs
    if($LASTEXITCODE -ne 0){throw 'Group registration policy test did not compile.'}
    & (Join-Path $output 'group-registration-test.exe')
    if($LASTEXITCODE -ne 0){throw 'Group registration policy test failed.'}
    Copy-Item -LiteralPath $core -Destination (Join-Path $native 'PeerChat.Core.dll') -Force
    & $csc /nologo /target:exe /platform:x64 /main:GroupCreationNativeContractTest ("/out:"+(Join-Path $native 'group-creation-native-contract-test.exe')) ('/reference:'+(Join-Path $fx 'System.Web.Extensions.dll')) .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\PeerChat.Server.cs .\tests\group_creation_native_contract_test.cs
    if($LASTEXITCODE -ne 0){throw 'Native-to-server group registration contract test did not compile.'}
    & (Join-Path $native 'group-creation-native-contract-test.exe')
    if($LASTEXITCODE -ne 0){throw 'Native-to-server group registration contract test failed.'}
}finally{Pop-Location}
