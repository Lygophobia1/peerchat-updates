# Physical history page boundary — dev.85

Dev.84 physical reports confirm current admission on both devices. PC has no
winner and receives `relay_duplicate_attachment` from the server. VM has processed
12 records and completed two conversations, then stops at archive data rejection.
The latter is after admitted transport/page processing, not missing grant consent.
Neither report proves the exact validation rule that rejects the next page.

Dev.85 adds constant-label receiver stages: current authorization, manifest/page
decode, request binding, record scope, historical mapping lookup/missing, account
binding, current permissions, generation/digest/signature, durable import and
cursor commit. The failed stage is retained in the existing bounded read-only
snapshot. No contents, credentials, keys or attachment bytes are recorded.
Validation and fail-closed behavior are unchanged; a missing mapping is not filled
from a remote message claim. Tests prove missing mapping rejects the whole page
before import and reports its distinct stage.

Server duplicate-attachment rejection is not by itself proof of an orphaned
binding; an existing same-scope attachment is intentionally excluded. Current
server bindings include the full archive scope/generations/grant IDs, and reverse
history directions are distinct. No live server binding or data has been cleared.

Physical next gate: capture both dev.85 snapshots after Sync now while stuck.
The VM data_rejection_stage is needed before choosing a security-preserving fix.

Passed: native enrollment/history storage/session scope, signed server metadata,
mutual Schannel TLS, paired packaged Node history/attachment transfer and restarts,
Settings smoke, full Windows build/package/import audit, Android JVM/debug/test/
release/lint, JNI/ARM64/TLS dependency audit, Development version/hash/extraction,
and git diff --check. These are automated results, not physical acceptance.
Development ZIP SHA-256:
`d39913a358a97b608af44ed048b1c9e81385f0513b2a8c632cbc93ca0cb2b708`.
No publication, server reset/deployment, trust mutation or profile data deletion.
