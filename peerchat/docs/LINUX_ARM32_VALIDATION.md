# Linux ARM32 / Raspberry Pi validation

## Accepted automated/runtime candidate

Version: 4.0.2-dev.33.
Source checkpoint: 7cc8f159c44b23c3b1aa4ab825c1c3a01fed6d0a.
Parent: bc6bb35 (dev.32). Earlier accepted packages/checkpoints were preserved.
Validation date: 2026-10-07. This is development-only, not a public deployment.

Pi: Raspberry Pi 4 Model B Rev 1.5, BCM2711, four Cortex-A72 cores,
reported maximum 1800 MHz, 3.7 GiB available physical RAM.
OS: Raspbian GNU/Linux 12 bookworm; kernel 6.12.62+rpt-rpi-v7l;
uname armv7l, LONG_BIT=32. Hardware is ARM64-capable; installed userspace is not.
OpenSSL 3.0.18; glibc 2.36; system ARM hard-float SQLite, ICU 72, libstdc++,
libgcc and zlib were already present. No system .NET runtime was installed.

## Architecture audit and build

The production server sources are unchanged from dev.32, except version metadata.
No Pi-specific server, protocol, mailbox, presence or session implementation exists.
SQLite pointers use IntPtr, SQLite int64 values use long, and portable text uses
explicit UTF-8. Existing CNG public-blob/P1363 parsing and platform ECDsa remain
authoritative. TLS, RSA and crypto native dependencies come from the pinned .NET
runtime plus existing OS libraries, not Windows CNG DLLs.

Build from the isolated Windows source worktree with SDK 10.0.401:

    pwsh -NoProfile -File scripts/build-linux-server.ps1

The script refuses an existing output version. It retains linux-x64/linux-arm64
framework-dependent targets and adds self-contained linux-arm using runtime
10.0.12. The ARM apphost and native libraries are checked as little-endian ELF32
ARM, and the managed application is checked as ArmThumb2 with the same VersionInfo.
The package allowlist uses the pinned runtime pack, notices, and application files.
ARM32 NuGet runtime package SHA-512 (base64):

    2loLULCR0o9BHT6z48Egin7qnsFf0DC0VTVq37/twAsQeKe7oFfa3BvtzFpRz8jq3mdPp2xPbtzWnlzzRs7E2g==

On the Pi, file/readelf confirmed ELF32 ARM EABI5 hard-float, with interpreter
/lib/ld-linux-armhf.so.3. ldd found no missing dependencies across packaged .so
files. Version execution returned 4.0.2-dev.33. No runtime/system packages,
sudo, boot changes, reimage, user/group or firewall changes were required.

Runtime is self-contained, NOT a static executable: distro libc/OpenSSL/ICU and
libsqlite3.so.0 are still required. These dependencies were present on this Pi.

## Artifacts

All paths below are relative to the isolated peerchat source root.

- build/linux/4.0.2-dev.33/PeerChat.Server-4.0.2-dev.33-linux-arm.zip
  34,285,599 bytes; SHA-256:
  b62a30785590cf79f130900dfe19bdf423723fd1eedbe03e005285135c946846
- build/linux/4.0.2-dev.33/PeerChat.Server-4.0.2-dev.33-linux-arm64.zip
  SHA-256: 36575f3d5c4c184b235a46bec58d2f514da349c988ba68334f284f26a4179af8
- build/linux/4.0.2-dev.33/PeerChat.Server-4.0.2-dev.33-linux-x64.zip
  SHA-256: 41e39547a03a2b48acef74774bd56428de58fa00009aa748f4e32ade24fe8f61
- build/development/4.0.2-dev.33/DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.33.zip
  SHA-256: 5a756d5b79834b6a7321dd62a7ed9e916fd1bf430a1397526cb21660afd67d00
- build/android/4.0.2-dev.33/PeerChat-4.0.2-dev.33-debug.apk
  SHA-256: 0a97c5c35a6bfab2ec498947b0426cf64bf08046717be79354f23d5cdf72068a

RID manifests contain per-file hashes. Their LinuxExecution=pending describes
build-time state; this document records subsequent ARM32 execution evidence.
Linux x64 and ARM64 remain cross-build/package validated, NOT runtime accepted.

## Isolated deployment and safe commands

SSH peerchatwork@192.168.1.6, hostname ENDER3PROPI, using the existing dedicated
Windows key C:\Users\Livel\.ssh\peerchatwork_pi. Host-key verification stayed enabled.
All PeerChat writes are under /opt/peerchat-test. No production DB was copied.

