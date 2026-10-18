# Networking improvement backlog

See the [audit](2026-10-17-dev176-independent-audit.md). The user subsequently approved registration/readiness, attachment lock separation and 60-second grace repairs; their implementation and validation are tracked in [dev.177](../../NETWORKING_READINESS_DEV177.md). Other entries remain proposals. This backlog is not deployment authority.

| Priority | Item | Acceptance gate |
| --- | --- | --- |
| P0 | Scoped accepted registration implemented in dev.177; physical acceptance pending | Startup and generation-reset recovery; stale/revoked/cross-profile replies rejected |
| P0 | Bounded process-local picker readiness implemented in dev.177 | One job after readiness; cancellation/supersession protected. Before native job creation, process death still requires reselecting. |
| P1 | Attachment import/preflight waits moved outside gate in dev.177; independent control scheduling remains proposed | Delayed import/preflight permits local work; replacement login protected |
| P1 | Passive 60-second grace implemented in dev.177; physical power/latency measurements pending | No new wakelock/permanent service; reuse, expiry and security invalidation tests |
| P1 | Correlate failed request with generation/time and current readiness | No secrets/content in diagnostics; observed auth not mislabeled pure TLS time |
| P1 | Own-account Device Manager and cross-device authorization — approved and implemented for dev.179; physical acceptance pending | [Implementation and validation](../2026-10-17-device-manager-implementation.md): scoped management proof, non-destructive tombstones, retained frozen targets/history, bounded client reconciliation and legacy-session invalidation. No automatic reactivation or self/last-device revoke. |
| P2 | Fair event-driven cache continuations | Integrity/resume/replay and concurrent text latency; bounded memory/work |
| P2 | Server disk operations outside global account locks with authority fences | Revocation/relationship fencing, fixed lock order, crash/quota tests and separate Pi approval |
| P2 | Reproduce generation-superseded mailbox rescheduling with retained executor | Demonstrate persistent loss before fixing; in-flight sentinel alone is insufficient |
| P3 | Identity-preserving cache/direct fallback after ambiguous upload | No duplicate publication or silent audience narrowing; no security-denial bypass |

Physical evidence still needed: bidirectional durable ACK stages, active History Sync interference, Pi pressure, Android battery/memory and wake-to-ready timing. Preserve accepted media behavior; no Android voice or unrelated roadmap work.
