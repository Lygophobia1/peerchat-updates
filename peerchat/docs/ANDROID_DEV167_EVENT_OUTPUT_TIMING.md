# Android dev.167 event output and owner timing

## Evidence and scope

The physical dev.167 snapshot reports an archive rendezvous `relay_pair_timeout`, current signed scope, fresh metadata, allowed source/reader grants, and no data/source rejection. That snapshot establishes pending source pairing, not a grant failure or proof that the source was executing. It contains no stage durations capable of attributing the separate message/search delays to one cause. The file-transfer tail of the pasted snapshot is visibly garbled; it must not be interpreted as trustworthy complete measurements.

Source inspection found one unbounded execution boundary: `MessagingBackend.tick()` held the owner gate while `ForegroundEvents.heartbeat()` wrote queued group frames and heartbeats to the authenticated event socket. `openSubscription()` disables the idle read timeout for that long-lived stream, and the original socket `write()` had no independent deadline. A stalled event write could therefore block the owner, delaying message projection, application operations, and later archive/relay processing. This is a demonstrated code path; the supplied physical snapshot does not prove that a write actually stalled during that run.

## Client change

The existing event stream now has one bounded output execution adapter. Its producer only adds to the existing maximum 32-frame queue and never writes to the socket under the application owner gate. The writer sends the same group frames and 20-second event heartbeats. Each write has an independent 10-second socket-close deadline on the existing shared deadline executor; no thread is created per frame. Read idleness remains permitted. A failed or ambiguous write is not replayed on that socket; its queued frame is retained until stream retirement. Authenticated reconnect resets/requeues the existing native durable group pending state through the established path.

Process-local exported timing rows identify owner gate wait, whole ticks, contact/bootstrap refresh, group refresh, mailbox work, archive work, native relay servicing, and native/application projection. Rows contain only a fixed stage name, call count, last/max milliseconds, and failure count. Individual slow stages (at least 250 ms) are logged with fixed labels; no request bodies, credentials, tokens, message text or exception messages are recorded. Native relay servicing uses a fresh monotonic clock after preceding network work. Group/archive reconciliation deadlines are measured from operation completion, so a slow request does not consume its own following refresh/retry interval.

Other bounded RPC work remains serialized by the existing owner architecture. The new measurements make its cumulative cost visible; this checkpoint does not claim that every network stage is asynchronous or that all physical delays share the event-write cause. It does not change transfer caps/windows, History Sync grants/cursors, native message security, account authorization, or relay pairing policy.

## Focused validation

Offline Android host tests passed 13 tests, zero failures/errors: EventOutputPumpTest (3), SocketWriteDeadlineTest (2), OwnerStageTimingsTest (2), ArchiveWakePolicyTest (3), BackendLeasePolicyTest (3). Production and test Kotlin/Java compiled successfully. Fixtures verify that a blocked writer does not block producers, the queue stays bounded, failed output remains queued without ambiguous replay, idle heartbeats do not need owner ticks, a stalled write is interrupted despite an idle reader, completed writes cancel their deadline, and serialized stage costs/gate wait/failures are measured separately without private exception text.

No physical latency, source availability, transfer performance, Motorola background, or Wi-Fi/cellular acceptance is inferred from these host fixtures. No phone access, app-data reset, server modification, or deployment was performed.
