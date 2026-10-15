# Android dev.161 — physical routing and background wake findings

2026-10-15. Read-only investigation following physical reenrollment. Branch `codex/android-groups-voice`, source HEAD `345826aea06d20ebaafefdca57bf7ac7267f3dd1`; no application source, routing, server registration, ciphertext or ACK was changed. No new build/test/deployment was necessary. Main Work remains untouched.

## Physical result superseding the prior acceptance status

The owner confirmed reenrollment succeeded and both identified offline messages arrived after opening Android. The failure is failure to wake/service while backgrounded, not failure to retrieve these messages after reopening. Sender-disconnected timing, restart repetition and unattended wake acceptance have not independently been established by this investigation.

Read-only Pi snapshot: 2026-10-15 13:22–13:25 UTC, database `/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db`, SQLite URI mode=ro, query_only, read transaction and rollback. No mailbox fetch API was invoked (it may purge expired rows); no payloads were decrypted or acknowledged by the investigator. Local evidence: `outputs/android-dev161-routing/pi-read-only-routing.json` in the session deliverables.

## Account 4 devices and recipient authority

| Device | Permanent identity | Active enrollment | State |
|---|---|---|---|
| Windows PHONE profile, server device row 4 | `35049f3bf26f1d01a7e6c2d0dad2132a646093008d0c2f23f96fa32aa418201b` | `59c487f3b7c746dfa93e9ebdb00b30b7`, generation 2 | active, allowed=1 |
| Motorola, server device row 5 | `61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b` | `67ef5d94e35745df9aa7cbe227dad081`, generation 1 | active, allowed=1 |

These are the only two devices registered to account 4 JonahPhone in this snapshot. Windows also retains retired generation 1 `adf19a928e1b40bf998f97ce086db3be`, allowed=0. Neither presence SQL row exists at inspection; this does not measure live in-memory server presence.

Motorola's signed binding metadata names account 4, its exact permanent identity, current enrollment ID/generation and storage permission. Public agreement-key SHA256: `db15fce811c7ab8287dfeff3e5536bb1bdf9cb4a58037fa8405e91faa186f91d`. Canonical length and 64-byte signature shape match. Server RegisterMailboxKey checks session ownership and identity signature; the approved native enrollment path reuses the existing opaque key and checks its original binding/public metadata and HPKE possession before creating this binding. This is consistent with the same-key reenrollment design. We did not extract a phone private key or independently export the historical phone public key for a second byte comparison.

## Exact destinations of the owner's tests

Sender: Sammy account 5, device `0f1ae8dffc8069987b7d772e5f6f8398ed17ea4306f47c78453636e0a0347391`.

| Message ID | Motorola copy | Windows copy |
|---|---|---|
| `46ffb70a5228432b98580d8ce285aecd` | stored 08:16:03.511 Chicago, key 67ef… gen1; signed receipt retained, envelope cleared | stored 08:16:03.496, key 59c4… gen2; ciphertext pending |
| `23173107bae44a1aa509eded5beb34c9` | stored 08:16:54.862 Chicago, key 67ef… gen1; signed receipt retained, envelope cleared | stored 08:16:54.852, key 59c4… gen2; ciphertext pending |

Each Motorola ACK was parsed read-only: account 4, exact Motorola device, Sammy sender, message ID and envelope digest match its row; signature length 64 and canonical end match. The production server verifies recipient identity signatures before retaining ACKs. No independent signature verification using an exported phone identity public key was added to this read-only inspection.

Totals: Windows 53 retained records, 7 pending/46 ACKed; Motorola 2 records, both ACKed/zero pending. The former five Windows pending messages remain Windows messages; the two new Windows copies bring that count to seven. A zero pending count for Motorola is expected after successful ACK, not proof it was never targeted.

The supplied dev.161 report independently shows both IDs in Sammy's native history, delivery=2, addressed to the exact Motorola identity. It shows `native_receive_stage=durable_receipt_ready`, received account 4/current key, and a later empty fetch (`fetched=0`). The owner confirms they appeared after reopening. Together these establish fetch, decrypt, durable import, display and ACK for these messages. `recipient_relation=not_in_account_display_index` on incoming rows merely compares the recipient (local phone) with the remote contact's device list; it is not a server routing error.

## Existing routing behavior — no policy change required for these tests

Read-only code inspection included the latest Main Work native Windows mailbox implementation; operation 8 remains account fanout. Desktop MailboxExecution requests mailbox_keys for the recipient account, packages the returned bindings, invokes native operation 8, then uploads separately prepared envelopes. Server MailboxKeys advertises all owned active storage-allowed devices, not just one, ordered by device. WindowsMailbox checks each device has an existing verified native identity pin and authenticated account mapping, then independently encrypts/persists an envelope per eligible device. It does not bypass device verification.

