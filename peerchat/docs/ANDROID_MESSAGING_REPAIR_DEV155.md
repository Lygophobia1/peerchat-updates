# Android messaging investigation and routing decision

Android messaging remains pending repair and physical retest. Checkpoint
`dc97469` supplies diagnostic APK `4.0.2-dev.155`; it does not establish the
phone's underlying failure. Independent transfer development is now authorized
while the user is unavailable. No live Pi changes or phone installation are
authorized in this pass.

## Confirmed account and delivery evidence

The phone (`61f70f…`) and Windows PHONE profile (`35049f…`) belong to account 4
with distinct device identities. Sammy belongs to account 5 (`0f1ae8…`).
Read-only server metadata showed Sammy's account-4 envelopes addressed to the
Windows device, followed by signed receipts for those destinations. Those
receipts did not consume phone-addressed envelopes. No mailbox recipient key
was registered for the phone. Registering a session or displaying Online is
not evidence of a working mailbox or peer-authenticated message route.

Both dev.147 and dev.153 Sammy reports show relay pairing followed by peer
authentication timeout; later attempts also close before completing that
exchange. This excludes the old Windows build as a sufficient explanation.
The phone's existing logs repeatedly report IllegalStateException from mailbox
servicing without the failing operation. No server-confirmed session
supersession has been demonstrated for this failure.

## Native and application boundaries

Mailbox servicing registers/reconciles the device recipient key before fetch,
then verifies/decrypts each envelope, durably imports it, creates a signed
receipt and acknowledges the server. It must not ACK an unverified sender.
An early registration failure prevents that servicing pass from reaching fetch
or outgoing mailbox submission. It does not invalidate an otherwise valid login.

The native initial-friend pin guard rejects automatic trust if another verified
historical device is recorded for that account. Android currently reports a
generic verification-required status when that guard refuses. Whether this is
Sammy's actual rejection remains pending the new diagnostic result; do not
silently approve the device or overwrite a stored pin.

Dev.155 adds fixed-stage mailbox/provider diagnostics, a bounded per-peer native
verification decision, and recent native message IDs with account/device
attribution. It excludes message text, keys, aliases, tokens and arbitrary
server/exception text. Server presence wording no longer claims delivery is
operational. These are diagnostic/presentation changes, not a routing repair.

## Validation and remaining evidence

The prior checkpoint passed 149 Android JVM/UI tests, ARM64 debug compilation,
APK version/signature checks, lint with zero errors and 14 warnings, native
protocol/endpoint lifecycles, encrypted mailbox and independent-device/restart
tests, and presence/mailbox lifecycle tests. The dev.155 signer matches dev.135.

The legacy Windows-host JNI harness cannot compile the Android POSIX attachment
adapter because sys/syscall.h is unavailable on Windows. This harness result
is blocked, not passed. It must not substitute for actual Android JNI/runtime
validation. No new phone test or live server mutation was performed.

When physical testing is available, replacement-install dev.155 with adb
install -r and preserve all app data. Close only the Windows PHONE-profile
session, keep Sammy available, and record one unique logical message ID per
direction. Copy full Android diagnostics including Messaging execution and
recent_native_row entries, and matching sender diagnostics. Check mailbox
registration state, native verification reason, durable receive and ACK before
claiming delivery. Repeat foreground reconnect and cold app restart. Retained
Windows copies may be recoverable through existing authorized History Sync;
do not reassign their identity or claim that recovery has occurred.

## Windows first routing requires a separate design approval

The current mailbox creates separately encrypted envelopes only for devices
with verified native pins, authenticated account mappings and eligible signed
recipient keys. Independent per-device envelopes survive another device's ACK
when already prepared. Live routing currently selects a current device, not an
account-wide preferred-device delivery transaction. Device identities and
history grants do not alone establish this new routing policy.

A timeout is not proof that Windows failed to import: its durable ACK can be
lost. Sending a phone envelope after that timeout can deliver to both devices.
Conversely, treating the account-level Delivered flag as a device-independent
receipt can prevent an envelope from ever being prepared for another device.
Changing a candidate sort order cannot satisfy the requested policy.

A compatible proposal needs an explicit authenticated preferred-device policy,
logical message delivery attempts with generation/expiry, durable account-wide
winner/commit reconciliation, revocation checks, and notification fencing.
Each selected device still needs its own ciphertext; never redirect an existing
envelope to another key. The design must specify how a lost commit/ACK is
resolved before fall back and which duplicate-import behavior is acceptable
under network partitions. Existing incremental History Sync remains recovery
and reconciliation, not delivery authority. This exceeds a small scheduling
change and is deferred for explicit approval; no fanout protocol or storage
schema has been changed for it.
