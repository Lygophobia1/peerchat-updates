# Profile-worker archive enrollment boundary — dev.49

This is a shared native application-state prerequisite, not completed enrollment
UI or a physical archive-sync candidate. Platform RPC adapters, device selectors,
route demand and visible provenance remain to be integrated. No permanent key,
transport owner, reconnect timer, history database or server feature is added.

Enrollment borrows the existing profile/storage and owns its archive Authorization
instance. It exposes bounded device views and explicit approve/authorize/revoke/
disable commands. Those commands require user consent and current authenticated
metadata; ordinary same-account registration cannot grant archive access.

The platform supplies bounded signed public descriptor/grant metadata from the
authenticated server, bound to account, local device, authenticated generation
and strictly increasing response revision. Unknown authorizers are never pinned
automatically. Metadata validates canonical bytes/signatures/device key bindings.
Removed authorizers can have tombstones verified against an existing pinned public
key, but cannot authorize a current session. Missing/invalid/stale metadata fails
closed; permission expires after 30 seconds unless refreshed by the existing
worker. An explicit invalidation immediately denies further access.

Self-descriptor registration is protected and durable under a domain-separated
context. Reopening reuses the exact signed registration, not a different timestamp
or identity. Descriptor publication is public metadata only, not enrollment.

## Durable grant publication

Local policy v2 atomically stores the latest signed policy and a bounded journal
of at most 32 pending signed public grant records. Existing v1 local policies
remain readable; signed grant wire format, domains and identities are unchanged.
The existing protected 64-KiB policy bound remains enforced.

Issuing a revoke and re-enrollment before the server is reachable preserves both
intermediate generations. Platform servicing retries the exact stored bytes in
generation order through the existing bounded worker. There is no new retry owner.
Authenticated matching current-generation metadata retires confirmed publication
intents. A transport-level success or account login alone does not retire them.
An exact verified record returned by authenticated server metadata also confirms
publication when removal/re-add tombstones make that record ineligible. This
retires only publication intent, never grants access, and permits explicit
re-enrollment to publish the next generation instead of being stranded.

Queue/persistence/signing failures leave the earlier policy and journal intact.
Capacity failure is visible and does not silently discard older grants. Locally
disabling an authorizer remains distinct from distributing signed revocations;
neither is represented as deleting history already legitimately received.

## Automated coverage

tests/test-archive-security.ps1 -Enrollment covers explicit source/reader consent,
same-account denial, account/device/generation/revision binding, exact durable
descriptor retry, protected journal restart, intermediate revoke/re-enroll,
tampered and duplicate metadata, freshness expiration, reconnect, device removal,
no silent re-enable, bounded publication capacity and unchanged profile identities.

Existing authorization tests additionally cover original opaque signing,
provider failure without policy mutation, v1 local-policy compatibility and
exact-generation publication reconciliation. Platform/UI integration is pending.

The Pi server remains the validated dev.46 instance on 7443 (PID 23441 last
verified); this client-only checkpoint does not require redeployment or changes
to its database, certificate, TURN, FCM or external secrets.

Dev.48 passed its build gates before the stored-but-disabled publication edge
case was added. It is preserved, not overwritten or presented as the final
corrected checkpoint. Dev.49 rebuilds the corrected source into a new revision.

## Dev.49 validation evidence

Passed: enrollment/authorization/provenance and bounded text history tests;
archive and existing group scope tests; 12 mutual Schannel archive TLS cycles
including four real encrypted history pages; native/server proof interoperability;
102 ordinary mutual Schannel endpoint cycles; 1000 presence cycles and offline
composer/stop-drain regressions; complete portable Storage/identity/DPAPI/profile/
history/outbox/ACK/dedupe/provider tests; strict Android ARM64 shared-header syntax.

Full Windows build/package/import/restricted-load audits passed. Android's 94 JVM
tests passed with zero failures/errors; debug/test/release builds and lint passed.
JNI/ARM64/pinned TLS/dependency/release containment audits passed. Dev.47 -> dev.49
Development updater safe extraction/version/exact runtime hash checks passed.
The package SHA-256 is
0f2dad976f1ca5fe5e5453fd03a878585ef6c5f4087dba021bf675e2b91d47aa.
APKs are preserved under build/android/4.0.2-dev.49. No phone installation,
physical archive acceptance, publication or merge was performed.
