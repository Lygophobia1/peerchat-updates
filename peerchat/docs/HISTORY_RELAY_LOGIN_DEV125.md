# History relay cancellation and Android login isolation — dev.125

Physical Windows/Android History Sync remains FAILED / PENDING RETEST.
Valid exact signed grants in the supplied account-4 diagnostics do not need
replacement. No identity, grant, permission, history or attachment was changed.

## Proven boundaries

- Android's incoming archive demand used `require` for the source role. The
  account-wide server notification for a source attachment can reach its reader;
  Windows ignores that role echo, Android threw it into the general tick handler.
  That handler set serverPresenceLive=false. The production UI observer then
  scheduled a 30-second startup watchdog even for an established login, calling
  resetNetworkSession and navigating to Login. This is a client-side path, not
  proof of a server-confirmed token invalidation. The user's exact physical
  logout timestamp/cause remains to be correlated with the new diagnostics.
- The real pinned-TLS relay regression reproduced a disconnected pending socket
  retaining ownership during the old 15-second pairing wait. A retry can collide
  with that slot and receive relay_duplicate_attachment. The pre-fix regression
  also exposed delayed isolated-server shutdown; its cleanup exception masked
  the first assertion in the final test output. After the correction, cancellation
  releases ownership within the test's three-second bound (observed milliseconds).
- Android assigned its cancellation socket only AFTER openRelay returned from
  pairing. Closing its execution during rendezvous could not interrupt that read.

## Narrow corrections

Archive role echoes are ignored; malformed/unavailable archive work is caught at
the archive boundary, while explicit session_invalid/session_superseded/
event_session_superseded still propagates. An established login reconnects without
the initial-startup watchdog resetting it. Explicit user reset/logout and bounded
initial startup failure retain their existing behavior. Cancellation owns the
Android socket before rendezvous waits.

The server waits on pairing and a single cancellable TLS read together, reusing
that read after readiness. No concurrent reads, timeout increase or duplicate
suppression: duplicates remain rejected and a valid winner remains intact. Only
accepted reader demand recruits an archive source; rejected duplicates and source
echoes cannot emit new archive work. No additional retry/session/transfer owner.

Safe diagnostic records identify local attempt number, reader/source role,
server endpoint/pair correlation IDs, stage, duplicate rejection and ownership
cleanup. Android distinguishes authentication_invalidated from login_navigation
(startup_presence_timeout/manual_network_reset), including generations. No
session tokens, keys, credentials, message text or attachment bytes are logged.

## Validation

- 140 Android JVM/UI tests, zero failures/errors; debug/test/release and lint.
- Established login survives five 31-second reconnect/archive-failure cycles,
  does not authenticate again and still sends; initial startup timeout remains.
- Role/account/device checks and real invalid-session classification retained.
- Actual signed-grant/pinned-TLS archive relay in both directions: cancelled
  pairing, repeated retries, duplicate pending/active attachments, winner survives
  bidirectional opaque forwarding, account tokens retained. Genuine newer event
  session supersedes the old device session and its subscription is rejected.
- Native mutual Schannel archive handshakes, verified imports, durable cursors,
  restart/replay/deduplication, stale/revoked/wrong-account grants and scope isolation.
- Packaged Windows Node archive/C ABI restarts, actual shared TransferManager
  encrypted file/journal/reconnect/message-ACK coexistence; manager-owned relay
  retries and 100 physical socket retirement cycles.
- Modern portable server parity: signed devices, groups, rooms, presence (1,000
  lifetimes), real pinned-TLS mailbox sender-disconnect durability, signed ACK,
  receipt restart, quotas, TTL, revocation and device removal.
- Windows full build/runtime package/import audit; unified dev.124 -> dev.125
  Development updater version/hash/extraction; ARM64 JNI/dependency/release
  containment. Prior unrelated long-message layout performance failure is not
  claimed fixed. No physical acceptance claim or phone/data access.

Development artifacts (source-root-relative):

