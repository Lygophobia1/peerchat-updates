# Network cleanup Phase 1

Isolated branch: `codex/network-cleanup-phase1`, based on accepted
`249f462` / 4.0.2-dev.19. No merge, key changes, protocol crypto changes,
media algorithm changes, or server schema changes.

## 1. Previous Android bootstrap behavior

While in use, the application ran a complete `messaging_bootstrap` roughly
every five seconds: authenticated presence, friends/requests, native identity
projection, protected display-context persistence and UI history projection.

## 2. Foreground events

`ForegroundEvents` executes `ServerAccounts.openEvents()`, the existing pinned,
authenticated `events` subscription used by Windows. Its bounded reader queue
contains at most 128 event records. The existing application coordinator drains
32 per pass and sends the existing 20-second heartbeat. Queue overflow is a gap:
the channel closes and authoritative reconciliation follows reconnection.
The reader performs no delivery, trust promotion or route selection.

## 3. Previous mailbox polling

Windows and Android normally invoked combined mailbox fetch/receipts/upload work
every five seconds, including when empty. Each operation opens fresh server TLS.

## 4. Mailbox triggers

Existing `mailbox_ready` / `mailbox_receipt_ready` events request servicing from
the existing application mailbox executor. Startup/reconnect, explicit privacy
changes and native durable send acceptance also request work. Android FCM uses
the same bounded MessagingBackend. Windows preserves hints arriving during an
in-flight pass rather than overwriting them with the next repair deadline.
Bounded batches continue when a page has more work. Already-Stored messages do
not keep upload servicing alive just to await receipts; independently pending
device envelopes remain included through existing durable upload records.

## 5. Previous eager route policy

Windows offered relay routes for every friend and on conversation selection;
Android online presence automatically produced relay requests. Presence and
route demand were consequently coupled.

## 6. Demand-driven routes

SessionTransportManager gates server-assisted relay attempts on live demand.
Pending non-Stored native outbox work renews demand. Explicit call/share/reconnect
commands and authenticated incoming rendezvous hints provide a bounded 45-second
establishment/control window. Active native call/share state renews demand.
Idle relay routes retire through the same owner; retries remain owner-controlled.
Existing explicit manual direct and scoped P2P mechanics remain intact.
This is not a conversation-selection warm-route policy.

## 7. Incoming reachability

After the existing relay authorization succeeds, the server emits additive
`relay_demand` over its existing authenticated event stream. It identifies the
requesting account/device, intended recipient device and existing purpose/scope.
The recipient checks its own device and authoritative friend mapping before
requesting native demand. The event contains no message or key material and
does not authenticate peer TLS or grant trust. Group-key rendezvous retains its
separate existing roster authorization/control path.

Mailbox-forbidden Android wake work may consume the same event stream and execute
only genuinely demanded native routes within the existing finite OS wake lease.
Mailbox-enabled bounded wake remains independent of peer-route establishment.

## 8. Windows event reactions

Mailbox hints service the mailbox domain. Group roster/room/media/ciphertext hints
retain their existing domain handlers. Group invitations refresh only invitations.
Contact/request/presence hints repair the bounded friend domain, not invitations
or all group state. Candidate updates are emitted only for affected presence or
device observations; ordinary selection does not demand a route.
Initial event connection and reconnection trigger authoritative repair.

## 9. Slow repair

Healthy foreground events: 15-minute repair opportunity. Degraded Android:
two-minute repair; Windows friend repair remains three minutes, mailbox two.
These repair opportunities are not Android background radio alarms. A finite
background lease remains bounded by the existing OperationScope/job deadline.
Interrupted pending work remains governed by the existing OS wake/retry path.

## 10. Android lifecycle

Foreground use opens one existing event subscription. Quiescence cancels its
socket through OperationScope and clears transient authorization while keeping
the native Profile and local account-history projection. No foreground service
or Kotlin session/outbox is introduced. Event disconnect projects reconnecting,
not online merely because a remembered credential exists.

## 11. Removed policy

- Five-second full bootstrap and normal empty mailbox/receipt timers.
- All-friend presence-based connection demand and selection-based eager routing.
- Group-invite fetch on every friend/presence hint.
- Native friend-generation replacement on unchanged Android identity snapshots.

## 12. Retained infrastructure

SessionTransportManager, DeliveryManager, durable outbox/history/ACK/dedupe,
peer TLS, opaque relay streams, server-authorized friend/group scopes, bounded
FCM wake scheduling, Go HPKE/CNG and Android BC/Keystore, native screen/voice
engines and TURN-only media policy. Existing trust verification remains mandatory;
an unverified device cannot queue plaintext merely because its account is online.

