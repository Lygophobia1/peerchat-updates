# dev.188 delivery latency and admission investigation

## Scope

Retained worktree `native-unicode-emoji`, branch `codex/native-unicode-emoji`; starting checkpoint `0cd7ead` (production dev.187 `52b125f`). Disposable codex/codex1 profiles retained. No key, friendship, history, database or app-data reset. Pi remains dev.186; changes are in the clients and shared local storage, not the relay protocol. Physical phone acceptance remains pending.

## Changes

- Logical-account send resolves the current canonical device while holding the backend gate and verifies the initiating local account. A device replacement between tapping Send and coroutine execution no longer submits an obsolete device ID. Native admission and trust checks remain authoritative.
- Incoming authenticated relay wake hints survive a contact refresh for up to 15 seconds, bounded to 32 devices and once per authorization generation. Only the exact current account/device and a nonrevoked binding can produce demand. Session replacement clears hints.
- Android closes a waiting physical rendezvous when its native request has been revoked by a device/network change. Previously unattached sockets skipped native validity checks and could occupy a Pi relay binding until its 15-second timeout.
- Live queue, receive, peer ACK and mailbox publication read the exact protected native row, including its real history cursor and native target status. Complete clean cached pages merge that row. Missing, dirty or changed-device pages still reconcile normally; incremental updates do not postpone the 60-second safety refresh.
- DM history ordering caches authenticate new records using the existing append-only archive index. A delta-count mismatch (including deletion plus replacement), missing cache, oversize history or non-append change retains the full protected scan. The page payload is still read from protected storage; no decrypted ordering metadata is persisted. Group history is unchanged.
- Optional send wake RPC uses a bounded scoped background lane. Expensive registration diagnostics wait until no resume/bootstrap/relay work is active. Neither operation controls durable admission or marks a message delivered.

## Evidence and unsuccessful experiments

The earlier admission failure was reproduced with `canonical_matches=false` and `known_matches=true`: the fixture submitted a historical device through an API requiring the current canonical device. The fixture now waits for the independently known selected device. Production UI account resolution was fixed separately and covered for device changes and local-account replacement.

A reconstructed dev.187 baseline of 16 alternating sends had median receipt 3,092 ms, range 2,362–27,424 ms, and four receipts over 10 seconds. Initial wake-hint retention passed 16 rounds (median 2,690.5 ms) but a subsequent run still stalled at 25,520 ms. That first clean run was not sufficient evidence of repair.

Moving only the wake RPC off the owner worker did not improve the warm-run median. An initial exact-row experiment omitted the local cursor and was rejected by the existing parser; those timings are not acceptance evidence. The query was corrected to read the durable rowid. The final JNI contract test checks cursor/content against full history and rejects a wrong peer and malformed message ID.

After stale-socket retirement, 32 forced reconnect sends all obtained the selected-device ACK, with no admission failures or receipts over 10 seconds. Before the incremental ordering-cache fix, their median was still 3,743.5 ms (2,109–9,243 ms). A warm run at that stage had median 725 ms (290–4,691 ms including startup). These runs demonstrated remaining protected-history contention rather than proving instant reconnect delivery.

All timings use one process monotonic clock and match generated native message IDs. Two retained Android owners share one x86 emulator and the live Pi; these are not physical-phone timings. Forced reconnect tears down and restores the sender session on every message, unlike normal warm chat.

ACK assertions confirm the independently selected receiving device. Offline queueing may use the existing canonical-only path with unresolved account selection. Missing secondary-device pins, offline targets and unresolved all-device delivery are preserved, never converted to account-wide success.

## Validation

490 Android unit tests passed with zero failures/errors; Android lint passed. Native relay-request lifecycle suite passed including request invalidation across canonical/network replacement. Native chronology/storage suite passed including exact-row single-record decryption, append-only bounded decryption, external append, delete-plus-replacement fallback, late/equal-time ordering, pagination, restart, cache eviction, and preserved delivery fields. Android ARM64 unit tests (490), lint and build passed; Windows full development build and dependency/load/lifetime checks passed.

