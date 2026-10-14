# Diagnostics instrumentation / performance backlog (not implemented)

This inventory does not authorize development during the unified diagnostics completion milestone.

- Connections: retained native session/attempt/demand identifiers, establishment timestamps/duration and correlated recovery outcomes. Do not reconstruct a historical session catalog from unrelated events.
- Transfers: selected-transfer actual in-flight/ACK latency/retry/queue metrics, encryption/decryption/socket costs and authenticated Pi relay observations, with ownership and byte semantics. Measure bottlenecks using the current subsystem before any separate protocol optimization. The 256 MiB limit stays unchanged; 5 GB work needs separate compatibility authorization.
- DM media: unavailable pinned-ABI wire traffic/RTT/loss/congestion, end-to-end capture/playout/frame age, screen-only start time and completed-call history. Do not replace unavailable values with payload rates or queue-age estimates.
- Group media: group bridge export of per-source NetEq/concealment and native route IDs/generations, shared encoder costs/queues and correlated join/offer/first-packet/first-frame timing. Remote playback cannot be inferred by the sharer. Investigate the delayed second viewer separately; do not optimize here.
- History: reliable eligible totals, separately acknowledged source transmission count, deduplication count and safe per-conversation progress/cursor metadata. Exposing metadata must not trigger scans or read/change protected history/cursors. Unresolved historical ownership remains fail-closed.
- Logs: a safely profile/session-attributed application timing source, explicit retention-read completeness/health and structured correlated recovery outcomes. The existing global temp timing log is not imported automatically.
- Crashes: actual exception/stack/dump/termination capture and explicit privacy/retention policy would be a separate crash-engine task. Existing unfinished-operation markers are not proof of a crash or exact exit time.
- Performance acceptance: physical multi-PC voice continuity, 30 FPS screen pipelines, multiple viewers, UI/collector CPU cost and actual LAN transfer bottlenecks remain separate from fixture projection and native startup tests.