Ordinary offline friend-profile resolution can name the oldest registered device (Windows row 4) when no live device overrides it. That single display/canonical identity is not the mailbox fanout list. The two real records per logical message demonstrate that Motorola was included. There is no evidence of a stale mapping excluding Motorola for these tests and no need to implement Windows-first/Android-fallback.

Existing ciphertext remains immutable per sender/message/device. Server StoreMailbox validates the signed destination and key; FetchMailbox filters exact authenticated recipient account/device. Each ACK belongs to one destination/digest. Windows' outstanding copies cannot be given to Motorola or canceled merely because Motorola ACKed.

## Concrete background wake blocker

The dev.161 package's generated BuildConfig has `VERSION_NAME=4.0.2-dev.161` and **FCM_CONFIGURED=false**. This corresponds to the delivered APK SHA256 `33d2b38be76ec7d0cfe8de075ff50920bc318255c7e07bdbb47d789e9b9c7443` and its recorded source/build evidence. The build enables this gate only when the standard Android google-services.json config is present and matches com.peerchat.android.

PeerChatAndroidApplication.onCreate returns before Firebase initialization/token acquisition when this flag is false. PeerChatFcmService.onNewToken and onMessageReceived likewise reject processing under this gate. Thus this candidate cannot use its intended FCM wake path, regardless of battery/notification permissions. A preexisting encrypted token survives an update and may still be registered; a token row does not establish that the currently installed APK handles push callbacks.

Pi configuration has FcmEnabled=true, configured project and credential-path fields. Motorola has an FCM token row (142 characters, unexpired at inspection), and remembered-device authorization exists. Token contents and service credentials were not printed/read. These metadata checks do not establish actual provider acceptance or token/project correctness.

The existing server mailbox_store path already invokes an opaque push hint after durable storage. The Android handler is intended to schedule a bounded JobScheduler worker, restore the existing remembered authenticated profile, fetch/import/ACK, then issue notifications. It is not a permanent background socket. With the package gate false, the hint cannot enter that path. This is a demonstrated configuration blocker; further vendor delivery, OS scheduling, remembered-session and notification issues may remain once configured.

## Remaining diagnostic error is separate

The report still says `stage=mailbox_keys result=IllegalStateException state=rejected_or_storage_unavailable`. In MailboxExecution, mailbox_keys is the later OUTGOING recipient-key lookup, after incoming fetch and signed ACK processing. It is not native_recipient_registration or the old account19 binding validation. This generic result does not identify which candidate recipient account was queried or the exact server rejection. Do not infer local key loss, failed recovery, or permission to reset anything. A separate focused diagnostic must identify the candidate account/nonsecret server state if this error persists; no repair is claimed for it. History Sync's platform-relay failure is also independent and was not changed.

## Next safe work and physical acceptance

No routing changes, new APK, push registration edits, Pi configuration changes or deployment were performed during this investigation. Existing keys/trust/history/server ciphertext and ACKs are preserved.

The next focused repair should reuse the existing FCM adapter and bounded worker with the correct public Android Firebase application configuration for the Pi's configured project. Review the project/package/signer association, preserve the same application ID/signing certificate and data, and produce a package that demonstrably has FCM_CONFIGURED=true. Do not include server service-account secrets in the APK. First inspect any existing approved configuration before requesting new project setup. Server changes or registration-policy changes still require authorization. No replacement messaging engine or routing policy is needed.

After a configured candidate is approved, distinguish app-backgrounded/Home + screen locked, swiped away, and Android Settings Force stop. Record nonsecret fcm_received, wake_schedule_accepted, wake_job_started, remembered authentication, fetched/imported/ACKed, and notification result stages. Do not attribute every missing notification to routing. Check app notification permission/channel and Android background restrictions only alongside these actual stages.

Repeat one uniquely labeled test: Windows PHONE profile closed; Android backgrounded; Sammy sends and confirms Stored; capture both immutable destinations by read-only snapshot; close Sammy fully; wait for wake/job/notification and Motorola signed ACK without opening the app. Then reopen Android to verify exactly one persistent logical row, restart/dedupe, and Sammy's receipt reconciliation. Record elapsed times and both device ACKs separately. Previously successful reopen delivery is a different accepted checkpoint from unattended wake delivery.

If future product changes add explicit device targeting, freeze target identities per new logical send, seal independently for each verified authorized target, and define whether Delivered means any or all requested targets. Persist per-device upload/ACK state and dedupe logical IDs across authorized History Sync imports; never retarget accepted immutable envelopes or treat one device ACK as another's. This is a recommendation for review, not an implemented policy change.
