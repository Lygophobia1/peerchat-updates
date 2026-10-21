# dev.189 reconnect and acknowledgment latency

Starting checkpoint: f1bb213 (dev.188 production 7020d07). Retained disposable codex/codex1 profiles and message history were preserved. Pi remains dev.186: the measured bottlenecks were in the clients, and no server source changed. Physical-phone acceptance remains pending.

## Changes

- Indexed exact mailbox-object prefix ranges replace correlated LIKE scans, with unchanged protected content and target checks. On a synthetic 2,000-message fixture, results were identical and query time dropped from 1,744 ms to 3.6 ms.
- Mailbox preparation reads four protected messages per batch, matching its upload batch. Live transport alternates four recent messages with a rotating backlog page, excludes already submitted messages before decrypting, and retains every row until its authenticated ACK. A full 16-message window performs no redundant protected reads.
- Incoming Android frames wake receipt/ACK publication. Optional metadata starts one second after initial authorization; incoming History Sync authorization refresh uses the existing scoped background lane instead of holding the messaging gate during network waits.
- Successful authenticated relay winners reset failure backoff. Fresh authenticated incoming demand can expedite a disconnected route, bounded to once per second, without duplicating pending attempts or replacing active winners. Ordinary presence polling retains backoff.
- The Windows test fixture waits for the independently expected current device after reconnect instead of permanently failing during a transient canonical-device change. Production trust and native admission remain authoritative.

## Evidence and limits

Host tests cover 1,000 relay lifecycles plus retry/reset/hint boundaries; 102 real Schannel transport lifecycles; exact durable ACKs; a full submission window; newest-message latency under 80 retained messages; and eventual ACK of all older messages. Native durability/fairness and chronology checks passed. Android: 492 unit tests and lint passed. Full Windows dependency/lifetime audits passed.

Early experiments are retained in the evidence directory. SQL indexing alone did not fix protected-read contention. Limiting mailbox work alone left Windows live-outbox scans of 100 protected rows. After bounded live work, Windows ACKs fell to 123–231 ms in the first successful eight-round test. Before the final retry repair, a 32-reconnect run still had 9.8–10.4-second outliers. These are failed performance experiments, not final acceptance.

All live timings match native message IDs and use a single monotonic clock. Two Android owners run in one retained x86 emulator against the live Pi. Windows tests use a retained isolated Windows profile and that emulator. These are not physical-phone measurements. ACKs prove delivery to the independently selected device only; unresolved selection, unverified siblings and offline devices are preserved, never converted to all-device success.

Final results below include startup and retain the unsuccessful experiments above.

## Final live results

| Test | Sends | Arrival median / p95 / max | Selected-device ACK median / p95 / max |
|---|---:|---|---|
| Forced reconnect A | 32 | 1,710 / 2,181 / 4,550 ms | 1,774 / 2,320 / 4,552 ms |
| Forced reconnect B, after Windows interoperability | 32 | 1,601 / 2,481 / 4,493 ms | 1,611 / 2,844 / 4,496 ms |
| Warm Android, including startup | 32 | 253.5 / 3,579 / 4,495 ms | 354.5 / 4,545 / 5,329 ms |

All 96 sends obtained the exact selected-device ACK; no admission failure occurred. Both reconnect maxima were round 0. The warm run had startup contention in rounds 0–4; the remaining 27 sends had maximum arrival and ACK of 639 ms. Startup is still measurably slower than an established conversation; this does not claim instant first-message delivery or zero remaining latency.

Final Windows–Android interop: eight bidirectional round trips, including four Android reconnects. Windows outbound durable selected-device ACK: 62–189 ms (median 125 ms). Android reply ACK: 203–406 ms while connected and 608–711 ms after reconnect. The Android fixture's `windows_durable_receive` timer includes waiting for the next Windows send and its intentional five-second observation interval; it is not a one-way delivery measurement. Exact retained history survived reconnects. Some reply target manifests retain another pending sibling or unresolved selection, correctly preventing account-wide completion claims.

## Candidate

Production checkpoint: `efe7e22` on `codex/native-unicode-emoji`. Candidate version: `4.0.2-dev.189`. Android ARM64 and Windows packages are built from this checkpoint. Pi remains dev.186; no deployment was needed. No physical phone was installed or reset. Package hashes and signature/dependency/source verification are recorded separately in `messaging-repair-package-audit.json`.

