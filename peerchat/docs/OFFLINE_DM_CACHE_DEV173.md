# Offline DM encrypted cache — dev.173 validation report

## Scope and status

Branch: `codex/native-unicode-emoji`, existing native-unicode-emoji worktree.
Baseline: dev.172 (`e242b50`). Physical acceptance of this candidate is pending.
Production source checkpoint: `bdd28e57075a578864f7401d12c9b38279499088`.
Automated gates and package audits passed. The authorized Pi update from dev.166
to dev.173 completed; Windows/Android physical acceptance remains pending.

The implementation adds offline DM attachments to the existing encrypted cache,
not a replacement transfer engine. The individual plaintext limit remains
256 MiB. Existing group attachments, clipboard screenshots, emojis, media,
message identities, history and transfer journals remain in scope for preservation.

## Implementation and security boundaries

- Authenticated, signed capability registration binds existing device keys to the
  new attachment scope. Every recipient in the frozen audience needs a compatible
  registered key and verified identity; missing keys never imply automatic trust.
- Public server metadata contains signed routing/size/ciphertext-digest evidence,
  not private filenames, plaintext hashes or decryption keys. Existing per-device
  HPKE carries the private attachment body. Friendship incarnation, device consent,
  revocation and source/recipient authorization are rechecked.
- The cache reuses the existing quota, filesystem reserve, configurable directory,
  48-hour default retention, lease and crash-recovery machinery. DM reads and
  progress reject expired objects even if a lease predates expiry; release remains
  allowed. Ordinary shipping server builds keep the new scope gated off.
- Native TransferManager owns ciphertext hashing, chunks, resume and durability.
  Private recovery records save the original selected source, protected stage,
  per-job state and exact server lease before dependent work. Atomic index updates
  and immutable bounded history pages do not alter existing message/journal schemas.
- Lost append/completion responses reuse the original owner lease. They never
  suppress duplicate-owner rejection or evict another active owner.
- Plaintext finalization verifies the complete body before recording completion.
  A previously verified final is compared on replay, never overwritten as a partial.
  Verified internal copies survive export failures; the existing selected-output
  and Retry Save workflow remains responsible for user-visible export.
- Local retained-history reads are scoped read-only projections, not download
  grants. Supplemental cache corruption fails this feature closed without erasing
  evidence or disabling ordinary messaging/history/transfer journals.
- A verified-file receipt is separate from text-mailbox ACKs. Publication of a
  durably stored private notice reuses the existing mailbox-ready/push hint; it
  does not claim receipt or verification by the recipient.
- Android Send captures a bounded provisional row and clears the editor immediately.
  Each send saves independently; Queued is displayed only after durable native save.

## Protocol audit

See `TRANSFER_PROTOCOL_AUDIT_DM_CACHE.md` for the field-by-field audit.

Direct Windows/Android peer transfers retain negotiated 32/64 KiB chunks and
1/4/8/16 windows, with legacy 32 KiB × 1 fallback. Windows is uncapped by default;
Android retains its configurable bandwidth settings. Media-aware backpressure is
preserved. The opaque Pi relay does not parse TransferManager file frames.

The cache is a distinct bounded authenticated JSON/blob RPC: at most 64 KiB data
per request, serialized durable acknowledgements and independent recipient offsets.
Local TransferManager v2 framing does not imply 16 parallel cache network requests.
Existing group cache downloads now negotiate local 64 KiB × 1 instead of performing
32 KiB reads. No peer format or cache protocol is relabeled or forcibly replaced.
Base64/JSON, serialized RPC and durable SQLite/filesystem work remain measurable
costs. Host fixtures are not physical Pi, Android or LAN throughput measurements.

## Completed checkpoints

| Commit | Change |
| --- | --- |
| `8607fbe` | Independent Android provisional sends and immediate composer clearing |
| `444d340` | Default-gated authenticated DM cache server scope |
| `f72798c` | Prior-shipping-assembly rollback compatibility and coherent cache backups |
| `8af672a` | Protected source/staging/lease recovery and bounded history persistence |
| `f961188` | Enforce retention on existing DM leases while permitting cleanup |
| `369f37c` | Existing group cache receiver uses negotiated local 64 KiB reads |
| `59e085a` | Exact-transfer-ID concurrent media recovery assertions |
| `686b376` | Permanent cache failures preserve verified copies and stop retries |
| `a5c5dad` | Purpose-scoped Windows/native Android adapters and local history |
| `84cf473` | Durable DM publication reuses existing mailbox wake hints |
| `6740875` | Client RPC execution, retained history and truthful unavailable rates |
| `ef95c8e` | Real Windows/JNI two-device cache, mailbox wake and recovery fixtures |
| `82abe0a` | Purpose-2-only durable local engine retirement with protected-file retention |

