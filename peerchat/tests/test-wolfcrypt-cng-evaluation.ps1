param(
    [switch]$BuildEvaluationDependency,
    [string]$JavaHome = 'C:\Program Files\Java\jdk-22',
    [string]$GccRoot = 'C:\TDM-GCC-64',
    [string]$CmakeRoot = 'C:\Users\Livel\AppData\Local\Android\Sdk\cmake\3.22.1\bin'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$evaluation = Join-Path $root 'build\deps\wolfcrypt-evaluation'
$revision = '3c5eead44904df64e6a5a1f4ebdce377d35a849a'
$source = Join-Path $evaluation "wolfssl-$revision"
$archive = Join-Path $evaluation "$revision.zip"
$native = Join-Path $evaluation 'mingw-x64'
$stock = Join-Path $root 'build\deps\bouncycastle-1.86.1-jdk18on\bcprov-jdk18on-1.86.jar'
$patched = Join-Path $root 'build\deps\bc-hpke-eacda831fbe15b272933e40841d8b0827b556fae\artifacts\4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C\peerchat-bc-hpke-1.86-external-dh-v1.jar'
function Assert-Hash([string]$Path, [string]$Hash) {
    if (!(Test-Path -LiteralPath $Path) -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Hash) { throw "Missing/invalid evaluation input: $Path" }
}
Assert-Hash $archive 'C20D50CE143CBF2E8DBBD91E1035C75E99112DB7774D399E969894BE036367A7'
Assert-Hash $stock '2AF190B300CBB0B35E248CCF5F4A06B6072030AEB3DA7A98EC73ABE5B4CB371F'
Assert-Hash $patched '4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C'
# Check every extracted upstream source against the checksum-pinned archive.
# No download or source patch happens in this test entry point.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $relative = $entry.FullName.Substring($entry.FullName.IndexOf('/') + 1)
        $file = Join-Path $source $relative
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing pinned source: $relative" }
        $stream = $entry.Open(); $digest = [Security.Cryptography.SHA256]::Create()
        try { $expected = [BitConverter]::ToString($digest.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $digest.Dispose() }
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected) { throw "Modified upstream source: $relative" }
    }
} finally { $zip.Dispose() }
if ($BuildEvaluationDependency) {
    $cmake = Join-Path $CmakeRoot 'cmake.exe'
    & $cmake -S $source -B $native -G Ninja "-DCMAKE_MAKE_PROGRAM=$(Join-Path $CmakeRoot 'ninja.exe')" "-DCMAKE_C_COMPILER=$(Join-Path $GccRoot 'bin\gcc.exe')" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DWOLFSSL_CRYPT_ONLY=yes '-DCMAKE_C_FLAGS=-DWOLFCRYPT_ONLY -DNO_CERTS' -DWOLFSSL_HPKE=yes -DWOLFSSL_CRYPTOCB=yes -DWOLFSSL_AESGCM=yes -DWOLFSSL_HKDF=yes -DWOLFSSL_EXAMPLES=no -DWOLFSSL_CRYPT_TESTS=no -DWOLFSSL_ASM=no
    if ($LASTEXITCODE -ne 0) { throw "Evaluation configuration failed: $LASTEXITCODE" }
    & $cmake --build $native --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Evaluation dependency build failed: $LASTEXITCODE" }
}
$library = Join-Path $native 'libwolfssl.a'
if (!(Test-Path -LiteralPath $library)) { throw 'Build the evaluation-only dependency with -BuildEvaluationDependency first' }
$exe = Join-Path $evaluation 'wolfcrypt-cng-evaluation.exe'
& (Join-Path $GccRoot 'bin\g++.exe') -std=c++17 -D_WIN32_WINNT=0x0601 -DWOLFCRYPT_ONLY -DNO_CERTS -O2 -Wall -Wextra -I $native -I $source (Join-Path $PSScriptRoot 'wolfcrypt_cng_evaluation.cpp') $library -lncrypt -lbcrypt -lcrypt32 -lws2_32 -static-libgcc -static-libstdc++ -o $exe
if ($LASTEXITCODE -ne 0) { throw "Native evaluation compile failed: $LASTEXITCODE" }
$classpath = "$stock;$patched"
& (Join-Path $JavaHome 'bin\javac.exe') -cp $classpath -d $evaluation (Join-Path $PSScriptRoot 'tls-host\WolfCryptCngEvaluation.java')
if ($LASTEXITCODE -ne 0) { throw "Oracle compile failed: $LASTEXITCODE" }
$before = @(& $exe --list-test-keys)
if ($LASTEXITCODE -ne 0) { throw 'Test container inventory failed' }
& (Join-Path $JavaHome 'bin\java.exe') -cp "$evaluation;$classpath" WolfCryptCngEvaluation $exe
if ($LASTEXITCODE -ne 0) { throw "Native/BC evaluation failed: $LASTEXITCODE" }
$after = @(& $exe --list-test-keys)
$beforeText = @($before | Sort-Object) -join [Environment]::NewLine
$afterText = @($after | Sort-Object) -join [Environment]::NewLine
if ($LASTEXITCODE -ne 0 -or $beforeText -ne $afterText) { throw 'Evaluation test key was not cleaned up' }
Write-Output 'PASS: unchanged pinned upstream sources; host interoperability; namespaced CNG test-key cleanup'
Write-Output 'PENDING: licensing approval, physical Android Keystore interoperability, production integration/security review'