- build/development/4.0.2-dev.125/Client/PeerChat.Desktop.exe
- build/development/4.0.2-dev.125/DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.125.zip
  SHA-256 6582e2dc489407a1225f3468deb3022a60f7ed1ec44b539abb12312be3b518cd
- Android debug/test/unsigned release candidates are preserved under the same
  development version. ARM32 Linux candidate uses pinned self-contained runtime
  10.0.12, not a new OS/.NET installation. No merge or release publication.

## Standing Pi update authority

The user authorized compatible validated PeerChat server updates on 2026-10-13.
Routine updates use existing /opt/peerchat-test tooling: inspect, stage/hash,
verified runtime/config and coherent SQLite backups, controlled restart, health
and binary rollback using the CURRENT database. Never overwrite newer writes
with an older backup automatically. Keys/certificates/credentials/coturn/firewall
and unrelated services remain untouched. Destructive schema/data changes,
identity/grant resets, key replacement or new public exposure still need approval.
Deployment evidence and rollback location are recorded separately after execution.

Pi validation first exposed a listener-start race in the new isolated archive
test, corrected by waiting for actual listener readiness (not a production
timeout change). The first combined ARM32 parity run stalled on group receipts;
the isolated real three-client fanout rerun passed unchanged. The full ARM32
parity rerun is required before deployment and remains separately recorded.

## Actual Pi deployment result — 2026-10-13

Deployed dev.46 -> dev.125 to the existing ARM32 Pi with the same public/LAN
endpoint (173.225.14.37:7443 / 192.168.1.6:7443). Actual process PID 24620;
coturn remained PID 2718. No firewall, TURN, OS, systemd or certificate change.
Port 17443 is not listening. Certificate SHA-256 remains:
62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.

The first preservation gate detected an additive empty group_rosters.creation_proof
column. Automatic binary rollback to dev.46 succeeded using the CURRENT database;
no old database was restored. Private comparison proved every original account,
device, remembered credential, descriptor and grant column/row unchanged. The
updater now compares exact original columns (and rejects missing columns/tables)
instead of conflating an additive column with changed user data. Verified
immutable staging was reused; prior evidence remains preserved.

Final rollback directory:
/opt/peerchat-test/rollback/pre-4.0.2-dev.125-20261013T052808Z
Contains verified dev.46 runtime/config copies and coherent online/stopped SQLite
backups, both integrity_check=ok. Both DB backup SHA-256:
4e505198b96a7a3d973e5cdd2f1a92ca5b444f96fd85ffede00c8003748b61ee.
The earlier rollback directory ending 20261013T052612Z is retained too.

Final server:
/opt/peerchat-test/server/4.0.2-dev.125/PeerChat.Server
Config:
/opt/peerchat-test/config/4.0.2-dev.125/server-config.json
Configuration bytes are identical to dev.46, including existing DB, certificate,
password-file, FCM and TURN references. DB remains:
/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db.
No user records, keys, credentials or private material were printed or exported.

Passed on actual ARM32: full portable server parity rerun, signed archive relay
both directions/cancel/duplicate-preserved winner/real supersession, existing
test-account authentication/discovery over unchanged pinned TLS, real FCM OAuth
and cache reuse (no push sent), current DB integrity and backup integrity.
Isolated evidence under:
/opt/peerchat-test/validation/dev125-7fdb5ac548644a4392570b80750f4513
(parity-retest.log, archive-relay-retry.log, group-fanout-retry.log).
The first combined fanout stall is retained as non-reproduced test evidence,
not silently claimed fixed; subsequent isolated and complete reruns passed.

ARM32 server ZIP SHA-256:
792adb034f0ac94d1ab7cdfa8b6047eb761586c815b7f7f2bcfa72590dd2043c.
Android debug candidate SHA-256:
408055be5678f4487ec6f005bc7d978c15d2f4c276ae8ca80b275a3c5e27f6bb.
No APK was installed; physical Android History Sync acceptance remains pending.
