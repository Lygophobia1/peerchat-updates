# PeerChat updater operator guide

This is the practical guide for publishing and testing PeerChat updates. For the implementation
contract and security boundaries, see [README.md](README.md). This guide uses the actual current
worktree and candidate paths; do not substitute a different folder without deliberately updating
the relevant command and manifest values.

## 1. How to use the updater

In plain terms, an update works like this:

```text
Build a new PeerChat version
  → package only its Client runtime files
  → publish a ZIP plus matching manifest to Development or Release
  → client checks its selected channel
  → client downloads and verifies the ZIP
  → choose Download and restart
  → PeerChat exits
  → PeerChat.Updater.exe replaces the Client files
  → updated PeerChat starts and confirms it opened its native profile/core
  → update is kept, or the old Client files are restored
```

The update process does not move or replace local profiles, encrypted history, device identity,
trust, account sessions, or P2P data.

## 2. Exact current directories and files

| Purpose | Current path |
|---|---|
| Repository/worktree root | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat` |
| Updater source | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\updater` |
| Updater build executable | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\build\v0.3-beta\Updater\PeerChat.Updater.exe` |
| Updater build core | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\build\v0.3-beta\Updater\PeerChat.Updater.Core.dll` |
| Manifest example | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\updater\manifest.example.json` |
| Technical updater reference | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\updater\README.md` |
| This guide | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\updater\UPDATER_USER_GUIDE.md` |
| Current updater candidate package root | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final` |
| Current candidate Client folder | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final\Client` |
| Candidate desktop executable | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final\Client\PeerChat.Desktop.exe` |
| Candidate updater executable | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final\Client\PeerChat.Updater.exe` |
| Candidate package hashes | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final\PACKAGE-SHA256.txt` |
| Candidate feed map | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final\Client\update-feeds.json` |
| Packaging script | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\scripts\package-v0.3-beta.ps1` |
| Updater build script | `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\scripts\build-updater.ps1` |

The packaged feed map uses the real GitHub raw paths under `Lygophobia1/peerchat-updates`.
Development currently has a published manifest. The configured Release URL is
`https://raw.githubusercontent.com/Lygophobia1/peerchat-updates/main/release/manifest.json`, but that
file intentionally remains absent until the first signed Release is safely published.

The current installed-version source is:

`C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\shared\VersionInfo.cs`

It now declares `4.0.0-dev.1` for the first v4 Development payload. The prior accepted updater
candidate declares `0.3.0-beta`, which is the upgrade floor used by the v4 Development manifest.

### Runtime-generated updater paths

The selected channel is installation-wide and stored at:

`%LOCALAPPDATA%\PeerChat\Updater\channel.json`

Downloads, resumable `.partial` files, extracted payloads, and copied standalone updater launchers
are under:

