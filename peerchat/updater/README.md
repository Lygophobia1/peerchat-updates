# PeerChat updater

The updater is a separate executable. PeerChat downloads and verifies a package, stages it under
`%LOCALAPPDATA%\PeerChat\Updates`, starts a copied updater launcher outside the client install
directory, and then exits. The launcher replaces the sibling install directory, starts the new
client, and rolls back if that client does not confirm healthy after native profile/core startup.

The client never replaces loaded files in-process. Local profiles, encrypted history, account
sessions, trust, and discovery state are not update payloads and are not moved by this process.

For the full operator/developer publication, test, recovery, and troubleshooting checklist, see
[UPDATER_USER_GUIDE.md](UPDATER_USER_GUIDE.md).

## Feed contract

Each channel serves `manifest.json` over HTTPS. The manifest specifies the exact ZIP URL, byte
size, SHA-256, entry executable, version, channel, and optional signature metadata. The package
is rejected when its hash or size differs, when the version is not newer, when the manifest or ZIP
contains unsafe paths, or when the first replacement launch fails its health acknowledgement.

`update-feeds.json` in a packaged client selects the Release and Development manifest URLs. The
user's selected channel is stored install-wide under `%LOCALAPPDATA%\PeerChat\Updater`, never in a
local profile.

## Development publication

Use the shared named-parameter publisher. It creates and validates a fresh Client-only ZIP and
candidate manifest before remote publication:

```powershell
.\scripts\publish-update.ps1 -Channel Development -Version 4.0.1-dev.1 -DryRun
.\scripts\publish-update.ps1 -Channel Development -Version 4.0.1-dev.1 -UpdateRepositoryPath "C:\path\to\peerchat-updates" -Publish
```

The explicit update-repository path must be a clean checkout of `Lygophobia1/peerchat-updates`.
The script never clones or guesses it. It uses the operator's authenticated `gh` session, uploads
the verified GitHub prerelease asset first, verifies it remotely, then commits/pushes only
`development/manifest.json`. Keep `DevelopmentAllowsUnsignedPackages` false unless the package is
an explicitly authorized development-only unsigned test. HTTPS, package size, and SHA-256 still
apply.

The default tag is `v<Version>`; use `-Tag <tag>` only when following an established alternate tag
convention. The generated manifest always names the exact selected tag and asset.

## Release promotion

Promote the *same ZIP bytes* only after Development acceptance. Create a Release manifest for the
same package URL/hash/size, set `Channel: release`, and sign it with the configured trusted
release key. The initial key ID is `peerchat-release-2026-01`; the publisher accesses its RSA-3072
private key only from `Cert:\CurrentUser\My`, while `PeerChat.Updater.Core.dll` embeds only the
matching public modulus/exponent. The configured feed is
`https://raw.githubusercontent.com/Lygophobia1/peerchat-updates/main/release/manifest.json`.
The feed file intentionally remains absent until the first real Release asset has been uploaded and
verified.

The signature algorithm is `rsa-sha256-pkcs1-v1_5`. It signs the exact bytes returned by
`UpdateManifest.GetSignaturePayload()`: UTF-8 without a BOM, LF delimiters, a trailing LF, and this
fixed order: domain marker, schema version, product, channel, version, minimum current version,
package URL, lowercase package SHA-256, decimal package size, entry executable, release-notes URL,
and published UTC text. The `Signature` object is not part of the payload. Package hash and size are
therefore authenticated by the signature. Release verification rejects absent signatures,
unrecognized or revoked key IDs, algorithm changes, malformed Base64/public material, invalid
signatures, and package/hash/size mismatches.

Release dry runs and publication require `-ReleaseSigningCertificateThumbprint`. The shared
publisher invokes the reviewed `scripts/sign-release-manifest.ps1` file with explicit arguments
under Windows PowerShell, signs the candidate, and then passes it through the same packaged verifier
before any GitHub action. It does not extract/evaluate script text or use encoded commands.
Publication still requires exact-byte agreement with the accepted Development manifest and
uploads/verifies the GitHub asset before committing `release/manifest.json`.

Back up the private key by exporting a password-protected PFX to two access-controlled offline
encrypted media locations. Use a unique high-entropy passphrase kept separately, never place the PFX
or passphrase in this repository, a GitHub release/feed, a PeerChat package, logs, or ordinary cloud
sync, and test restoration on an isolated Windows account. Losing every copy prevents existing
clients from trusting future releases. For planned rotation, ship the new public key beside the old
one in an old-key-signed release, allow an adoption interval, then switch the publisher key ID and
later mark the old key revoked. If the old private key is compromised, stop publishing with it and
use an independently authenticated installer/channel to establish a replacement trust root.

## Recovery and support

Interrupted downloads remain as `.partial` files for safe resume. The transaction journal and
backup are siblings of the target package directory. Bounded updater logs are written beside that
transaction and exclude client content, account credentials, private keys, and TLS/DTLS secrets.

Do not delete prior release artifacts to make an update. The packaging script creates a new
destination and refuses to overwrite an existing release directory.
