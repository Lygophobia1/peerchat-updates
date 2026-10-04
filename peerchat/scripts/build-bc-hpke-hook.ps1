param([switch]$ProvisionSources,[string]$JavaHome='C:\Program Files\Java\jdk-22')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$definition=Join-Path $root 'third_party\bc-hpke'
$pins=Get-Content -Raw -LiteralPath (Join-Path $definition 'source-pins.json') | ConvertFrom-Json
$cache=Join-Path $root ('build\deps\bc-hpke-'+$pins.commit)
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
foreach($entry in $pins.files.PSObject.Properties) {
    $path=Join-Path $cache $entry.Name
    if(!(Test-Path -LiteralPath $path)) {
        if(!$ProvisionSources){throw "Missing pinned source $($entry.Name); run explicit -ProvisionSources once"}
        $relative=if($entry.Name -eq 'LICENSE.html'){'LICENSE.html'}else{'core/src/main/java/org/bouncycastle/crypto/hpke/'+$entry.Name}
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/bcgit/bc-java/$($pins.commit)/$relative" -OutFile $path -UseBasicParsing
    }
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw "Upstream source hash mismatch: $($entry.Name)"}
}
$bc=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
if((Get-FileHash -LiteralPath $bc).Hash -ne '2AF190B300CBB0B35E248CCF5F4A06B6072030AEB3DA7A98EC73ABE5B4CB371F'){throw 'Stock BC artifact hash mismatch'}
$stage=Join-Path $cache ('proof-'+[Guid]::NewGuid().ToString('N'))
$source=Join-Path $stage 'source'; $classes=Join-Path $stage 'classes'
New-Item -ItemType Directory -Path $source,$classes | Out-Null
Copy-Item -LiteralPath @($pins.files.PSObject.Properties | Where-Object {$_.Name -like '*.java'} | ForEach-Object {Join-Path $cache $_.Name}) -Destination $source
$gitRoot=(& git -C $root rev-parse --show-toplevel).Trim()
if($LASTEXITCODE -ne 0){throw 'Repository root unavailable'}
$relativeSource=[IO.Path]::GetRelativePath($gitRoot,$source).Replace('\','/')
if($relativeSource.StartsWith('../')){throw 'Generated source must remain inside this worktree'}
Push-Location $gitRoot
try {
    & git apply --unidiff-zero --recount "--directory=$relativeSource" (Join-Path $definition 'external-dh.patch')
    if($LASTEXITCODE -ne 0){throw 'Exact upstream hook patch failed'}
} finally {Pop-Location}
if([IO.File]::ReadAllText((Join-Path $source 'DHKEM.java')) -notmatch 'boolean external = kpR.getPrivate\(\) instanceof ExternalP256PrivateKey'){throw 'External DH seam absent; do not compile stock receiver by accident'}
Copy-Item -LiteralPath (Join-Path $definition 'ExternalP256PrivateKey.java') -Destination $source
# Mechanical package relocation ONLY. Keep the signed stock provider jar intact:
# no split package, provider replacement, signature stripping or crypto changes.
foreach($file in Get-ChildItem -LiteralPath $source -Filter '*.java') {
    $upstream=[IO.File]::ReadAllText($file.FullName)
    [IO.File]::WriteAllText($file.FullName,$upstream.Replace('org.bouncycastle.crypto.hpke','com.peerchat.crypto.bc186.hpke'),$utf8)
}
$sources=@(Get-ChildItem -LiteralPath $source -Filter '*.java' | Sort-Object Name | ForEach-Object {$_.FullName})
& "$JavaHome\bin\javac.exe" --release 8 -encoding UTF-8 -cp $bc -d $classes $sources
if($LASTEXITCODE -ne 0){throw 'Patched upstream HPKE compilation failed'}
$metadata=Join-Path $classes 'META-INF'; New-Item -ItemType Directory -Path $metadata | Out-Null
foreach($input in @((Join-Path $cache 'LICENSE.html'),(Join-Path $definition 'source-pins.json'),(Join-Path $definition 'external-dh.patch'))) {
    [IO.File]::WriteAllText((Join-Path $metadata ([IO.Path]::GetFileName($input))),[IO.File]::ReadAllText($input).Replace("`r`n","`n"),$utf8)
}
$jar=Join-Path $stage 'peerchat-bc-hpke-1.86-external-dh-v1.jar'
& "$JavaHome\bin\jar.exe" --create --file $jar --date=2026-01-01T00:00:00Z -C $classes .
if($LASTEXITCODE -ne 0){throw 'HPKE artifact creation failed'}
$provenance=[ordered]@{upstream=$pins.commit; stockBcSha256=(Get-FileHash $bc).Hash; patchSha256=(Get-FileHash (Join-Path $definition 'external-dh.patch')).Hash; handleSha256=(Get-FileHash (Join-Path $definition 'ExternalP256PrivateKey.java')).Hash; artifactSha256=(Get-FileHash $jar).Hash; relocation='com.peerchat.crypto.bc186.hpke'; javaRelease=8}
[IO.File]::WriteAllText((Join-Path $stage 'provenance.json'),($provenance | ConvertTo-Json),$utf8)
$published=Join-Path $cache ('artifacts\'+$provenance.artifactSha256)
New-Item -ItemType Directory -Force -Path $published | Out-Null
$artifact=Join-Path $published 'peerchat-bc-hpke-1.86-external-dh-v1.jar'
if(Test-Path -LiteralPath $artifact) {
    if((Get-FileHash $artifact).Hash -ne $provenance.artifactSha256){throw 'Immutable hook artifact damaged'}
} else {Copy-Item -LiteralPath $jar -Destination $artifact}
Write-Host "PASS: isolated patched-upstream artifact $jar"
Write-Output $artifact