`%LOCALAPPDATA%\PeerChat\Updates\`

For example, a v4 payload is staged below:

`%LOCALAPPDATA%\PeerChat\Updates\4.0.0-development.1\`

The copied launcher is a generated path similar to:

`%LOCALAPPDATA%\PeerChat\Updates\launcher-<GUID>\PeerChat.Updater.exe`

For an installed target `...\SomePackage\Client`, the updater creates these generated siblings in
`...\SomePackage`:

```text
.peerchat-update-<GUID>.json       transaction journal
.peerchat-backup-<GUID>            old Client directory during replacement
.peerchat-new-<GUID>               copied replacement before activation
.peerchat-health-<GUID>            first-launch health marker
PeerChat.Updater.log               bounded updater log
```

For the current candidate, those sibling paths would be under:

`C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v0.3-beta-updater-candidate-final\`

They exist only while an update transaction is active or retained for diagnosis.

## 3. Development versus Release

### Development

Development is for an intentionally selected, pre-release update feed. On a test client, open
**Settings → Updates**, select **Development**, then click **Check for Updates**.

The client reads the `DevelopmentManifestUrl` in its adjacent `update-feeds.json`. The checked-in
and current packaged file has only placeholders, so this is:

**BLOCKED UNTIL FEED URL IS PROVIDED.** Set it to a real HTTPS URL such as
`<DEVELOPMENT_FEED_HTTPS_URL>/manifest.json` in the installed update-capable candidate before
testing. The same configured candidate must already be on both test PCs; the v4 ZIP itself is then
downloaded from the feed, not copied manually between PCs.

The current Development test package explicitly enables `DevelopmentAllowsUnsignedPackages`.
That opt-in applies only when Development is selected and does not turn off HTTPS, size, or SHA-256
verification. It can never make an unsigned Release acceptable.

### Release

Release is the default normal channel and uses `rsa-sha256-pkcs1-v1_5` with initial key ID
`peerchat-release-2026-01`. The RSA-3072 private key is held only in the release operator's
`Cert:\CurrentUser\My` store under certificate thumbprint
`B8A353A2C410A04987B6AF87D2D493819EC29A86`. The updater embeds only its public modulus/exponent. Unsigned,
unknown-key, revoked-key, malformed, invalid, and package-mismatched Release manifests fail closed.
The Release feed becomes live only when the shared publisher uploads and verifies the first normal
GitHub Release asset and then commits `release/manifest.json` last.

Release clients must never point at or receive Development-only manifests. A manifest's `Channel`
must exactly match the selected client channel.

## 4. Literal checklist: first v4 Development delivery test

The v4 ZIP and matching manifest are already staged under `releases\v4-dev.1\DevelopmentFeed`.
This checklist assumes two PCs already run the update-capable current candidate and a real HTTPS
Development feed is available.

1. **BLOCKED UNTIL FEED URL IS PROVIDED:** choose and configure
   `<DEVELOPMENT_FEED_HTTPS_URL>` and `<DEVELOPMENT_PACKAGE_HTTPS_URL>`.
2. In `shared\VersionInfo.cs`, set the intended v4 semantic display value, for example
   `4.0.0-development.1`. Keep the manifest version identical.
3. From the repository root, run:

   ```powershell
   .\scripts\package-v0.3-beta.ps1 -ReleaseDirectory releases\v4-development
   ```

   The legacy script name is factual; the explicit output must appear at:

   `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v4-development\Client`

4. Verify the resulting `PACKAGE-SHA256.txt` at:

   `C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\releases\v4-development\PACKAGE-SHA256.txt`

5. Create a ZIP whose root contains the **contents** of `Client`, including `PeerChat.Desktop.exe`,
   `PeerChat.Core.dll`, `PeerChat.Updater.exe`, `PeerChat.Updater.Core.dll`, configs, icon,
   `service-bootstrap.json`, and `update-feeds.json`. Do not ZIP the outer `Client` folder itself.
6. Calculate the ZIP byte size and SHA-256. Copy those exact values into a Development manifest.
7. Set manifest fields: `SchemaVersion: 1`, `Product: PeerChat`, `Channel: development`,
   `Version: 4.0.0-development.1`, `MinimumCurrentVersion: 0.3.0-beta`, root entry executable
   `PeerChat.Desktop.exe`, exact HTTPS ZIP URL, exact SHA-256, byte size, and publication UTC.
8. Upload the ZIP to `<DEVELOPMENT_PACKAGE_HTTPS_URL>` and the manifest to
   `<DEVELOPMENT_FEED_HTTPS_URL>/manifest.json`. Verify both URLs from a browser or HTTPS client.
9. On test PC 1, open **Settings → Updates**, choose **Development**, and click **Check for Updates**.
   Expect `Version 4.0.0-development.1 is ready to download and verify.`
10. Click **Download and restart** in the confirmation. The current UI has no separate Download and
    Restart buttons; that one confirmation performs download, verification, preparation, updater
    launch, and graceful client close.
11. `PeerChat.Updater.exe` waits for the original desktop process, replaces its `Client` directory,
    launches the new `PeerChat.Desktop.exe`, and waits up to the configured first-launch timeout.
12. The new desktop must open its selected local profile and native core. Only then does it write the
    health marker passed as `--peerchat-update-health-token`.
13. Confirm the version, selected profile, local history, trust, text messaging, and normal startup.
    Inspect `PeerChat.Updater.log` and the transaction journal if the updater reports a failure.
14. Repeat steps 9–13 on test PC 2. Do not copy the v4 ZIP from PC 1; both PCs must download the
    same published package and verify its SHA-256.

## 5. Publishing a Development v4 build

### Preferred automation

`scripts\publish-update.ps1` is the one shared publisher for Development and future Release
promotion. It takes named parameters rather than menu choices, uses the existing clean package
pipeline, creates a ZIP from the Client runtime files only, verifies its contents/imports/hash/size,
and writes a candidate manifest locally before any remote operation.

Before running it, set `shared\VersionInfo.cs` to the exact requested semantic version. For real
publication, supply an explicit local checkout of `Lygophobia1/peerchat-updates`; the script never
guesses a location or clones a repository. The checkout must be clean, have the expected `origin`,
and contain the channel directory. `gh auth status` must succeed using the operator's existing
GitHub session—no credential is stored by PeerChat.

Development dry run (builds and validates locally; no GitHub or Git changes):

```powershell
.\scripts\publish-update.ps1 -Channel Development -Version 4.0.1-dev.1 -DryRun
```

Development publish (the explicit `-Publish` and repository path are both required):

```powershell
.\scripts\publish-update.ps1 `
  -Channel Development `
  -Version 4.0.1-dev.1 `
  -UpdateRepositoryPath "C:\path\to\peerchat-updates" `
  -Publish
