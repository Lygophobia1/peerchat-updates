$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.IO.Compression.FileSystem
$hash='4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C'
$jar=Join-Path $root "build\deps\bc-hpke-eacda831fbe15b272933e40841d8b0827b556fae\artifacts\$hash\peerchat-bc-hpke-1.86-external-dh-v1.jar"
if((Get-FileHash -LiteralPath $jar).Hash -ne $hash){throw 'HPKE artifact changed'}
$zip=[IO.Compression.ZipFile]::OpenRead($jar)
try {
    foreach($entry in $zip.Entries){
        if($entry.FullName.EndsWith('.class') -and !$entry.FullName.StartsWith('com/peerchat/crypto/bc186/hpke/')){throw 'Unexpected HPKE jar package'}
        if($entry.FullName -match '\.(dll|so|key|pfx|pem)$' -or $entry.FullName -match 'Proof|Fixture|Test'){throw 'Unexpected runtime/test material in HPKE jar'}
    }
    foreach($name in @('META-INF/LICENSE.html','META-INF/source-pins.json','META-INF/external-dh.patch')){
        if(!$zip.GetEntry($name)){throw 'HPKE provenance/license missing'}
    }
} finally {$zip.Dispose()}
foreach($relative in @('android/app/build/outputs/apk/debug/app-debug.apk','android/app/build/outputs/apk/release/app-release-unsigned.apk')) {
    $zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $root $relative))
    try {
        $found=$false
        foreach($entry in $zip.Entries | Where-Object {$_.FullName -match '^classes[0-9]*\.dex$'}) {
            $stream=$entry.Open();$buffer=[IO.MemoryStream]::new()
            try {$stream.CopyTo($buffer);$text=[Text.Encoding]::ASCII.GetString($buffer.ToArray())}
            finally {$stream.Dispose();$buffer.Dispose()}
            if($text.Contains('Lcom/peerchat/crypto/bc186/hpke/ExternalP256PrivateKey;')){$found=$true}
            foreach($fixture in @('NativeCngHpkeFixture','OpaqueHpkeHookProof','OpaqueRecipientHpkeTest','RecipientKeyDeviceTest')) {
                if($text.Contains($fixture)){throw 'HPKE test fixture in runtime APK'}
            }
        }
        if(!$found){throw 'Patched upstream opaque HPKE capability missing from APK'}
        foreach($name in @('META-INF/LICENSE.html','META-INF/source-pins.json','META-INF/external-dh.patch')){
            if(!$zip.GetEntry($name)){throw 'Packaged HPKE provenance/license missing'}
        }
    } finally {$zip.Dispose()}
}
Write-Host 'PASS: hash-pinned isolated HPKE artifact, packaged provenance/license, debug/release adapter present, test fixtures excluded; no additional native TLS dependency.'
