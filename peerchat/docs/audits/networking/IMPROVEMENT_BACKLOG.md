# Networking improvement backlog

All entries are proposals, not implemented fixes. See the [audit](2026-10-17-dev176-independent-audit.md). No production development or deployment is authorized by this backlog.

| Priority | Item | Acceptance gate |
| --- | --- | --- |
| P0 | Reconcile scoped native/mailbox accepted registration before capability | Startup and generation-reset JNI tests become recovery tests; stale/revoked/cross-profile replies rejected |
| P0 | Bound and retain attachment intent across picker/reconnect | One job after readiness; safe cancellation/restart; no duplicate send |
| P1 | Remove inline attachment network/disk waits from shared gate; prioritize control/ACK | Delayed capability/import cannot block durable text admission; replacement login protected |
| P1 | Evaluate passive 60-second background grace | User approval first; compare 0/30/60/120 seconds and work leases; no new wake lock/permanent service |
| P1 | Correlate failed request with generation/time and current readiness | No secrets/content in diagnostics; observed auth not mislabeled pure TLS time |
| P2 | Fair event-driven cache continuations | Integrity/resume/replay and concurrent text latency; bounded memory/work |
| P2 | Server disk operations outside global account locks with authority fences | Revocation/relationship fencing, fixed lock order, crash/quota tests and separate Pi approval |
| P2 | Reproduce generation-superseded mailbox rescheduling with retained executor | Demonstrate persistent loss before fixing; in-flight sentinel alone is insufficient |
| P3 | Identity-preserving cache/direct fallback after ambiguous upload | No duplicate publication or silent audience narrowing; no security-denial bypass |

Physical evidence still needed: bidirectional durable ACK stages, active History Sync interference, Pi pressure, Android battery/memory and wake-to-ready timing. Preserve accepted media behavior; no Android voice or unrelated roadmap work.
