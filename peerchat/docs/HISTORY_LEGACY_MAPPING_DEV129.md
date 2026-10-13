# Physical archive mapping boundary — dev.129

## Proven boundary

The Motorola reports valid current account-4 signed grants, an authenticated
winning archive route, and `last_source_rejection_stage=source_record_device_mapping`.
This is a source-side record admission failure, not proof of a current relay
duplicate, missing grant, or receiver data rejection. A prior relay-close event
does not override the current connected winner.

The source checks both original devices against retained authenticated account
bindings before creating a record attestation. Failure closes archive work; no
cursor may advance past an unattested record. Suppressing relay errors cannot
repair missing original-device evidence.

## Read-only physical evidence (2026-10-13)

An opt-in native SQLite metadata probe opened the existing normal profile database
read-only. It queried only public conversation device references, row counts and
binding-object existence; no encrypted BLOB, message content, credential or key
was read. The database contains 369 rows across eight public device references.
Four lack a historical device-account binding:

| Public device reference | Indexed rows |
| --- | ---: |
| 2c2b3ff1a09038a94c1f09bc93f225124951e8a6a901810240e0683365404866 | 3 |
| 3cad98973ab84da8bcf18183f78e197a42b96d60a8c0cae6c09aa4a9f80dfbae | 3 |
| 4dfee277c24245a26e14a2dff6d962dcce1174a7330bf34f65c824133d231833 | 129 |
| 7927a46cbe1c751cc65c7d041818ffa6efa35ada65af0a1b7fa5fdfbad7162af | 101 |

These are index counts, not a decrypted-record survey or proof that every record
belongs to one old realm. The exact first failing record was not read.

A separate opt-in instrumentation probe loaded the protected friend-history index
for the current saved endpoint, certificate pin and account. None of these four
references has a retained binding there. The probe asserted that the existing
public signing identity remained byte-identical. `OK (1 test)`; all four safe
`HISTORY_BINDING_PROBE` outputs contained an empty `current_pin_accounts` list.
The Pi's current `devices` directory also contains none of these four references
(read-only exact public-identity lookup, no account/credential reset).

The previously checkpointed MAILBOX_SENDER_SESSION_COUPLING_TRACE.md documents
7927a46cbe1c under old sender account 8 and this same Motorola identity under
recipient account 19. Current History Sync uses account 4. This proves that at
least one missing reference has historical account context different from the
current one; it does not authorize substituting current numeric account IDs.

## Tools and validation

- tests/android_history_routing_probe.c: standalone read-only public-index probe,
  built with existing NDK ARM64 clang, C11/O2/Wall/Wextra/Werror, and the device's
  existing SQLite library. No application dependency or production packaging change.
- HistoricalBindingProbeTest: explicit history_binding_probe=true opt-in;
  existing storage/identity aliases required; no login, transport, history writes,
  grant issuance, binding writes or key creation. Only test APK replacement-installed.
- assembleDebugAndroidTest offline passed. Physical protected-index probe passed;
  main application reopened afterward. Production APK remains dev.129.
- Existing dev.129 validation remains recorded in HISTORY_SOURCE_DIAGNOSTICS_DEV129.md.
  No new production correction or new physical acceptance is claimed here.

## Security boundary / next decision

Do not seed account mappings from this document, plaintext, nicknames, a source's
claims, current account-number coincidence, or an old certificate namespace.
Do not silently skip records, advance cursors, fabricate ownership, delete rows,
reissue grants, or weaken source/reader verification to report completion.

Current grant/route repairs do not provide an authenticated cross-server account
migration. Recovery of old-server records needs an explicitly approved migration
policy retaining original server/account provenance and verifying the relevant
historical evidence. Whether all four references have recoverable evidence remains
unproven. Preserve the records pending that decision.

Windows/Android full History Sync physical acceptance remains FAILED/PENDING.
Pi stays dev.125: this diagnostic checkpoint contains no server-side change to deploy.
No identities, histories, grants, trusted pins or production behavior were changed.