Final rebuild, package audit and deployment results follow below.

## Automated evidence available

- `build/dm-cache-admission-final.log`: signed admission, two-device audience,
  source/recipient revocation, retired-key distinction, cross-account rejection,
  friendship replacement, independent durable progress and verified receipts.
- `build/dm-cache-runtime-regressions.log`: quota, reservations, crash recovery,
  filesystem safety and existing group authorization.
- `build/dm-cache-policy-regressions.log`: configurable capacity, reserve/mount
  checks and 64-bit policy accounting, including capacities above 10 GiB.
- `build/dm-cache-expiry-legacy-regressions.log`: expiry across an active lease and
  actual dev.166 server assembly opening the new cache without losing unexpired
  ciphertext or offsets. This is isolated Windows execution, not ARM execution.
- `build/dm-cache-mailbox-regressions.log` and
  `build/dm-cache-history-overlap.log`: mailbox security and overlapping history
  import/deduplication without false live ACKs.
- `build/dm-cache-deployment-helper-tests.log`: coherent stopped-cache backup,
  hash/integrity verification and insufficient-backup-space rejection.
- `build/tests/dm-attachment-body-84061050f5954c4db4342c416fc47f9d`:
  actual protected storage, HPKE/controller restart, lease reuse, verified-copy
  preservation, immutable paging, old WIP reader compatibility and bounded work.
- `F:/PeerChat-isolated-cache-tests/dm-application-7d69dbfff8824c1fb270ed241a962be9/results.log`:
  real Windows Application/pinned-TLS candidate, sender offline, two independent
  recipients, lost replies, server/client restart, interrupted finalization,
  verified-final comparison and reverse-direction receipt aggregation.
- Android JVM suite: 309 tests passed; lint: zero errors and 17 warnings. The
  final native rebuild includes engine retirement; signing identity remains the
  accepted `db65bb23…d4141` certificate.
- `build/tests/dm-attachment-body-95115ae55a7d4a7f9768f7d2fc1d0224`:
  40 permanent failures free controller work slots; transient quota retries;
  failed output callbacks retain verified data and terminal errors after restart.
- `F:/PeerChat-isolated-cache-tests/dm-application-b5794fed7f044b88b843eb9954b2ec6d/results.log`:
  fresh candidate publication wakes both real authenticated event subscribers;
  invalid-signature publication is rejected with no hint. The full two-direction
  recovery and verified receipt fixture also passed.
- `build/tests/android-jni-dm-recovery-4/results-*.log`: real production JNI,
  native Windows adapters, existing HPKE and authenticated pinned-TLS cache in
  both directions; secondary unpinned device rejection, source/download restart,
  lost append response and exact Unicode/binary payload verification. Windows-host
  CNG/DPAPI/SQLite shims are used, not physical Android Keystore/POSIX execution.
  The enhanced run `results-cf02ea31473646d593381bc9f4a4480f.log` also verifies
  ordinary encrypted-mailbox dispatch, exact native signed durable ACK, replay
  deduplication during cache upload, and protected completed-copy history after
  reopening without a session/friend/mailbox provider.
- `build/tests/media-transfer-complete-dependencies-review.log`: real H.264,
  presentation, text coexistence, verified 10 MiB transfer, media shutdown and
  authenticated relay reconnection passed. Earlier failure was a missing fixture
  ScreenMedia DLL, not a production route regression.
- `build/dev173-chat-attachment-regressions-final3.log` and
  `build/dev173-clipboard-regressions-final3.log`: attachment projection, explicit
  text ACK isolation, 608 retained rows, bounded Unicode previews and clipboard
  image/text precedence passed against the candidate Desktop assembly.

## Final release gates and artifacts

1. Post-admission expiry is corrected and independently reviewed. Final evidence:
   `build/tests/dm-attachment-body-00e2a75e44f34e87accdb9c34ec10cb9`. Eight admitted
   expirations and restart cycles leave capacity for a new usable transfer;
   partials remain retained, no Cancel is sent, failed saves roll back, ordinary
   peer/purpose mismatches reject and completed transfers stay complete. A local
   bounded reconcile also covers the earlier unshipped WIP state after restart.