## Final measured results (source 7020d07)

| Test | Sends | Local commit median / range | Receipt median / range | Selected-device ACK median / range |
| --- | ---: | --- | --- | --- |
| Warm session, both directions | 32 | 62.5 ms / 41–1,794 ms | 288 ms / 248–5,229 ms | 889 ms / 317–5,231 ms |
| Forced sender reconnect before each send | 32 | 49 ms / 31–78 ms | 2,578.5 ms / 2,085–4,068 ms | 2,824.5 ms / 2,298–7,347 ms |

All 64 sends reached the independently selected device and received its native ACK. No admission failures or 20-second receipt stalls occurred. Both runs passed the exact native-row cursor/content and rejection checks. The warm run includes session startup; its worst result was round 2, with 1,749 ms waiting for the backend gate while multiple native peer handshakes were processed. Most later warm receipts were a few hundred milliseconds, but the startup outlier is not excluded from the table. The reconnect median improved from the reconstructed baseline, but the original 2.6–3.5-second reconnect delay is not eliminated. Remaining work is reducing startup handshake/background-work contention without weakening authorization or retaining unauthorized connections.

The prior 32-send stale-socket run plus this final 32-send reconnect run did not reproduce the previous 20–27-second tail. This finite observation does not establish that the intermittent condition can never recur. The original unclassified admission failure is now explained by a reproduced historical-device fixture mismatch and a separately covered UI race, not by bypassing native rejection.

Pi dev.186 was verified running from `/opt/peerchat-test/server/4.0.2-dev.186/PeerChat.Server`. A server deployment is not required for these client/local-storage fixes. No Pi binary, database, configuration, or rollback state was changed.

Direction breakdown (16 sends per direction): warm receipt medians were 328 ms for codex → codex1 and 286 ms for codex1 → codex. Forced-reconnect receipt medians were 2,822 ms and 2,431 ms respectively. No direction was accepted based solely on the other direction's success.

## Windows interoperability and artifacts

Windows dev.188 and the final Android emulator build passed probe `4448b855b5404ff8957c586cbbfffda3`. Windows message `78e877227fe14ffcbb70d075b369da71` received the selected Android device ACK. Android reply `44d26e97c23c453b98971bc566f688e1` reached Windows and received its selected-device ACK. Android retained both rows after saved-session resume. Its target selection remained unresolved; account-wide success is not asserted.

Android's initial receive-stage wait was 17,574 ms, including coordinated Windows process startup and readiness, so it is not a one-way delivery measurement. Its reply-ACK wait was 6,810 ms. This interoperability pass demonstrates correctness, not consistently prompt Windows delivery. The Windows probe also refreshes metadata while pumping events; neither that harness effect nor startup contention has been isolated enough to attribute the whole delay. Physical Windows/phone acceptance and further startup-latency work remain outstanding.

Retained evidence directories:
- `build/tests/live-disposable-9144dff023384fed8171a244cb2cfd42`
- `build/tests/windows-live-runtime-cf2ba929e4de4786ad72bc6eeb89a4ac`

Production checkpoints: `b7746ac` for account send resolution and initial hint retention; `7020d07` for final relay/history repairs. Package version is 4.0.2-dev.188. Source ancestry, Android ARM64 ABI, existing signing identity, Windows runtime dependencies, ZIP contents and copied artifact bytes passed the packaging audit. Existing dev.187 deliverables are preserved. No physical device was installed.

| Artifact | SHA-256 |
| --- | --- |
| PeerChat-Android-4.0.2-dev.188-debug.apk | `40d7b372bdd4cff21f88bb60aefc34e1218cb60376d11bcbed572c2f683ad62e` |
| PeerChat-Windows-4.0.2-dev.188.zip | `b700c285b5fb413b9ab4823c983d1037335111981fbbb94c9eb8f55d0339360e` |

This candidate improves normal warm messaging and addresses reproduced races. It is not an acceptance claim that every send after reconnect, or every Windows/phone send, is instantaneous.