## 13. Fresh TLS still used

Authentication/Remember Me and ordinary server APIs open fresh pinned TLS requests.
Mailbox registration, key lookup, store, fetch, ACK and receipt operations also
remain individual RPC connections. Events retain one foreground subscription;
each demanded relay execution retains its existing stream. No RPC pooling or
multiplexing redesign is included.

## 14. Validation, request model and risks

Before, foreground Android approximated 12 full bootstraps/minute plus 12 combined
mailbox passes/minute, independent of changes. Windows likewise approximated 12
mailbox passes/minute plus its slower repair snapshots. A combined mailbox pass
normally makes at least fetch and receipts requests, plus real upload/ACK work.
After, healthy idle foreground operation has event heartbeats (three/minute) and
approximately one domain/mailbox repair opportunity per 15 minutes; actual domain,
send, receive and ACK activity produces additional requests. No measured battery
or bandwidth savings are claimed.

The new server build is required for live incoming-demand hints; old servers do
not implement that additive hint. Use matching Windows/Android/server candidates.
Real contact verification, no-storage delivery, idle-route retirement, foreground
event lifecycle, locked mailbox delivery and media commands require user physical
acceptance. Existing bounded native outbox scans still have their supported page
limits; this phase does not implement unbounded history synchronization.

An intermediate dev.20 validation package was allocated and preserved. Subsequent
corrections use dev.21 for the coherent handoff; no prior package is overwritten.
Automated results and final artifact paths are reported with the checkpoint.

### Dev.21 automated and basic handoff evidence

- Full direct Windows build, package/import checks, Go HPKE containment and
  native group voice/screen ABI smoke: passed.
- Android JVM suite: 84 tests, zero failures/errors; debug, instrumentation APK,
  release and lint: passed. ARM64/JNI, TLS and HPKE artifact audits: passed.
- Reliable peer protocol (1,000 cycles), demand-driven relay requests (1,000
  cycles plus idle/incoming/backoff invariants), mailbox protocol/persistence/
  signed ACK/sender-disconnect and three-client encrypted group fanout: passed.
- Actual Node baseline/current characterization and dedicated reconnect,
  asymmetric EOF, scoped P2P, handoff and relay-signaled media recovery: passed.
  The reconnect fixture now explicitly requests control demand; it no longer
  relies on the deliberately removed idle-contact eager-connect policy.
- Discovery/auth/device ownership, event lease expiry/replacement, friendship
  and durable account restart regressions against the dev.21 server: passed.
- Shared/generated Windows and Android versions agree; Development updater
  preparation, safe extraction and exact runtime hashes: passed. Unsigned
  Development policy only; nothing published.
- Motorola replacement install: passed. Signing identity, protected Remember Me
  record and recipient-key registry hashes were identical immediately before
  and after installation. User confirmed Chats; foreground authentication and
  events connected. Short idle logs contained only startup/event-repair bootstrap
  and mailbox passes, not five-second repeats. This is not locked-delivery or
  media acceptance. Windows launched its responsive existing profile chooser.
- `git diff --check`: passed. Generated compiler objects excluded from checkpoint.

Artifacts relative to this isolated repository:

- `build/development/4.0.2-dev.21/Client/PeerChat.Desktop.exe`
- `build/development/4.0.2-dev.21/ServerAdmin/PeerChat.Server.Admin.exe`
- `build/android/4.0.2-dev.21/PeerChat-4.0.2-dev.21-debug.apk`
- `build/development/4.0.2-dev.21/DevelopmentUpdater/PeerChat-4.0.2-dev.21.zip`

APK SHA-256: `eeee1aba3e4150a924f18b4df1650cfd720beb5e25c0284ab125edca760d2da4`.
Development ZIP SHA-256: `38f989b91b2ecc01223370248f24e0d7632701e762eadb4215f2b33f74c102f7`.
The currently running older development server was left untouched. Start the
matching dev.21 server with the existing configuration/data before testing new
incoming-demand behavior. No complete physical acceptance matrix was performed.

## 15. Recommended Phase 2

After physical acceptance: monotonic/in-memory presence leases and server epoch,
reduced durable heartbeat writes, then a separately scoped persistent RPC reuse
design. Linux/ARM64 server portability, P2P redesign, files/history synchronization,
video and media optimization are not part of this checkpoint.
