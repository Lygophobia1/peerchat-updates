# Approved Pi dev.158 deployment — 2026-10-15

## Result

**Deployed and cache enabled; ready for physical Windows testing.** The user
explicitly approved the documented deployment. This is server deployment and
cache-safety validation, not physical acceptance of group picture/file delivery.

- Previous running server: `4.0.2-dev.125`, PID 24620.
- Deployed server: `4.0.2-dev.158`, final observed PID 5029.
- Executable: `/opt/peerchat-test/server/4.0.2-dev.158/PeerChat.Server`.
- Configuration: `/opt/peerchat-test/config/4.0.2-dev.158/server-config.json`.
- Existing database retained: `/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db`.
- Existing endpoint retained: `192.168.1.6:7443` / existing public mapping.
- TLS certificate SHA-256 unchanged:
  `62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3`.
- Coturn still PID 2718. No TURN, firewall, OS or systemd changes.

## Readiness and staged integrity

Read-only inspection confirmed ARMv7l / 32-bit, ext4 on `/dev/mmcblk0p2`, mounted
at `/`, UUID `bb15c8e6-d999-4838-be67-5ff200bffa46`. Filesystem size is
125421432832 bytes; initial available space was 112237785088 bytes. Total RAM was
3967205376 bytes, available about 3586240512 bytes. These are observations, not
hardcoded capacity assumptions in the server.

The existing installation is a manually supervised PeerChat process, not a
systemd service. Its prior checked PID/start/stop helper was reused. No service
was invented or installed. The existing SSH key and known-host verification were
used; no authentication fallback or host-key bypass.

Delivered the unchanged ARM32 self-contained package with pinned .NET 10.0.12:
`PeerChat.Server-4.0.2-dev.158-linux-arm.zip`, SHA-256
`cdfd255e3057f27701d50a6f1013acac5122845921e605fb99cb63c657414acc`.
Verified the ZIP, complete manifest allowlist and every runtime file hash on Pi.
Actual ARM32 `--version` and native loader checks passed before any live stop.

Staging/evidence:
`/opt/peerchat-test/staging/dev158-approved-20261015` and
`/opt/peerchat-test/validation/dev158-approved-20261015` (mode 0700).

## Backup and deployment sequence

Restorable rollback directory:

`/opt/peerchat-test/rollback/pre-4.0.2-dev.158-20261015T105314Z`

Contains verified prior runtime and configuration copies, four privately backed-up
referenced certificate/credential files, `online.sqlite`, `stopped.sqlite`,
`pre-cache-stopped.sqlite`, pre-activation dev.158 disabled configuration and
private preservation manifests. All three SQLite backups passed integrity checks.
Credential/certificate copies were hash-verified against the unchanged live files;
no credential contents were printed or put into source control.

1. Verified the old server's pinned TLS, existing dedicated validation-account
   login and authenticated discovery before downtime.
2. Created coherent online backup and verified runtime/config/private copies.
3. Gracefully stopped only the checked old PeerChat PID; took a consistent stopped
   database backup. No forced kill of the live service.
4. Started the manifest-verified dev.158 runtime with byte-identical configuration
   and cache disabled. Pinned TLS, authentication and discovery passed. Original
   identity/device/grant/roster columns and contents passed preservation checks.
5. Created the new empty cache instance with restrictive permissions. Opened it
   with a deployment-only reflection probe against the **exact candidate server
   assembly**. Its own storage binding, quota/reserve checks and durable schema
   passed; this did not modify user account/session/grant records.
6. A deliberately wrong filesystem binding was rejected before any cache-index
   bytes changed. Kept the negative-test evidence in the private validation area.
7. Backed up the disabled configuration; gracefully stopped dev.158 and backed up
   its current database again. Atomically installed only the reviewed cache
   subsection and restarted through the checked helper.
8. Final TLS/auth/discovery, identity/grant preservation, unchanged noncache
   configuration, process/listener, cache permissions and database integrity checks
   passed. No rollback was required; no old database was restored.

## Active cache configuration and safety

