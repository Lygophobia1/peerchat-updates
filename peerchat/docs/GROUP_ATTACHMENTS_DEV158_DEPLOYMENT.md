# dev.158 encrypted-cache deployment and rollback plan

> Subsequently approved and executed on 2026-10-15. See
> [PI_DEV158_DEPLOYMENT_2026_10_15.md](PI_DEV158_DEPLOYMENT_2026_10_15.md)
> for actual health, safety checks and rollback location. The original plan below
> is retained as the pre-deployment record.

PLAN ONLY — no live Pi installation, restart, cache initialization or migration
has been performed. Obtain explicit approval before the controlled installation.

## Read-only readiness checks

Use the existing administrator SSH target and actual service/unit paths; do not
guess credentials, a 32-GB card, a cache mount or the currently running release.
The prior 2026-10-14 read-only probe reported armv7l/32-bit, ext4, 125421432832-byte
filesystem, 112239681536 bytes available and about 3.97 GB RAM. Those are stale
observations, not current readiness. Recheck before approval:

```sh
uname -m
getconf LONG_BIT
free -b
lsblk -b -o NAME,SIZE,FSTYPE,UUID,MOUNTPOINTS
findmnt -T /opt/peerchat-test -o TARGET,SOURCE,FSTYPE,OPTIONS,UUID
df -B1 /opt/peerchat-test
du -sx --block-size=1 /opt/peerchat-test
systemctl list-units --type=service --all 'peerchat*'
```

Then inspect the actual unit's `ExecStart`, executable `--version`, executable
hash and nonsecret configuration fields. Do not paste whole configurations,
environment files, TURN secrets or certificate passwords into diagnostics.
Repeat `findmnt`, `df`, ownership/mode and UUID checks on the proposed cache path.
Verify the appropriate installed libraries and glibc dependencies for the ARM32
self-contained package. A successful Windows cross-build is not Linux execution.

## Backup and installation, only after approval

1. Record running version/hash, exact binary directory, service identity/unit,
   full configuration path and database/cache paths. Identify every replaced file:
   the immutable release payload only, plus an explicitly reviewed cache subsection
   in the configuration. No certificate, key, TURN or unrelated service changes.
2. Choose a timestamped rollback directory on verified available storage. Back up
   the previous entire server release, configuration, unit definition and secret
   references with restrictive permissions. Hash and verify the copies.
3. Quiesce and gracefully stop only PeerChat. After worker/SQLite drain, take a
   consistent backup of the account database and any existing cache database plus
   owned ciphertext. Include WAL/SHM state coherently; never copy only the live
   main database. Check backup integrity in isolation. Preserve device identities,
   mailbox messages/ACKs, grants, certificates, credentials and TURN configuration.
4. Stage the versioned package in a new immutable release directory, verify every
   manifest hash, ARM32 executable architecture and `--version`. Keep prior releases.
   Do not substitute a framework-dependent ARM64 package on a 32-bit installation.
5. Start the new server with cache still disabled first. Verify ordinary auth,
   presence, relay, mailbox and existing clients before enabling the opt-in cache.
6. For first cache initialization, use an explicitly approved empty service-owned
   directory (0700), on the intended mounted filesystem, with safe ancestors and
   no links. Create a fresh 32-lowercase-hex instance marker named
   `.peerchat-attachment-cache-instance`; record it in configuration. Never overwrite
   an existing marker/index or turn an old directory into a new instance.
7. Set `ExpectedFilesystem` to `UUID=<actual UUID>`, `ExpectedMountPoint` to the
   observed mount and `Directory` to its absolute cache directory. This must match
   the runtime storage probe. Missing mounts fail closed; no OS-volume fallback.
8. Keep `MaximumBytes=10737418240`, `RetentionSeconds=172800`,
   `PartialIdleSeconds=21600`, `MinimumFreeBytes=2147483648`,
   `MinimumFreePercent=20`, unless the administrator requests stricter/different
   validated limits. The quota includes ciphertext, partials, metadata, temporary
   overhead and reservations. It is a default, not a 10-GiB ceiling. Do not lower
   the reserve to force admission. Plaintext per-file bound remains 256 MiB.
9. Explicitly set `Enabled=true`, restart PeerChat in the approved maintenance
   window, verify ownership and effective capacity before accepting uploads.
   Protected leases prevent eviction; over-quota admission must reject cleanly.

The source contains no automatic storage migration or destructive cache-enable
action. For SSD relocation, follow the quiesce/consistent-copy/integrity/binding
procedure in `GROUP_ATTACHMENT_CACHE_DESIGN.md`; retain the old copy. No automatic
move, overwrite or mount reconfiguration is authorized by this candidate.

## Smoke tests and recovery

Check version/health, authentication and genuine session invalidation, discovery,
relay attachment, mailbox sender-offline fetch/ACK and existing client compatibility.
Then test one upload with two independent eligible Windows recipients, sender
offline, original-file hash equality, signed plaintext receipts, duplicate/restart
recovery and accurate unavailable/unauthorized responses. Confirm cache accounting,
retention/cleanup and no plaintext/decryption key stored server-side.

If critical health checks fail, stop/drain the candidate. Restore the previous
executable/release selection and compatible configuration with cache disabled.
Keep current databases and all new writes. Do NOT restore an older account/cache
database over newer writes automatically. If the previous binary cannot safely
read current data, keep the service stopped and request a consistency decision;
use the backups for isolated analysis rather than blind rollback. Verify recovery
of auth, relay and mailbox, retain rollback backups, and record the outcome.

No coturn restart/reconfiguration, firewall exposure, OS upgrade, cryptographic
replacement or destructive migration is part of this plan.
