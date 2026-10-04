param([switch]$ProvisionSources,[string]$JavaHome='C:\Program Files\Java\jdk-22')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cache=Join-Path $root 'build\deps\bc-hpke-upstream-tests-v1'
New-Item -ItemType Directory -Force -Path $cache,(Join-Path $cache 'crypto') | Out-Null
$items=@(
 @{name='HPKETestVectors.java';url='https://raw.githubusercontent.com/bcgit/bc-java/eacda831fbe15b272933e40841d8b0827b556fae/core/src/test/java/org/bouncycastle/crypto/test/HPKETestVectors.java';sha='091885e20614a8d4267b9909303504fdbaaf03d1f06c2ebbaa78a708aadd6be6'},
 @{name='TestResourceFinder.java';url='https://raw.githubusercontent.com/bcgit/bc-java/eacda831fbe15b272933e40841d8b0827b556fae/core/src/test/java/org/bouncycastle/test/TestResourceFinder.java';sha='a462d58a14b404710542f9ea10cb111ed7a12a1e1493a33455f3f7148493330a'},
 @{name='crypto\hpke.txt';url='https://raw.githubusercontent.com/bcgit/bc-test-data/3c385e9f4c0b12ce5a86d2b29a17fb3b68dd4e1a/crypto/hpke.txt';sha='f7cf1cd329400643206e4b8947e2b52b36ab83885c3124b7f32036610b24bdac'}
)
foreach($item in $items) {
    $file=Join-Path $cache $item.name
    if(!(Test-Path -LiteralPath $file)) {
        if(!$ProvisionSources){throw 'Pinned upstream test fixture missing; provision explicitly'}
        Invoke-WebRequest -Uri $item.url -OutFile $file -UseBasicParsing
    }
    if((Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -ne $item.sha){throw "Pinned upstream test changed: $($item.name)"}
}
$junit=(Get-ChildItem -LiteralPath (Join-Path $env:USERPROFILE '.gradle\caches\modules-2\files-2.1\junit\junit\4.13.2') -Filter junit-4.13.2.jar -Recurse | Select-Object -First 1).FullName
if(!$junit){throw 'Existing JUnit cache required; no automatic dependency download'}
if((Get-FileHash -LiteralPath $junit).Hash -ne '8E495B634469D64FB8ACFA3495A065CBACC8A0FFF55CE1E31007BE4C16DC57D3'){throw 'Pinned JUnit test dependency mismatch'}
$stock=Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
$hook=& (Join-Path $root 'scripts\build-bc-hpke-hook.ps1') -JavaHome $JavaHome
$utf8=[Text.UTF8Encoding]::new($false)
foreach($variant in @('stock','patched')) {
    $out=Join-Path $cache ($variant+'-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $out | Out-Null
    $test=[IO.File]::ReadAllText((Join-Path $cache 'HPKETestVectors.java'))
    if($variant -eq 'patched'){$test=$test.Replace('org.bouncycastle.crypto.hpke','com.peerchat.crypto.bc186.hpke')}
    $source=Join-Path $out 'HPKETestVectors.java';[IO.File]::WriteAllText($source,$test,$utf8)
    & "$JavaHome\bin\javac.exe" --release 8 -encoding UTF-8 -cp "$hook;$stock;$junit" -d $out $source (Join-Path $cache 'TestResourceFinder.java')
    if($LASTEXITCODE -ne 0){throw "Upstream $variant test compilation failed"}
    & "$JavaHome\bin\java.exe" "-Dbc.test.data.home=$cache" -cp "$out;$hook;$stock;$junit" junit.textui.TestRunner org.bouncycastle.crypto.test.HPKETestVectors
    if($LASTEXITCODE -ne 0){throw "Upstream $variant HPKE tests failed"}
    Write-Host "PASS: pinned upstream HPKE suite ($variant)"
}
