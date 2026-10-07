param([string]$DotNet='C:\Program Files\dotnet\dotnet.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if((& $DotNet --version) -ne '10.0.401'){throw 'Pinned SDK 10.0.401 required'}
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'shared/VersionInfo.cs')
if($source -notmatch 'Display="([^"]+)"'){throw 'Authoritative version unavailable'}
$version=$Matches[1]
$destination=Join-Path $root "build/linux/$version"
if(Test-Path -LiteralPath $destination){throw 'Linux candidate exists; never overwrite an immutable candidate'}
New-Item -ItemType Directory -Path $destination | Out-Null
foreach($rid in @('linux-x64','linux-arm64')){
    $output=Join-Path $destination $rid
    & $DotNet publish (Join-Path $root 'server/portable/PeerChat.Server.csproj') -c Release -r $rid --self-contained false -p:UseAppHost=false -o $output
    if($LASTEXITCODE){throw "Publish failed: $rid"}
    $files=@(Get-ChildItem -LiteralPath $output -File)
    $allowed=@('PeerChat.Server.dll','PeerChat.Server.deps.json','PeerChat.Server.runtimeconfig.json')
    if($files.Count -ne 3 -or @($files | Where-Object {$_.Name -notin $allowed}).Count){throw 'Unexpected packaged file'}
    $runtime=Get-Content -Raw (Join-Path $output 'PeerChat.Server.runtimeconfig.json') | ConvertFrom-Json
    if($runtime.runtimeOptions.framework.version -ne '10.0.12'){throw 'Runtime pin mismatch'}
    $deps=Get-Content -Raw (Join-Path $output 'PeerChat.Server.deps.json') | ConvertFrom-Json
    if(!$deps.runtimeTarget.name.EndsWith('/'+$rid)){throw 'RID mismatch'}
    $stream=[IO.File]::OpenRead((Join-Path $output 'PeerChat.Server.dll'))
    $pe=[System.Reflection.PortableExecutable.PEReader]::new($stream)
    try{
        $expectedMachine=if($rid -eq 'linux-arm64'){'Arm64'}else{'Amd64'}
        if($pe.PEHeaders.CoffHeader.Machine.ToString() -ne $expectedMachine){throw 'Assembly architecture mismatch'}
        $metadata=[System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
        $reported=$null
        foreach($handle in $metadata.TypeDefinitions){
            $type=$metadata.GetTypeDefinition($handle)
            if($metadata.GetString($type.Name) -eq 'VersionInfo'){
                foreach($fieldHandle in $type.GetFields()){
                    $field=$metadata.GetFieldDefinition($fieldHandle)
                    if($metadata.GetString($field.Name) -eq 'Display'){
                        $constant=$metadata.GetConstant($field.GetDefaultValue())
                        $reported=[Text.Encoding]::Unicode.GetString($metadata.GetBlobBytes($constant.Value))
                    }
                }
            }
        }
        if($reported -ne $version){throw 'Assembly version mismatch'}
    }finally{$pe.Dispose();$stream.Dispose()}
    $hashes=[ordered]@{}
    foreach($file in $files){$hashes[$file.Name]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    [ordered]@{Version=$version;Rid=$rid;Sdk='10.0.401';Runtime='10.0.12';Deployment='framework-dependent, no apphost';LinuxExecution='pending';Files=$hashes} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination "$rid-manifest.json") -Encoding UTF8
    Compress-Archive -LiteralPath $files.FullName -DestinationPath (Join-Path $destination "PeerChat.Server-$version-$rid.zip")
    Write-Host "PASS: $rid build/version/runtime-only package. Linux execution still required."
}
