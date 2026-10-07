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
foreach($rid in @('linux-x64','linux-arm64','linux-arm')){
    $output=Join-Path $destination $rid
    $selfContained=$rid -eq 'linux-arm'
    & $DotNet publish (Join-Path $root 'server/portable/PeerChat.Server.csproj') -c Release -r $rid --self-contained $selfContained "-p:UseAppHost=$selfContained" -o $output
    if($LASTEXITCODE){throw "Publish failed: $rid"}
    $files=@(Get-ChildItem -LiteralPath $output -File)
    $allowed=@('PeerChat.Server.dll','PeerChat.Server.deps.json','PeerChat.Server.runtimeconfig.json')
    if(!$selfContained -and ($files.Count -ne 3 -or @($files | Where-Object {$_.Name -notin $allowed}).Count)){throw 'Unexpected packaged file'}
    $runtime=Get-Content -Raw (Join-Path $output 'PeerChat.Server.runtimeconfig.json') | ConvertFrom-Json
    $runtimeVersion=if($selfContained){$runtime.runtimeOptions.includedFrameworks[0].version}else{$runtime.runtimeOptions.framework.version}
    if($runtimeVersion -ne '10.0.12'){throw 'Runtime pin mismatch'}
    $deps=Get-Content -Raw (Join-Path $output 'PeerChat.Server.deps.json') | ConvertFrom-Json
    if(!$deps.runtimeTarget.name.EndsWith('/'+$rid)){throw 'RID mismatch'}
    $stream=[IO.File]::OpenRead((Join-Path $output 'PeerChat.Server.dll'))
    $pe=[System.Reflection.PortableExecutable.PEReader]::new($stream)
    try{
        $expectedMachine=switch($rid){'linux-arm'{'ArmThumb2'} 'linux-arm64'{'Arm64'} default{'Amd64'}}
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
    if($selfContained){
        # Only the pinned runtime pack, its notices, and the three application
        # files/apphost may enter the ARM32 self-contained package.
        $pack=Join-Path $env:USERPROFILE '.nuget/packages/microsoft.netcore.app.runtime.linux-arm/10.0.12'
        $runtimeNames=@(Get-ChildItem (Join-Path $pack 'runtimes/linux-arm') -File -Recurse | ForEach-Object Name)
        $allowed+=@('PeerChat.Server','createdump','LICENSE.txt','THIRD-PARTY-NOTICES.txt')+$runtimeNames
        if(@($files | Where-Object {$_.Name -notin $allowed}).Count){throw 'Unexpected ARM32 runtime payload'}
        foreach($file in $files){
            if($file.Name -eq 'PeerChat.Server' -or $file.Extension -eq '.so' -or $file.Name -eq 'createdump'){
                $bytes=[IO.File]::ReadAllBytes($file.FullName)
                if($bytes.Length -lt 20 -or $bytes[0] -ne 127 -or $bytes[1] -ne 69 -or $bytes[2] -ne 76 -or $bytes[3] -ne 70 -or $bytes[4] -ne 1 -or $bytes[5] -ne 1 -or [BitConverter]::ToUInt16($bytes,18) -ne 40){throw "Not little-endian ELF32 ARM: $($file.Name)"}
            }
        }
    }
    $hashes=[ordered]@{}
    foreach($file in $files){$hashes[$file.Name]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    $deployment=if($selfContained){'self-contained ARM32 apphost'}else{'framework-dependent, no apphost'}
    [ordered]@{Version=$version;Rid=$rid;Sdk='10.0.401';Runtime='10.0.12';Deployment=$deployment;LinuxExecution='pending';Files=$hashes} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination "$rid-manifest.json") -Encoding UTF8
    Compress-Archive -LiteralPath $files.FullName -DestinationPath (Join-Path $destination "PeerChat.Server-$version-$rid.zip")
    Write-Host "PASS: $rid build/version/runtime-only package. Linux execution still required."
}