- Executable: /opt/peerchat-test/server/4.0.2-dev.33/PeerChat.Server
- Config: /opt/peerchat-test/config/4.0.2-dev.33/server-config.json
- Physical-test DB: /opt/peerchat-test/data/4.0.2-dev.33/physical-client.db
- Logs: /opt/peerchat-test/logs/4.0.2-dev.33/
- Port: 192.168.1.6:17443, TLS; listener binds 0.0.0.0:17443.
- Current manual test-server PID at handoff: 10762 (not a permanent service).
- Epoch at handoff: 3b01ccdd17a043fd9e1a791a5efd5ac8.
- Certificate SHA-256 pin:
  62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3

This is a new disposable development certificate valid for fourteen days, not a
copy/rotation/export of any real device identity. PFX and password files are
restricted to mode 0600; config/data version directories are mode 0700.
A disposable pi-validation-status account was used only to read the current epoch.
It is not a client identity or an authorized contact of real users.

The server is already running. Do not start another copy on its occupied port.
To stop/start the tracked isolated manual process safely over SSH:

    python3 /opt/peerchat-test/server/pi_runtime_validation.py stop --version 4.0.2-dev.33
    python3 /opt/peerchat-test/server/pi_runtime_validation.py start --version 4.0.2-dev.33

The helper checks PID executable/config ownership before signaling. Equivalent
foreground launch, after stopping the existing instance:

    umask 077
    export HOME=/opt/peerchat-test/data/4.0.2-dev.33
    export TMPDIR=/opt/peerchat-test/data/4.0.2-dev.33/tmp
    export DOTNET_EnableDiagnostics=0
    /opt/peerchat-test/server/4.0.2-dev.33/PeerChat.Server --config /opt/peerchat-test/config/4.0.2-dev.33/server-config.json

Foreground Ctrl-C uses SIGINT. The helper uses SIGTERM with no forced-kill fallback.
Manual foreground launch does not create the helper PID record.

## Runtime/parity results

PASS on the actual ARM32 Pi:

- Startup, explicit config, restricted PFX/password loading, TLS, native SQLite,
  Unicode, WAL, synchronous FULL, busy writer, corrupt DB fail-closed.
- Fifty DB open/reopen cycles and five in-process active TLS/partial-read
  shutdown/drain/restart cycles.
- Actual executable SIGINT and SIGTERM with active event/TLS/partial reads:
  zero exit, bounded worker drain, checked SQLite close, committed account
  persistence, integrity_check=ok, and three distinct restart epochs.
- Windows-produced canonical identity/P1363/group/mailbox fixtures verify.
  Linux-produced signatures also verify with unchanged Windows CNG verification;
  modified signatures fail.
- 1,000 presence lifetimes, monotonic expiry, multi-device aggregation, stale
  owner/generation rejection, logout, revocation and restart.
- Signed group roster/database rules, three-client encrypted/opaque fanout and
  room authorization/reconnect tests. No physical group-media claim.
- Production mailbox pinned TLS: sender logout/offline/unreachable, later
  recipient fetch/signed ACK, ciphertext cleanup and sender receipt reconciliation.
  Duplicate/wrong-device/pin rejection, quotas, TTL, rotation/revocation/device
  removal, tamper/truncation and DB/receipt restart persistence.
- Existing discovery suite executed on Pi against the separately launched real
  server: register/login/device ownership, friendship/blocks, relay rendezvous,
  initial presence, heartbeat/silent expiry/reconnect, sequential RPC reuse/count
  bounds, idle/partial frames, device-switch rejection and temporary TURN HMAC.
- FCM provider mocked OAuth/RSA/signing/cache/error tests on Linux. No real FCM
  delivery or Android background wake is claimed.

Windows full dev.33 build/package/import checks and full Framework discovery
(including admin/restart) passed. Modern Windows portable parity passed.
Android 87 JVM tests, debug/test/release builds, lint and ARM64/JNI/TLS/HPKE audits
passed; no physical phone changes/tests occurred. Version/updater safe extraction,
hash and ordering audit passed under Windows PowerShell. git diff --check passed.

Evidence: Pi logs/parity33d.log and logs/network33.log. Windows host evidence:
build/tests/server-portability-8fa24373f9374d08b652a08954b284c2.
Returned Linux-to-Windows fixtures: build/tests/pi-arm33-return.

## Reproducing tests

