# Live group diagnostics — dev.89

The group Connection/Voice/Screen Share pages now refresh at most once per second
while their diagnostics window is open. They consume the existing application
group/room snapshots and copied GroupVoiceService/GroupScreenShareService session
counters. No new media collector, route, transport, reconnect or retry owner exists.
The existing application timer drives this view and its handler detaches on close.

Voice packet/capture/playout and screen capture/encode/decode/presentation statistics
remain the existing actual counters. Unavailable statistics are not synthesized.
The live group snapshot is separate from the full/direct snapshot. Retained failure
reports stay immutable; switching to a failure never refreshes its evidence. An
authentication owner/generation change invalidates the live group view. Copy/save
uses the displayed selected group snapshot and existing privacy filtering.

Validation: native media observer/audio/lifetime tests, rendered WPF open group
view refresh and context invalidation, detached/copied counters and frozen evidence;
group envelope/membership/revocation security, group voice session ownership and
group screen shared capture/encode/fanout regressions. Windows full build/import
and Development updater audit; Android JVM/debug/test/release/lint/ARM64 audits.
Physical group media/statistics validation remains pending. No 3+ participant or
physical TURN acceptance is claimed. dev.88 is an intermediate local build; dev.89
is the final aligned candidate after separating live and retained diagnostic state.

The optional older Stage 1A harness initially failed to link
createShippingHpke, because its Node compile omitted the existing production Go
provider. The harness now compiles that provider and stages its DLL through the
normal pinned build script. No assertion or production behavior was weakened.

## Group text history: blocked authorization prerequisite

The focused regression demonstrates that a different device using the same member
account receives LocalNotMember, and that a duplicate account in a multi-device
roster is rejected by current GroupKeyManager. Existing envelope tests require
current roster/key generations and reject a removed member. Server selection also
requires the exact authorized group device; the current schema does not publish a
historical membership proof. These are deliberate security constraints, not tests
to weaken. Archive approval alone cannot resolve them.

Before group text history can satisfy the approved same-account existing-member
and deny-new-member-old-history requirements, an explicit prerequisite is needed:
define authenticated additional-device group enrollment and bounded historical
membership/provenance verification, or explicitly constrain an initial milestone
to already-authorized group devices with independently verifiable records.
Neither change was inferred or implemented here. No group plaintext imports,
automatic enrollment, new keys, server/schema changes or protocol redesign occurred.
Group text-history cross-platform, restart and incremental acceptance is NOT green.