```

The default GitHub tag is `v<Version>` and the asset is `PeerChat-<Version>.zip`. If an established
release convention needs a different safe tag, pass it explicitly with `-Tag <tag>`; the generated
asset and manifest URL are then verified against that exact tag.

The remote sequence is intentionally ordered: create/reuse prerelease tag, upload the ZIP,
verify the remote asset URL/name/size, then replace only `development/manifest.json`, commit it,
and push it. The manifest is never moved first. The script prints the exact ZIP, tag, asset URL,
manifest URL, byte size, SHA-256, release URL, and completed stages.

If publication stops after upload but before manifest push, clients remain on the prior manifest.
Keep the generated ZIP and candidate manifest, inspect the named GitHub release and local update
checkout, fix the cause, and rerun the same explicit command. Do not delete releases, artifacts,
transaction evidence, or profiles as recovery.

Use this manifest shape, replacing every angle-bracket value before publication:

```json
{
  "SchemaVersion": 1,
  "Product": "PeerChat",
  "Channel": "development",
  "Version": "4.0.0-development.1",
  "MinimumCurrentVersion": "0.3.0-beta",
  "PackageUrl": "<DEVELOPMENT_PACKAGE_HTTPS_URL>/PeerChat-4.0.0-development.1.zip",
  "PackageSha256": "<EXACT_64_CHARACTER_SHA256>",
  "PackageSize": <EXACT_ZIP_BYTE_SIZE>,
  "EntryExecutable": "PeerChat.Desktop.exe",
  "ReleaseNotesUrl": "<DEVELOPMENT_FEED_HTTPS_URL>/4.0.0-development.1",
  "PublishedUtc": "<UTC_ISO_8601_TIMESTAMP>"
}
```

Build first, create one Client ZIP, calculate its hash and size, upload the ZIP, then publish the
manifest last. Publishing the manifest last avoids advertising a package that is not yet available.
The installed candidates must have a matching real `DevelopmentManifestUrl` before they can find it.

## 6. Promoting Development to Release

The same publisher reserves Release for exact-byte promotion rather than rebuilding:

```powershell
.\scripts\publish-update.ps1 `
  -Channel Release `
  -Version 4.0.0 `
  -PromotePackagePath "C:\path\to\accepted-development.zip" `
  -TestedDevelopmentManifestPath "C:\path\to\accepted-development-manifest.json" `
  -ReleaseSigningCertificateThumbprint "<CURRENT-USER-CERTIFICATE-THUMBPRINT>" `
  -UpdateRepositoryPath "C:\path\to\peerchat-updates" `
  -DryRun
```

