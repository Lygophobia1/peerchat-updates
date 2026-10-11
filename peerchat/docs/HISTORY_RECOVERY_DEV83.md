# History authorization recovery — dev.83

Physical dev.82 PC diagnostics show both required grants from the trusted VM
authorizer verified and matching server generations, but metadata aged 30,365 ms.
Native authorization expires at 30,000 ms; the idle application refresh interval
was 60 seconds. Windows and Android now refresh idle metadata after 20 seconds
(active refresh remains 10 seconds). Native expiry and failure behavior are unchanged.

A read-only inspection also proved the PC had an active v1 archive intent tied
to its explicitly disabled local authorizer. Restoration retried only that issuer.
An active restored intent now selects an existing current same-authorizer grant
pair for the same source, through the normal enrollment admission predicate.
No trust or permission is changed and no grant is issued. Paused/ambiguous v1
intents remain paused. Expired metadata, wrong account, revocation, unknown or
disabled authorizers remain fail-closed.

The replacement reader uses the existing issuer-scoped cursor namespace; old
scope cursors are retained, not copied or erased. Existing import deduplication
prevents repeated messages when the new authorization scope scans history.

Focused coverage includes active restoration with no compatible pair, switching
to an already trusted valid pair, actual message import, reopen, preserved disabled
trust, explicit pause and stale metadata. Existing history tests cover both
directions, restart cursors, replay/deduplication and revocation. Physical PC/VM
bidirectional transfer and reconnect catch-up must still be confirmed on dev.83.

Validation passed: native enrollment/history storage/session scope; signed server
metadata; 12 real mutual Schannel TLS archive handshakes; paired packaged Node
history/attachment transfer and five profile-worker restarts; Settings smoke;
Android JVM/debug/test/release/lint; JNI/ARM64/TLS dependency audits; full Windows
build/package/import audit; Development version/updater manifest/hash/extraction;
git diff --check. These are host/static results, not physical PC/VM acceptance.

Development package SHA-256:
`0bfd839cc9b92891249979ea2b2300bc0fd9b57749c1bf3fea303760e647602c`.
No publication, server deployment, identity change or real grant mutation.
