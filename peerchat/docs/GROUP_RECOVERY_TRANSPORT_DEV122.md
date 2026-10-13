# Authenticated read-only group recovery transport — dev.122

Continues the approved group enrollment roadmap after the dev.121 transfer
performance checkpoint. Production enrollment and key provisioning remain OFF.
This checkpoint does not claim a completed device enrollment or physical test.

The existing ordinary authenticated RPC owner now supports group_device_recovery.
The request carries the public device/group identities and last admitted epoch.
The database derives the current live lease and revalidates exact account,
registered device, token binding, live session owner, lease generation, server
epoch and current device-specific group membership through the existing strict
admission. No account-only authorization and no stale-lease fallback.

The response uses the existing dev.117 ordered durable before/after snapshots
and signed device proofs. Its 8 KiB/32-transition budget, atomic historical
capture, pagination and explicit unavailable/gap results are unchanged. No
roster is invented, no history/key is exposed, and missing ordinary membership
epochs are still unavailable until signed-ledger recovery is implemented.

Windows DiscoveryClient reuses its existing pinned TLS/RPC Request method.
The managed projection validates bounds, requested group/cursor, integer epochs,
completion/reason consistency, consecutive before/after generations, root and
legacy scope consistency, bounded member/device mappings and canonical proof
encoding. This is transport validation only, NOT signature verification or
cryptographic authorization. Native Replica/GroupKeyManager remain responsible
for signed-root/ledger/proof admission before any key authority changes.
No application command currently invokes the new recovery page to enable
production enrollment; Android enrollment remains disabled too.

Tests invoke the actual server Handle dispatcher against an isolated durable DB
and feed its actual response into the managed parser. No production test hooks,
listeners, live profiles, existing keys or services are introduced. Windows and
portable server tests exercise current versus stale/revoked/superseded sessions,
wrong device, missing identity/token, malformed/future/zero cursors, exact epoch
pages, and tampered scope/cursor/proof encoding/missing/duplicate transitions.
Existing native proof/signature/root/revocation/rollback/restart tests remain
authoritative; transport success does not imply an enrolled or key-ready device.

A test-only object[] assumption failed on Windows JavaScriptSerializer's
ArrayList; the fixture now accepts IEnumerable just like the production parser.
No security assertion was removed or weakened.

Remaining activation gates: native application recovery transport consumption,
full signed membership-ledger bootstrap across ordinary roster gaps, explicit
same-account approval/revocation UI, fresh current-key provisioning and actual
application enrollment/revoke/reconnect validation. No historical message access
is granted by device approval alone. All existing grants/identities/history and
disabled-authorizer choices remain intact.

Android dev.119 background/locked transfer acceptance is preserved. Dev.121's
worker pacing fix and measured Pi TLS latency evidence are preserved. Physical
throughput validation remains pending; no ADB access or new physical test request.
No merge, publication or deployment.

Validation passed: native/CNG and portable group-device recovery/security tests,
actual dispatcher/managed projection integration, native group registration,
full Windows build/package/import audit, Android JVM/debug/test/release/lint,
ARM64/JNI/dependency audit, and portable-server parity including pinned-TLS
mailbox sender-disconnect durability, restart, quotas, revocation and presence.
Development version/update extraction audit passed dev.121 -> dev.122. The
legacy test-discovery helper could not compile its stale incomplete source list;
it was not rewritten and is not claimed green. Current comprehensive server
portability tests compile the complete production server and passed instead.

Preserved DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.122.zip SHA-256:
bb634a6b03d08b275937bb74eb401115f30bb80500f9e185bd1e41239c2e6cd2
Android debug APK SHA-256:
b593398e0b79384d1832f97c54f18030f800b3bbfa2be0e5eb080debfd78747f
Android unsigned release APK SHA-256:
2c6c2814b04c66b40237e925055256974de3cb0879c448491a12829257b9f02e
Unsigned Development artifacts only. No physical group acceptance claimed.

## Additional production transport regression at dev.124

tests/test-group-ciphertext-fanout.ps1 now exercises the public
GetGroupDeviceRecovery method over actual pinned TLS to the isolated production
DiscoveryServer, with three authenticated live event-channel device sessions.
The current cursor returns an empty complete page; ordinary membership epochs
without signed device-transition evidence return signed_ledger_gap without
advancing the cursor. Wrong-account device, future cursor and unrelated-group
requests are rejected. Existing three-client ciphertext fanout, receipts,
duplicate suppression, offline exclusion, reconnect and removal checks passed.
Evidence: build/tests/group-fanout-e9dd598afca14236b5e676c18eb37438.

This test-only checkpoint does not change the dev.124 runtime/package, install
anything on the phone, activate enrollment or treat a TLS-authenticated roster
as cryptographic approval. Native root/transition verification remains required.
