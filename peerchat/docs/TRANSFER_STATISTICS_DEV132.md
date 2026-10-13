# File statistics and automatic History Sync display, dev.132

Continues dev.131 `d5b350e` and dev.130 `9bae40a` on the existing branch.
No live Pi deployment, phone access, installation, account reset, historical
identity migration or user-profile writes were performed by this validation.

## Statistics architecture

`statistics.hpp::RateMeter` is a passive, bounded in-memory meter. It has no
thread, service, timer owner, socket, persistence or event log. TransferManager
supplies its clock and confirmed-byte high-water mark. Sender bytes come from
authenticated ACKs; receiver bytes advance only after append/flush/hash. Offered
bytes and retransmitted prefixes do not inflate statistics. Rates use recent
approximately one-second windows, expire to zero after a stall, and ETA requires
multiple sufficiently stable samples. ETA is hidden outside active transfer or
when rate/remaining duration is unreliable.

Windows uses its existing one-second attachment refresh. Android caches the
bounded attachment snapshot for one second, keyed by native handle and account
generation; other application updates are not slowed. The existing bubbles show
actual state, bytes/total, percentage, current/average Mbps and optional ETA.
Completed bubbles retain average, duration and size. Duration is end-to-end from
the existing protected creation timestamp, including preparation/wait/recovery;
it is not a fabricated wire-only duration or network bitrate.

`ProtectedStatisticsStore` implements the optional summary adapter. Only verified
Complete records with known measurement data are written. Its versioned protected
object namespace is equivalent isolated supplemental storage inside the existing
generic record store, not a new message-history or transfer-journal schema.
Context includes exact device and stable transfer ID; records bind peer, direction
and size. Existing profile protection encrypts these records. CAS and cached exact
record comparison avoid unchanged writes and duplicate completion records.

Live samples, rates and ETA are never written to SQLite. Missing, foreign,
unsupported or corrupted summaries leave statistics unavailable without changing
core state. Mandatory journal persistence remains authoritative and precedes
optional summaries; optional save/load errors cannot block payload, ACKs, resume
or profile loading. Existing Windows/Android journal codecs and schemas are
unchanged, so older clients retain their conversations, attachments and journal.
Summaries remain supplemental even if no longer present in the bounded current
transfer projection. Legacy completed transfers have no invented average.

Each current transfer is peer-specific. Group attachment fanout is not implemented
in the current supported UI, so no fictitious aggregate/group rate is displayed.
Voice, screen-sharing and network meters are not implemented by this milestone.

## Automatic History Sync setting finding

The confirmed defect was cosmetic loading-state presentation on Android:
`ArchiveDevicesSection` rendered `enrollment?.historySyncEnabled ?: false`, so a
missing verified enrollment snapshot appeared as OFF. It now says Verifying
History Sync setting until the protected native preference has been read, then
renders its actual ON or OFF value. Rendering never writes a preference.

Native `Authorization::historySyncEnabled` and operation 11 already read/write
the same protected preference scoped by account and exact local device. Missing
preference defaults ON; explicit OFF survives reopen. Metadata refresh and
reconnect do not write it. Automatic scheduling checks both that preference and
existing per-device automatic preferences/current grants. An explicit Pause is
still respected. No persistence reset or scheduler setting reset was reproduced;
the UI correction does not turn an explicit OFF into ON or change grants.

## Automated evidence

- `test-transfer-statistics.ps1`: confirmed-byte accounting, stall zero, bounded
  ETA, rewind/retry deduplication, exact binary completion, frozen duration,
  protected summary reopen, foreign/missing/corrupt summary isolation and optional
  provider failure isolation. Disposable SQLite audit triggers prove zero writes
  during live sampling, exactly one completion write and no duplicate/unchanged
  write. These triggers/tables exist only in generated test databases, not any
  production storage initializer or real profile.
- TransferManager: 1/3/10/32 MiB, Unicode/empty/binary, duplicate/reconnect, hash
  tamper, disk failures, capacity, atomic completion, cancellation and durable
  restart regressions pass. The fixture clock is controlled, not a physical LAN
  throughput benchmark; no physical speed improvement is claimed here.
- Shared endpoint: 102 actual mutual Schannel TLS lifecycle tests, bidirectional
  picture/file transfer and text/ACK/deduplication; capacity refusal preserves the
  valid route. No extra worker, polling owner or protocol was added for statistics.
- Archive enrollment/storage/TLS/scope/provenance tests pass. Additional native
  checks cover OFF across metadata refresh, protected OFF/ON reopen, scheduler
  honoring OFF and wrong-account setting writes rejected.
- Packaged Windows Node workers: 25 committed imports, three preserved unresolved
  rows, honest Partial, verified Unicode attachment download and five worker
  restarts. Previous request ownership/deadline corrections remain intact.
- Windows statistics formatting/bubble controls and archive Settings smoke pass.
- Android: 144 JVM/UI tests pass, including unknown/ON/OFF switch snapshots and
  statistics formatting. Debug, instrumented-test and release APK compilation,
  lint, ARM64 dependency/export audits pass. Instrumented physical tests were
  compiled, not run on the phone.
- Windows client/updater/server compile and package native dependency/import,
  HPKE/media ABI/lifetime audits pass. Existing ScreenMedia binary is unchanged.
- `git diff --check` passes. Generated outputs and unrelated object files are not
  checkpointed. No new dependency is required.

## Manual physical handoff

Use matching Windows and Android **4.0.2-dev.132**. Android debug APK SHA-256:
`68088EE0A2785A8A2EBEA56B8FA7620E4A70EDD3E81655FF25C0E5F9D3BDD0A2`.
APK signature is valid and unchanged from dev.130; certificate SHA-256:
`db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.

1. User replacement-installs the versioned debug APK with `adb install -r`.
   Do not uninstall, clear data or replace signing identity.
2. On Windows use the account-4 profile whose reader is `35049f3b…`; start History
   Sync from Android source `61f70fb0…`. Verify eligible imports and restart/repeat
   without duplicates. Expect Partial for unresolved legacy provenance, not full
   success. Repeat the reverse direction; check Android remains signed in.
3. Transfer a multi-megabyte file each way. Check both bubbles update statistics
   about once per second, completed files open correctly, completion summaries
   remain after restart, and locked Android transfers still finish normally.
4. Open History Sync settings, enable if desired, close/reopen and restart. Verify
   the saved ON/OFF choice, with Verifying rather than false OFF while loading.

Physical acceptance remains pending. The four historical references/236 indexed
rows remain protected and unresolved; no ownership was invented, reassigned or
deleted. No server deployment is required by the statistics/UI changes. Stop for
the user's manual results; do not continue the broader roadmap.