The dry run compares the selected ZIP's hash and size to the accepted Development manifest, signs
the candidate manifest with the Current User certificate-store key, and verifies it through the
same trusted updater verifier. The publisher runs the reviewed `scripts\sign-release-manifest.ps1`
from disk with explicit arguments under Windows PowerShell; it does not dynamically extract or
evaluate PowerShell functions. It performs no GitHub, manifest-repository, commit, or push action.

Never rebuild a tested package for promotion. Use this sequence:

```text
exact accepted v4 Development ZIP bytes
  → recompute and compare SHA-256
  → produce a signed Release manifest for those same bytes
  → publish the Release manifest to the Release HTTPS feed
  → Release clients discover the matching Release manifest
```

The ZIP URL may be reused, but `Channel` changes to `release` and the Release manifest must carry
the configured valid signature/key identifier. Package URL, SHA-256, size, entry executable, and
semantic version must remain identical to the tested package. The files that change during
promotion are the Release manifest and Release feed publication; the tested ZIP must not change.

Do not publish until the exact promoted bytes and stable version have completed physical acceptance.
Never publish an unsigned Release manifest or turn off Release signature requirements.

### Signing-key custody and rotation

The signing certificate is selected explicitly by thumbprint; the script does not search for or
guess a private key. Export a password-protected PFX to two access-controlled offline encrypted
media locations, keep its unique high-entropy passphrase separately, and test restoration on an
isolated Windows account. Never commit, upload, package, log, or ordinarily cloud-sync the PFX or
passphrase.

For planned rotation, add the new public key and key ID to the updater while the old key is still
trusted, distribute that client in a release signed by the old key, wait for adoption, then sign with
the new key. Retire/revoke the old key in a later signed client. If the old private key is compromised,
do not use it to claim a trustworthy rotation; distribute a new trust root through an independently
authenticated installer/channel.

## 7. Rollback and deliberate rollback test

The updater copies itself outside `Client` because Windows may lock a running executable. PeerChat
then exits so its own files are no longer loaded. The updater copies the staged payload to
`.peerchat-new-<GUID>`, renames the current `Client` to `.peerchat-backup-<GUID>`, activates the
replacement, and records each phase in `.peerchat-update-<GUID>.json`.

Windows can briefly retain a file handle immediately after PeerChat exits. Before that old-Client
rename, the updater verifies the original process has exited and retries only a transient sharing
or lock failure on a short bounded delay. It logs every retry; it never retries validation, hash
checks, extraction, health confirmation, or rollback indefinitely.

The updater launches the new desktop with a unique health-marker path. The desktop writes that
marker only after native profile/core startup. If the process exits, crashes, or does not report
healthy before the configured timeout, the updater moves the failed Client aside, restores the
backup, and restarts the old executable.

To prove rollback safely, use a disposable copy of the current candidate package in a new folder.
Publish a separately versioned Development ZIP that passes manifest/hash checks but is deliberately
made unable to report healthy. Run the update, verify the old Client returns, then inspect the
package-root `PeerChat.Updater.log`, `.peerchat-update-<GUID>.json`, backup marker, and failed
replacement directory. Do not test rollback against the only retained known-good package and do
not delete profiles or `%LOCALAPPDATA%\PeerChat` data.

## 8. Internal flow and trust boundary

`PeerChat.Desktop.exe` owns the Settings UI and `DesktopUpdateClient`. It reads the installation-wide
channel preference, loads the adjacent feed map, validates the chosen feed, requests a manifest,
downloads/resumes the ZIP, validates size/hash/signature policy, extracts it safely, copies the
standalone updater outside the target install directory, and launches it only after preparation.

`PeerChat.Updater.Core.dll` contains semantic-version comparison, manifest validation, HTTPS
transport, partial-download handling, SHA-256 verification, safe ZIP extraction, update plan/journal,
and rollback logic. `PeerChat.Updater.exe` is the small command-line host that loads a plan, writes
bounded operational logs, and runs the installer.

The manifest is an untrusted network input. It must match product/channel policy, use HTTPS, point
to a safe entry path, and bind the downloaded bytes through size and SHA-256. Release signature
verification is an additional fail-closed trust boundary. The updater does not use discovery,
P2P, voice, groups, or a PeerChat session to trust an update.

## 9. Versioning

