# Archive-scoped peer TLS boundary — dev.46

This is the next integration prerequisite, not completed end-user history sync.
Enrollment UI, platform route wiring, durable archive import/paging and attachment
fetch are still unavailable. No archive history is transferred by this checkpoint.

## Existing owners, narrower permission

SessionTransportManager owns archive transport instances and isolated continuity
using account, explicitly pinned issuer, source and reader. The context also
binds both exact current grant generations and issuance IDs. It cannot activate
the friend continuity, group key control, DM delivery/outbox, or media.

The shared portable ReliableEndpoint optionally consumes this admission through
the existing TLS/runtime/manager lifecycle. Default friend behavior is unchanged.
Archive TLS verifies the remote public key against the explicitly approved signed
grant, never persists a friend contact, and negotiates capability 2048. Frame 32
contains the exact domain-separated ArchiveSession/v1 binding. Both endpoints
must agree before reporting ready. No history message types are implemented yet.
Unexpected frames, including DM Text, fail closed. Current authorization is
re-evaluated on receive and before releasing buffered TLS output; stale snapshots,
revocation, disabled authorizers and changed generations cannot continue.

The Linux server accepts purpose archive_history only for two distinct devices
on the same authenticated account with exact current ServeHistory/ReadHistory
grants from the same issuer. Ordinary account membership alone is insufficient.
Pairing includes the complete grant-pair context; friend/group pairing is unchanged.
The server relays opaque peer TLS, not history plaintext. Readiness and each
forwarded record recheck admission. Signed grant mutations retire invalid open
archive relays. A device removal also makes future forwarding ineligible.

Server admission never overrides endpoint consent/pinned issuer validation.
No new socket owner, reconnect loop, identity or history database is introduced.

## Automated coverage

tests/test-archive-security.ps1 -SessionScope -PeerTls -ServerMetadata:

- purpose/device/route isolation and generation invalidation;
- reconnect using new instances without granting friend continuity;
- real mutual Schannel peer TLS on the shared endpoint, exact scope agreement;
- no contact pin creation, DM acceptance or ordinary outbox draining;
- stale admission and active-session freshness loss fail closed;
- native/managed grant proof interoperability and durable revocation.

The isolated Linux RPC harness additionally exercises real server relay pairing,
opaque bidirectional bytes, wrong account/device/grant rejection and closure on
revocation. These are automated tests, not physical archive-sync acceptance.

## Preserved Pi deployment evidence

dev.45 metadata checkpoint 8a85ae8 was promoted with the existing physical-client
DB, certificate, TURN and external Firebase configuration unchanged. PID 22773
listened on 7443 after promotion; coturn PID 2718 was unchanged. Exact certificate
SHA-256 remained 62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.
SQLite integrity and before/after account/device/friendship/group/mailbox/push row
counts matched. A consistent backup was retained before startup. Later deployment
evidence must identify the current running version separately.

## dev.46 gate results

Windows full build/runtime package/import and dev.45 -> dev.46 updater safe
extraction/version/hash audits passed. Android debug/test/release/lint passed;
94 JVM tests passed, with JNI/ARM64/pinned dependency/release containment audits.
Shared archive/endpoint headers also passed Android ARM64 strict syntax checking.
Storage/identity/DPAPI/history/outbox/ACK/dedupe/provider regressions passed.
Existing 102 mutual Schannel endpoint lifecycles, 1000 shared protocol cycles,
presence/auth/Remember Me/FCM/mailbox and group creation/fanout passed. Live file
transfer and protected process-restart transfer regressions passed unchanged.

All Linux runtime packages passed architecture/version/import-content audits.
Actual Pi ARM32 isolated lifecycle and archive metadata/relay tests passed on
17443, including active revocation closing both relay endpoints. The initial
relay fixture used numeric JSON fields instead of the existing relay protocol's
decimal strings; the harness was corrected, not production validation weakened.

Windows Development package SHA-256:
f928e369d92d8097441b34b235e861ff0f2d366edc7d1b3788ad25dba644de38.
APKs are preserved under build/android/4.0.2-dev.46. No phone installation,
physical archive acceptance, feed publication or branch merge was performed.
