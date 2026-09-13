[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$UnsignedManifestPath,
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [Parameter(Mandatory=$true)][string]$OutputManifestPath,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$UpdaterCorePath,
    [string]$KeyId='peerchat-release-2026-01'
)

$ErrorActionPreference='Stop'

function Stop-Signing([string]$Message) { throw $Message }

$unsigned=(Resolve-Path -LiteralPath $UnsignedManifestPath -ErrorAction Stop).Path
$package=(Resolve-Path -LiteralPath $PackagePath -ErrorAction Stop).Path
$updaterCore=(Resolve-Path -LiteralPath $UpdaterCorePath -ErrorAction Stop).Path
$output=[IO.Path]::GetFullPath($OutputManifestPath)
if($unsigned -eq $output){Stop-Signing 'Signed output must not overwrite the unsigned manifest input.'}
if(Test-Path -LiteralPath $output){Stop-Signing "Refusing to overwrite an existing signed manifest: $output"}
if(!(Test-Path -LiteralPath (Split-Path $output -Parent) -PathType Container)){Stop-Signing 'Signed manifest output directory does not exist.'}
if($KeyId -notmatch '^[0-9A-Za-z._-]+$'){Stop-Signing 'Release signing key ID contains unsupported characters.'}

$thumbprint=($CertificateThumbprint -replace '[^0-9A-Fa-f]','').ToUpperInvariant()
if($thumbprint.Length -ne 40){Stop-Signing 'Release signing certificate thumbprint must contain exactly 40 hexadecimal characters.'}

Add-Type -Path $updaterCore
$configuration=[PeerChat.Updater.UpdateFeedConfiguration]::CreateDefaults()
$configuration.Channel='release'
$configuration.ManifestUrl='https://raw.githubusercontent.com/Lygophobia1/peerchat-updates/main/release/manifest.json'
$configuration.RequireSignature=$true
$configuration.Validate()
$manifest=[PeerChat.Updater.UpdateJson]::ParseManifest([IO.File]::ReadAllText($unsigned),$configuration)
if($manifest.Signature){Stop-Signing 'Unsigned manifest input unexpectedly already contains signature metadata.'}
if((Get-Item -LiteralPath $package).Length -ne $manifest.PackageSize -or
   ![PeerChat.Updater.PackageHashVerifier]::VerifySha256($package,$manifest.PackageSha256)){
    Stop-Signing 'Unsigned Release manifest package size or SHA-256 does not match the selected package.'
}

$certificate=Get-Item -LiteralPath "Cert:\CurrentUser\My\$thumbprint" -ErrorAction SilentlyContinue
if(!$certificate -or !$certificate.HasPrivateKey){Stop-Signing 'The requested Release signing certificate/private key is not available in Cert:\CurrentUser\My.'}
$now=[DateTime]::UtcNow
if($certificate.NotBefore.ToUniversalTime() -gt $now -or $certificate.NotAfter.ToUniversalTime() -le $now){Stop-Signing 'The requested Release signing certificate is outside its validity period.'}
$keyUsage=@($certificate.Extensions | Where-Object {$_.Oid.Value -eq '2.5.29.15'} | Select-Object -First 1)
if($keyUsage.Count -eq 1 -and ($keyUsage[0].KeyUsages -band [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature) -eq 0){
    Stop-Signing 'The requested certificate is not authorized for digital signatures.'
}

$rsa=[Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
if(!$rsa){Stop-Signing 'The requested Release signing certificate does not contain an RSA private key.'}
try {
    if($rsa.KeySize -lt 3072){Stop-Signing 'The Release RSA signing key must be at least 3072 bits.'}
    $signature=$rsa.SignData($manifest.GetSignaturePayload(),[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
} finally {$rsa.Dispose()}

$manifest.Signature=New-Object PeerChat.Updater.UpdateSignature
$manifest.Signature.Algorithm=[PeerChat.Updater.TrustedReleaseKeys]::RsaSha256Pkcs1Algorithm
$manifest.Signature.KeyId=$KeyId
$manifest.Signature.Value=[Convert]::ToBase64String($signature)

$verifier=New-Object PeerChat.Updater.SignatureBoundaryVerifier -ArgumentList $true
$failure=$null
if(!$verifier.Verify($manifest,$package,[ref]$failure)){Stop-Signing "Generated Release signature failed the packaged updater verifier: $failure"}

$utf8WithoutBom=New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($output,[PeerChat.Updater.UpdateJson]::Serialize($manifest)+[Environment]::NewLine,$utf8WithoutBom)
$written=[PeerChat.Updater.UpdateJson]::ParseManifest([IO.File]::ReadAllText($output),$configuration)
$failure=$null
if(!$verifier.Verify($written,$package,[ref]$failure)){Stop-Signing "Written Release manifest failed final verification: $failure"}

[pscustomobject]@{
    ManifestPath=$output
    Algorithm=$manifest.Signature.Algorithm
    KeyId=$KeyId
    CertificateThumbprint=$thumbprint
    PackageSize=$manifest.PackageSize
    PackageSha256=$manifest.PackageSha256
} | Format-List