`shared\VersionInfo.cs` is the current installed-version source used by desktop and updater plan
creation. The manifest `Version` is the offered package version and must be strictly newer by
semantic-version ordering. `Channel` is a separate exact match: Development and Release are not
interchangeable. For this guide, “v4” means the proposed semantic test value
`4.0.0-development.1`; choose and retain one final semantic scheme before publishing.

### When changing PeerChat to a new version, update these places

1. `shared\VersionInfo.cs` — installed/display version (and release-folder value when the package
   layout changes).
2. The package destination passed to `package-v0.3-beta.ps1` — use a fresh, non-existing directory.
3. The ZIP filename and package hash/size.
4. The published Development or Release manifest `Version`, `MinimumCurrentVersion`, URL, hash,
   size, release-notes URL, and `PublishedUtc`.
5. For Release, valid signature algorithm/key ID/signature once that capability is configured.

## 10. Troubleshooting

| Symptom | Meaning and safe action |
|---|---|
| Check finds nothing | Confirm Development/Release selection, matching manifest `Channel`, newer semantic version, and real `*ManifestUrl` in adjacent `update-feeds.json`. |
| Manifest cannot download / HTTPS failure | Check the configured HTTPS URL and certificate. Inspect the client update status and do not enable insecure HTTP for Release. |
| Same version / downgrade rejected | The updater correctly refuses replacement. Increase the manifest version; never lower it to force an install. |
| Hash or size mismatch | ZIP bytes differ from the manifest. Recalculate from the uploaded ZIP; do not disable verification. Partial file is under `%LOCALAPPDATA%\PeerChat\Updates`. |
| Signature failure | The Release is unsigned, uses an unknown/revoked key or wrong algorithm, has malformed signature data, was altered, or does not match the ZIP. Do not turn off Release signature policy. |
| Development unsigned mode not enabled | `DevelopmentAllowsUnsignedPackages` is false. Obtain explicit authorization, set only that Development property to true, and preserve HTTPS/hash/size checks. |
| Download succeeds but install fails | Inspect package-root `PeerChat.Updater.log` and `.peerchat-update-<GUID>.json`. Ensure PeerChat files are not manually locked or replaced. |
| Health timeout / rollback | New desktop did not confirm native profile/core startup. Inspect `PeerChat.Updater.log`, the journal, failed replacement, and startup logs. Keep the backup intact for diagnosis. |
| Old version reopens | Rollback likely succeeded. Read the journal phase and log; do not overwrite the backup or profile data. |
| Package missing dependency | Compare package contents and `PACKAGE-SHA256.txt`; rerun the clean packager and native dependency audit. |
| PACKAGE-SHA256 mismatch | The staged package is not the expected output. Do not distribute it; rebuild into a new package directory. |

## 11. Safety rules

- Never disable hash verification to force an update.
- Never bypass Release signature verification.
- Never overwrite or delete the retained known-good package while testing.
- Never delete profile, encrypted history, identity, trust, account-session, or runtime data as updater cleanup.
- Do not make the updater depend on PeerChat P2P, discovery, voice, or session transport.
- Do not manually replace Client files while PeerChat is running.
- Do not delete transaction, backup, failed-replacement, or log files before collecting the failure evidence.

## 12. Quick reference

```text
Repo:       C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat
Candidate:  ...\releases\v0.3-beta-updater-candidate-final\Client
Updater:    ...\Client\PeerChat.Updater.exe
Hashes:     ...\releases\v0.3-beta-updater-candidate-final\PACKAGE-SHA256.txt
Reference:  ...\updater\README.md

Development: configure real Development HTTPS manifest URL → select Development in
             Settings → Updates → Check for Updates → Download and restart.
Release:     same tested ZIP bytes → signed Release manifest → Release HTTPS feed.
v4 test:     update VersionInfo + v4 manifest → fresh package/ZIP/hash → publish Development
             feed → update each test PC from that feed.
Rollback:    use a disposable package copy, force missing health confirmation, inspect journal/log,
             confirm old Client is restored.
Logs:        <package parent>\PeerChat.Updater.log
Manifests:   Development is live; Release uses .../main/release/manifest.json after first signed publish.
```