- Directory: `/opt/peerchat-test/cache/group-attachments`, parent/directory 0700.
- Instance marker and SQLite index: 0600, owned by `peerchatwork`.
- Bound to actual filesystem UUID above and actual mount `/`.
- Requested maximum: **10737418240 bytes (10 GiB)**.
- Retention: **172800 seconds (48 hours)** after completion.
- Partial idle bound: 21600 seconds (6 hours).
- Reserve: greater of 2147483648 bytes or 20% of filesystem capacity; current
  calculated floor **25084286567 bytes**.
- Available after deployment: approximately **111788118016 bytes**.
- Fresh cache index: `integrity_check=ok`, zero cached objects at validation.
- Initial charged budget, including metadata/operation headroom: 4321280 bytes.

No storage relocation, plaintext attachment, decryption key or imported user file
was added. The ordinary 256-MiB file limit and 32-KiB × 1 default remain unchanged.
Ordinary shipping builds stay feature-gated; this exact tested candidate is an
explicit administrator opt-in. No test fault-injection code entered the server.

## Actual Pi validation

Before the update, the existing safety suite was cross-built for ARM32 and ran
under the real Pi runtime against isolated storage on the same filesystem.
Temporary network fixtures were explicitly bound to **127.0.0.1**, not public
interfaces. No user account, profile, identity, key or signed grant was used by
these fixtures.

Passed: durable ciphertext sizes through 10 MiB, exact confirmed offsets and
duplicate rejection; two independent recipient offsets; incomplete/corrupt
rejection; quota pressure and oldest-eligible eviction; protected leases and
expiry; 16 concurrent reservations; wrong account/device/lease; original/current
membership and revocation/rejoin; durable envelope/ACK recovery; schema/instance
ownership; reconstructed reservation ledger; completion-lock scheduling and
revocation/session/lease rechecks; actual process-kill recovery during upload and
cleanup. The only forced kills were disposable crash-test children.

Isolated pinned-TLS cache RPC tests also passed disabled/enabled behavior,
device-bound-session enforcement, source logout, recipient envelope ACK and full
durable ciphertext download over reconnects. These tests do **not** claim native
plaintext decryption or physical Windows group attachment acceptance; those native
paths passed the prior Windows production-path tests and now require user testing
against this Pi.

Live post-update health passed pinned TLS, authentication, authenticated discovery,
database integrity and stable listener/process. Existing clients reconnected and
ordinary relay ownership events were observed. No live user mailbox was consumed
or acknowledged for a smoke test. Prior isolated messaging/relay/mailbox
regressions are recorded in the dev.158 validation report; live end-user delivery
remains part of physical acceptance.

Local retained evidence:
`build/deployment/dev158-pi-20261015/` (deployment/cache result JSON and safety/TLS
test logs). No secrets or production database backups were downloaded.

## Rollback if required

Retain the rollback directory and both versioned runtimes/configurations. Stop the
checked dev.158 process using the existing helper:

```sh
python3 /opt/peerchat-test/server/pi_runtime_validation.py stop --version 4.0.2-dev.158 --port 7443
python3 /opt/peerchat-test/server/pi_runtime_validation.py start --version 4.0.2-dev.125 --port 7443
```

The old configuration remains unchanged/cache-disabled and uses the **current**
database, not an older snapshot. Recheck pinned TLS, authentication/discovery,
mailbox and relay behavior. Keep the new cache files untouched. If a newly detected
schema incompatibility prevents safe binary rollback, stop and request a
consistency decision rather than restoring older data over new writes.

To retain dev.158 but disable cache, first stop it and restore the verified
`dev158-cache-disabled-config.json` from rollback to its exact config path, then
restart and verify health. Never restore `online.sqlite`, `stopped.sqlite` or the
cache index over newer writes automatically.

## Physical test handoff — stop here

Run the already supplied dev.158 Windows package on sender and recipient PCs with
their existing profiles. In an existing authorized group:

1. Select a picture and file; verify draft/name and explicit Send.
2. Download on two recipients, one initially offline; compare saved file hashes.
3. Check separate upload versus recipient-verification status, Open/Save,
   restart/resume, cancellation and older attachment history.

Physical group picture/file acceptance is **PENDING**. No further development,
deployment, Android installation or roadmap task follows this handoff.
