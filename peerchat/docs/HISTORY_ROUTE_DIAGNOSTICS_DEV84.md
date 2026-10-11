# History route diagnostics — dev.84

Physical dev.83 reports (2026-10-11 20:03–20:04 UTC) show fresh metadata and
valid same-authorizer source/reader grants on both PC and VM. Their Connection
sections describe JonahTest4, not the same-account archive scope, and historical
DM connection events do not identify the current history stall.

Read-only PC storage inspection shows a v2 completed intent using the trusted
VM authorizer. This proves the old disabled-issuer restore was replaced and a
scan completed, but does not establish how many new messages were imported.
The PC UI log also shows incoming archive admission rejected. Existing logs
do not identify its issuer/generation, so incompatible incoming authorizers are
a hypothesis, not yet a proven root cause of the remaining physical stall.

Dev.84 adds a read-only execution section to the existing bounded authorization
snapshot: durable intent issuer/source/disposition, active scope and grant IDs/
generations, exact-current admission, real message/conversation counters, pending
page, SessionTransportManager winner/pending/retry/network state, latest failed
attempt and bounded close reason, plus the latest incoming scope and exact grant
admission reasons. The report stays at most 49,152 bytes. No credentials, keys,
message bodies or attachment contents are added. No admission, trust, routing,
retry or transfer policy is changed by this checkpoint.

Physical next gate: start dev.84 on both existing PC/VM profiles, leave existing
permissions unchanged, and capture both full diagnostics while History Sync
shows Connecting. Compare active/incoming issuer and grant generations first,
then the winner, failed attempt, close reason and pending-page counters.

The supplied VM timing log likewise reports generic archive/provenance failures
without scope references. A manual Pause then Sync now on VMTEST is a safe
diagnostic comparison: it selects a current admitted scope without issuing any
grant or changing trust. Its physical result is pending.

Passed: enrollment/history/session-scope regressions, signed server metadata,
mutual Schannel archive TLS, paired packaged profile-worker history/attachment
transfer and restarts, Settings smoke, full Windows build/import/package audit,
Android JVM/debug/test/release/lint, JNI/ARM64/TLS dependency audits, Development
version/manifest/hash/extraction, git diff --check. No server deployment or upload.
Development ZIP SHA-256:
`7e040d076ccd09c51f2f747ec61f092fec0bdea7c1f7a07cb6f200f02c179d14`.