2. Rebuilt JNI/TLS passed after the final shared correction:
   `build/tests/android-jni-dm-final-173/results-b67335c215d9451290e02c1e9dfb9040.log`.
   Android freshly reran 309 tests (72 suites), lint had zero errors/17 warnings,
   and the final debug APK signature matches the existing installation identity.
3. Full Windows client/updater/media/server/admin compilation and native dependency
   audits passed: `build/dev173-complete-final-build.log`. Final package version,
   APK signature/ABI, source ancestry and every ZIP payload byte passed:
   `build/dev173-package-audit.log`.
4. The self-contained ARM32 package passed architecture/runtime/version/hash
   audits and executed `--version` with a successful loader check on the actual Pi.
5. Coherent backups and controlled restart completed successfully. Details below.

Artifacts under `build/development/4.0.2-dev.173`:

| Artifact | SHA-256 |
| --- | --- |
| `PeerChat-Windows-4.0.2-dev.173.zip` | `9bbe13882d762010951b27c5e36964e443c57011fd35b39059c6300440d5a5cf` |
| `Android/PeerChat-Android-4.0.2-dev.173-debug.apk` | `168fe82276553a2095bc5b4854f7d3d40864576bdf636b22270f4c43e93d840f` |

The runnable Windows executable is `Package/PeerChat.Desktop.exe` within that
version directory. The Pi package is
`build/linux/4.0.2-dev.173/PeerChat.Server-4.0.2-dev.173-linux-arm.zip`, SHA-256
`cf49d8615aa86c54c48592a55f0c1259367e80095204e1cf5f8546453b87ca40`.
`messaging-repair-package-audit.json` records exact absolute paths, sizes and hashes.
Its packaging-time `piDeployed=false` means the packaging operation itself did not
deploy anything; the separately authorized subsequent deployment is below.

No phone installation is authorized by this packaging workflow. Physical tests
are performed by the user after a replacement-installable APK is provided.

Known Android presentation limitation: the inline text-preview action for older
cached text attachments may require a current authenticated context after an
offline reopen. The verified internal file and existing export action remain
available. This does not affect ordinary text messages or discard attachment
content. No extra native preview API is introduced in this milestone.

## Read-only Pi readiness

At the last read-only inspection the Pi runs dev.166 on ARMv7l, PID 19692, under
`/opt/peerchat-test`. Its ext4 filesystem has 125,421,432,832 bytes total and
111,489,576,960 available, with about 3.97 GB RAM. Existing configured cache is
10 GiB with 48-hour retention. These are inspected values, not assumed SD capacity.
The deployment helper preserves current certificate/credentials/TURN references,
identity/grant content, database and cache and verifies staged runtime hashes.
It never restores an older database over newer writes automatically.

## Approved Pi deployment result

- Previous: `4.0.2-dev.166`; deployed: `4.0.2-dev.173`, actual running PID 5939.
- Rollback: `/opt/peerchat-test/rollback/pre-4.0.2-dev.173-20261017T002445Z`.
- Binary/config/private referenced files were copied and hash-verified. Online
  and stopped account SQLite snapshots passed integrity checks. The stopped cache
  snapshot and complete cache tree (25,497,632 bytes, three files) passed hash and
  SQLite integrity verification. Existing identity/grant content was compared
  before/after and preserved; no old database or cache was restored.
- Pinned TLS, existing isolated validation-account authentication, discovery,
  unchanged configuration, cache instance/filesystem binding and free-space
  reserve passed. The live filesystem retained 111,249,252,352 available bytes.
- Actual encrypted mailbox/relay/cache correctness was exercised in isolated
  authenticated host fixtures. Live phone import/download/throughput acceptance
  still requires the user's tests; no new public test listener was exposed.
- Evidence: `build/dev173-pi-deployment.log` and
  `build/dev173-pi-deployment-manifest.json`. No Android installation occurred.

Prior accepted packages and all preliminary candidate outputs were preserved,
not overwritten. No branch merge, publication or unrelated roadmap work occurred.

## Manual acceptance checklist after all gates pass

- Update Windows and Android without clearing data; verify identities/history.
- Send files and images each direction, close the sender after cache upload, then
  download on the recipient. Verify the exact saved bytes and chosen filename.
- With two approved recipient devices, confirm independent download/retry/receipts.
- Interrupt and restart uploads/downloads; verify resume and no duplicate messages.
- Verify normal DM text remains responsive, Android composer clears immediately,
  and a second message can be typed while the first saves.
- Recheck accepted direct v2 transfers, group attachments and clipboard screenshots.
- Keep physical throughput and Android/Pi acceptance separate from host test results.