Use fresh directories: database fixture seeding intentionally rejects duplicates.
Generate fresh Windows fixtures with tests/test-server-portability.ps1.
Publish the test project self-contained for linux-arm (not production packaging):

    dotnet publish tests/portable/PeerChat.Server.PortabilityTests.csproj -c Release -r linux-arm --self-contained true -o <new-test-output>

Transfer its output separately under /opt/peerchat-test/server, preserving executable
permission on the test apphost. Transfer framework.json and root *.bin fixtures;
copy mailbox *.bin RECURSIVELY, preserving quota and other subdirectories.
Do not transfer fixture databases or real profiles/identities/secrets.
Run the harness with fresh absolute root and mailbox-root arguments.
Return modern.json, signature-input.bin, portable-identity.bin and
portable-signature.bin; framework-fixtures.exe <returned-directory> --verify
checks Linux output through Windows CNG.

Mailbox fixture envelopes expire after ten minutes. Generate/copy/run promptly;
never alter expiry validation just to reuse old fixtures.

The --discovery harness entry delegates to the existing discovery assertions.
--remote-no-admin explicitly omits only the Windows-local admin pipe section;
the full Windows suite continues testing it. Use the isolated test TURN config
and test-only secret, never the real coturn secret. The final physical-test config
has TURN disabled, unlike the dedicated issuer regression.

## Measurements, failures and limits

Self-contained deployment: 72 MiB unpacked / 34,285,599-byte ZIP.
Standalone idle sample: RSS 63,004 KiB, VSZ 277,112 KiB, 9 threads, 88 FDs;
0.00% of one core over a ten-second sample. This is not a long-term load claim.
Fresh physical-test DB at observation: 4 KiB main, 347 KiB WAL, 32 KiB SHM.
No connected client sockets at idle; one TCP listening socket on 17443.

Bounded in-process parity workload (server + synthetic clients in one process):
48.07 s wall, 37.67 s user CPU, 2.16 s system CPU; sampled peak RSS 84,896 KiB,
20 threads, 106 FDs. This is not standalone server-only peak memory.

Investigated non-production failures:
- Missing nested quota fixtures in the first transferred bundle: fixed transfer.
- Expired ten-minute mailbox fixtures after an approval usage-limit interruption:
  fresh fixtures passed; expiry enforcement unchanged.
- Cross-PC TURN test compared independent clocks with ten-second tolerance;
  Pi was about 29 seconds ahead. Unchanged TTL assertions passed on the Pi.
  No clocks/system time configuration were changed.
- Windows PowerShell 7 cannot host the Framework JavaScriptSerializer in this
  updater audit. Windows PowerShell 5.1 passed. The first DevelopmentUpdater
  folder is preserved but was not the validated audit; use DevelopmentUpdaterValidated.
- /usr/bin/time was absent; Python/proc/resource observations replaced it.
  No monitoring package was installed.

Coturn remains active, same observed PID 2718, UDP 192.168.1.6:3478.
No coturn files, secrets, ports or service state were modified.

## User physical acceptance and remaining gates

Use isolated development client profiles/accounts, not automatic profile edits.
Verify the exact certificate pin through normal PeerChat setup. Test three accounts:
server-authoritative Online with idle peer routes, direct/group messaging, clean
logout, killed-client expiry, reconnect and sender-independent Stored delivery.
Repeat Windows/Android physical foreground/background/locked scenarios as applicable.

Final Pi config intentionally has TURN and FCM disabled. Real media TURN issuance
requires a separately authorized matching coturn secret/config; opaque Android FCM
wake requires safe external Firebase credentials. Neither live capability is
claimed from mocked provider/test issuer checks. No media transport was changed.

True Linux ARM64 and x64 execution still need their own suitable hosts. ARM32
execution is NOT ARM64 acceptance. Do not reimage this useful 32-bit Pi.

No systemd unit was installed. Existing generic template uses /usr/bin/dotnet for
framework-dependent deployment; a future ARM32 self-contained installation should
use its apphost ExecStart instead. Service user/paths/permissions, backup and
restricted external secret setup require explicit privileged-install approval.
Keep coturn independent. Retain WAL/FULL, SQLite-aware backups and safe power;
prefer SSD/high-endurance storage. See RASPBERRY_PI_DEPLOYMENT.md.

No merge, publication, authoritative-main changes, real-key changes or user-data
deletion occurred. Three pre-existing untracked screen object files remain excluded.

Official platform references:
- https://github.com/dotnet/core/blob/main/release-notes/10.0/supported-os.md
- https://learn.microsoft.com/en-us/dotnet/core/deploying/
