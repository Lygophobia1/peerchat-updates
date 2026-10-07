# Go HPKE production integration

## 1. Architecture and scope

Integration starts at dev.17 `7e41f0a8119790564563ef98bf7ec5d200ff6952`
on isolated `codex/go-hpke-production`. The earlier isolated proof is
`1c8c014d3de4143ee1499445f351c9b298b5ee03`; it is not a runtime dependency.

`WindowsMailbox -> HpkeProvider -> Go crypto/hpke -> opaque KeyExchanger ->
authorized RecipientKeyHandle -> Windows CNG`.
KeyManager and the protected registry retain lifecycle/authorization authority.
Go cannot locate/open an alias. SessionTransportManager and DeliveryManager,
server schema, mailbox wire and Android production source are unchanged.

## 2. Pinned build input

Go **1.26.8 windows/amd64** archive:
`https://go.dev/dl/go1.26.8.windows-amd64.zip`.
SHA-256 `B92C3B2ADAE85A11BA71FE7216DAF0D84E82AF4C8AB6C5625807F28622043A59`.
`scripts/build-go-hpke.ps1` acquires or copies this exact archive into ignored
`build/go-toolchain`, verifies its hash and extracted compiler version, and uses
`GOTOOLCHAIN=local`. An existing approved archive may be supplied through
`PEERCHAT_GO_TOOLCHAIN_ARCHIVE`. Cached extracted tools are trusted local build
inputs, not a defense against a malicious administrator modifying the cache.
The DLL is built with `-trimpath -buildmode=c-shared -ldflags='-s -w'` and TDM GCC.
No Go installation is needed on recipient PCs. No toolchain ships.

## 3. C ABI

`platform/go_hpke/abi.h` and `bridge.h` define the synchronous borrowed-buffer
interface: `pc_go_hpke_ping`, `pc_go_hpke_seal`, `pc_go_hpke_open`.
Version probe returns 12608. Errors: 0 success, 1 invalid arguments, 2 crypto or
capability rejection, 3 insufficient capacity, 4 contained Go panic.
Lengths start at zero. Valid bounded output buffers are cleared before use;
plaintext is copied only after authentication. Inputs/outputs must be valid,
nonoverlapping caller-owned memory for the entire call. Invalid native addresses
are not safely recoverable. Limits: plaintext 65,536, ciphertext 65,552,
metadata 8,192, SEC1 public/encapsulated points 65 bytes.
No Go pointer or capability survives its synchronous operation.

## 4. Opaque CNG path

The adapter borrows the already-authorized `shared_ptr<RecipientKeyHandle>` and
checks its public point against the expected point before and inside agreement.
Go validates the remote P-256 point; the existing CNG provider also validates it.
CNG alone performs raw ECDH. Its existing single little-to-big-endian conversion
is unchanged; Go consumes the returned 32 big-endian bytes without another
reversal/KDF. No private scalar or exported identity crosses this boundary.

## 5. Existing-key continuity

No key-manager, alias, registry, binding or rotation implementation changed.
Test-only CNG keys demonstrate non-exportability, persisted reopen and unchanged
public identity. Production application key-ID/generation/public-point/signed-
binding continuity still requires recording the existing Windows and Motorola
public records during the pending physical test. No normal user key was created,
deleted, replaced or re-registered in this integration run.

## 6. Protocol

RFC 9180 Base mode 0; DHKEM(P-256, HKDF-SHA256) 0x0010,
HKDF-SHA256 0x0001, AES-128-GCM 0x0001. Go owns DHKEM, labeled KDF,
key schedule, nonce construction, AEAD and context sequence behavior.
Each envelope has a fresh context. `mailbox_protocol.hpp` is unchanged, including
the terminating NUL in `PeerChat/mailbox-HPKE/Base/v1`, canonical header AAD,
SEC1 encapsulation, recipient key IDs/generations and sender signatures.

## 7. Transient-secret policy

PeerChat clears its raw DH and failed plaintext buffers; Go clears directly owned
DH and successful temporary plaintext buffers on return. Internal Go HPKE/AES
state and GC copies have no deterministic erasure guarantee. No memory locking,
pagefile/dump immunity, complete heap zeroization or independent certification
is claimed. This limitation was explicitly accepted for this integration.

## 8. Runtime lifetime and concurrency

The DLL is loaded once from the containing native module's directory with
restricted DLL/system search and is never FreeLibrary'd. The embedded Go
scheduler/GC/threads remain until process exit. Provider shutdown closes new
admission and drains admitted operations. Call-scoped capabilities are released
normally. A per-provider mutex protects only admission counters; there is no
process-global crypto executor or serialization of independent HPKE calls.

## 9. Automated interoperability

`tests/test-hpke-provider.ps1`: official Go RFC 9180 suite vectors, public API
baseline, opaque serialization rejection, Go/CNG negative tests and unchanged
Android `OpaqueRecipientHpke` host adapter, 100 each direction. Native code emits
the exact production mailbox info/AAD/body bytes to the host oracle. This host
adapter uses a synthetic software ECDH capability, NOT physical Keystore proof.
`tests/test-mailbox-protocol.ps1` additionally checks native/managed canonical
wire, sender signatures, protected storage and signed ACKs using Go.

## 10. Physical Android status

ADB inventory during this pass: **no attached device**. No installation, data
clear, identity change or instrumentation run occurred. Real existing Motorola
Keystore <-> existing Windows CNG interoperability is PENDING. Android production
BC/Keystore code is unchanged; only the old provider-specific device-test name
was made provider-neutral. Replacement installation only when the phone returns.

## 11. Mailbox acceptance

Host mailbox protocol/server regressions PASS: signed identity-bound registration,
blind durable store/fetch/signed ACK, duplicate store/fetch/ACK, wrong-device and
pin rejection, restart persistence, receipt reconciliation, quotas/TTL, rotation,
revocation/device removal, permission rejection and independent multi-device
uploads. The pinned-TLS harness fetches/ACKs with sender signed out and unreachable;
recipient cleanup precedes sender receipt reconciliation.
This is not a claim of physical FCM/locked/offline-sender acceptance.
The known dev.17 history/notification P0 remains outside this change.

## 12. Stress/resources

Eight concurrent callers/two CNG keys, 12 rounds of 1,000 seal/open round trips,
plus wrong-key/tag failure mix. After lazy runtime warmup, the last three rounds
retain the original <=4 handle-growth assertion. Recorded complete run:
221 -> 256 -> 257, then 257 across the remaining rounds. An earlier short-warmup
run grew by six handles in round three; it was NOT accepted as a passing leak test.
Warmup was extended with the same concurrent workload rather than relaxing the
steady-state assertion. These are process handle checks, not proof of erasure or
absence of every possible heap leak. Repeated provider lifetimes and gated-DH
shutdown are covered. Raw ABI checks cover stale plaintext after failed auth,
missing capabilities and short capacity.

## 13. Build and reproduction

Normal command: `$env:PEERCHAT_DEPENDENCY_ROOT='C:\Peerchatdep\deps'; .\build.ps1 -All`.
The normal build stages `PeerChat.Hpke.dll` and its notices beside Core in Client
and Package; it does not require the removed evaluation switch or old cache.
Immutable revision allocation uses the source revision as its minimum and scans
existing development/evaluation output directories without overwriting them.
Initial integration full Windows build/package audit: PASS, **4.0.2-dev.18**.
Android uses the same source version; no Android HPKE implementation translation.

## 14. License/dependency inventory

The runtime bundle carries Go BSD-3-Clause, vendored golang.org/x/crypto and x/sys
BSD notices, Fiat/generated arithmetic notice, MinGW-w64 runtime notice, GCC
license and GCC Runtime Library Exception 3.1. These are actual pinned-toolchain
notice files copied by the build, not invented license summaries. The proof
identified x/crypto `v0.46.1-0.20251210140736-7dacc380ba00` and x/sys `v0.39.0`.
Go runtime/scheduler/GC and used standard/vendor code are embedded in the DLL.
Static PE imports are KERNEL32.dll and msvcrt.dll; Windows RNG may load system
bcryptprimitives.dll/ntdll.dll. Native Core retains its existing CNG system imports.
No JVM, external Go runtime or removed crypto library ships. This is a notice
inventory, not legal certification or a release-readiness statement.

## 15. Historical wolfCrypt removal inventory

Every location below was an old evaluation artifact; none remains an available
provider. Rows marked HISTORICAL ONLY retain provenance, not executable instructions
for the current build.

| Location | Former purpose | Disposition | Replacement |
| --- | --- | --- | --- |
| platform/wolfcrypt_evaluation_hpke.cpp | HPKE/CNG callbacks, global executor/init/shutdown | REMOVED | platform/go_hpke_provider.cpp + Go standard library |
| platform/hpke_provider.hpp | evaluation factory and disabled shipping factory | REMOVED | one production factory |
| windows_mailbox.hpp | provider-selection branch | REMOVED | production factory only |
| ui_bridge.cpp | evaluation-only mailbox support gate | REMOVED | normal mailbox support |
| build.ps1 | HpkeEvaluation switch, defines, includes, libwolfssl.a, archive/library pins, evaluation output/manifest/license copy | REMOVED | pinned Go build, normal development output and notices |
| build/deps/wolfcrypt-evaluation | ignored source ZIP/extracted headers/static library/native cache | NOT CREATED in integration tree; old proof/reference caches untouched | ignored pinned Go cache |
| scripts/publish-update.ps1 | old evaluation marker/manifest rejection branches | REMOVED | unchanged normal updater security gates; normal package audit |
| tests/test-wolfcrypt-cng-evaluation.ps1 | source download/CMake build and oracle runner | REMOVED | test-hpke-provider.ps1 |
| tests/wolfcrypt_cng_evaluation.cpp | old oracle/callback/KDF stress fixture | REMOVED | Go/RFC/CNG/BC tests |
| tests/tls-host/WolfCryptCngEvaluation.java | old independent host oracle driver | REMOVED | BcGoInterop.java with actual Android adapter |
| tests/hpke_provider_test.cpp | evaluation factory, obsolete shipping rejection and serialized shutdown expectation | REMOVED | production factory, concurrent/draining lifetime tests |
| tests/test-hpke-provider.ps1 | old library link/cache/oracle dependency | REMOVED | Go DLL build and BC/RFC validation |
| tests/mailbox_protocol_test.cpp | old provider factory | REMOVED | production factory, same protocol assertions |
| tests/test-mailbox-protocol.ps1 | old defines/header/library/cache dependency | REMOVED | production Go adapter/DLL |
| tests/hpke_device_interop_test.cpp | old factory/result label | REMOVED | production provider, same test-only exchange |
| tests/test-hpke-physical-interop.ps1 | old source/library/cache link | REMOVED | production Go adapter/DLL |
| Android HpkeInteropDeviceTest method label | old provider-specific name | REMOVED | provider-neutral method; same physical test |
| tests/test-hpke-package-containment.ps1 | evaluation marker fixtures | REMOVED | normal Go ABI/import/notices/runtime-artifact audit |
| HPKE-EVALUATION.json / wolfCrypt-GPLv3-EVALUATION.txt | generated evaluation package artifacts | NOT GENERATED | normal runtime plus Go notices |
| docs/WINDOWS_NATIVE_HPKE_EVALUATION.md | old evaluation/library selection evidence | HISTORICAL ONLY | this document |
| docs/MAILBOX_EVALUATION_RUNTIME.md | old runtime/licensing description | HISTORICAL ONLY | this document |
| docs/DEV17_CHECKPOINT_SCOPE.md | old checkpoint file/command provenance | HISTORICAL ONLY | this document |
| docs/ANDROID_LOCAL_HISTORY_MAILBOX_ORDERING.md | dev.17 evaluation/physical failure evidence | HISTORICAL ONLY | physical acceptance remains pending |

## 16. Package audit

Normal `build/development/4.0.2-dev.18/Package` passed required runtime/import
audits, including HPKE's exact three exports, x64 PE, system-only imports, bundled
notices and absence of object/header/archive/test/debug artifacts. Generated MSVC
objects outside the package are not source and are excluded from commits.
Final recursive source/package search must classify every remaining old-provider
match as historical documentation; any active dependency is unacceptable.
Broad case-insensitive `wc_` also matches the unrelated Windows SDK
`WC_ERR_INVALID_CHARS` constant in network.cpp, node.cpp, protocol.hpp and
storage.cpp. These are UTF-8 validation flags, not crypto dependencies, and
must not be removed merely to hide a textual false positive. All actual
wolfCrypt/wolfSSL matches remaining in project source are historical documents.
Recorded DLL SHA-256:
`7EEE7F8471F8943EE0C78EBCA56BB03276E43066130008B1AAF72A5759D4106D`.

## 17. Remaining acceptance/risk

Pending: existing normal key/binding continuity record; physical bidirectional
real-key mailbox exchange; locked/offline-sender test twice; actual FCM,
notification/history behavior; physical Windows/Android live regression. No
physical pass is inferred from host tests. The phone must preserve signing and
recipient keys, Remember Me, registry and history. No production publication.
Legacy aggregate test scripts can contain obsolete build source lists; use the
authoritative Server via test-discovery.ps1 -ServerDirectory, and the scoped
updated Node characterization instead of rebuilding third-party dependencies.

## 18. Rollback and physical plan

No protocol, schema or permanent-key migration occurs. Keep the immutable dev.17
reference; rollback is a source/provider/package decision, not a key rotation.
Old evaluation licensing remains historical and does not authorize publishing it.
When Motorola is available: record public identity/binding IDs before and after
replacement install; run both-direction application mailbox exchanges; then send
to unavailable/locked Android, wait for Stored, fully exit Windows and prove its
process is absent, leave it closed while Android server-only fetch/decrypt/native
accept/signed ACK completes. Reopen Windows and verify Delivered. Repeat twice,
check ordering/dedupe and no-storage mode. If history/notification P0 persists,
record crypto/mailbox pass separately from UX failure; do not fake notification
or require peer TLS to accept mailbox ciphertext.

## Integration validation record

- Windows full normal build: PASS, dev.18. Core/Desktop, Updater, ScreenMedia,
  Server/Admin and Package produced from this worktree. Existing compiler warnings
  remain; no new media implementation changes.
- Provider: official RFC suite vectors PASS; Android host BC 100 each direction
  PASS; direct ABI/auth-output/bounds/provider failure PASS; concurrent 12,000
  round trips with mixed failures PASS; admission/drain and capability release
  PASS. Host interoperability is not physical Keystore acceptance.
- Recipient KeyManager/DPAPI registry/CNG tests PASS (1,000 manager/registry cycles,
  100 CNG persisted reopens); portable Storage/Profile/history/outbox/ACK/dedupe,
  SQL transactions/corruption and group envelope tests PASS.
- Original-message signing/unchanged Windows verifier, native/JNI/BC mutual TLS,
  Schannel interop, certificate/pin/time/tamper/provider rejection PASS.
- Shared reliable wire (1,000 lifecycles), real Schannel/shared endpoint (102
  lifecycles), current and baseline six-node text/ACK/outbox/history tests PASS.
  Reconnect characterization PASS, including stale relay EOF and owner recovery.
- Server discovery/auth/presence/lease/restart, relay requests, remembered-auth
  resume/revocation and three-client encrypted group fanout PASS.
- Android: 82 JVM tests, zero failures/errors; debug/test/release APKs and lint
  PASS. Lint reports 0 errors, 14 existing warnings. ARM64/JNI/release-hook/TLS/
  HPKE dependency audits PASS. Physical replacement installation is recorded below.
- Windows/Android source/generated/About constants/APK versions all dev.18 PASS.
  Updater dev.17 -> dev.18 ordering, manifest/hash, safe extraction including
  notices and 42 updater assertions PASS. The .NET Framework updater validation
  must run in Windows PowerShell; a PowerShell 7 attempt could not deserialize
  through its framework dependency. The failed preparation directory is retained
  separately, not relabeled or overwritten.
- Canonical prepared candidate: `build/development/4.0.2-dev.18/DevelopmentUpdater-validated/PeerChat-4.0.2-dev.18.zip`.
  SHA-256 `E8659A9A3E7424266E565D26D2543D6CB7CF665EB470E1565AF71E7D363CBCF0`.
  Unsigned Development validation policy only; no signature/publication claimed.
- Debug APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
  SHA-256 `012DF4413959383F9B9AB7DCD3374677429701515E4396AF6C533C79517A3877`.
- Normal package contains no old-provider artifacts/symbols, no external Go
  dependency and no absolute worktree path in the Go DLL. Active source references
  removed; remaining old-provider references are the historical documents/table.
- Physical Motorola: moto g power (2025), Android 16/API 36, arm64-v8a,
  authorized serial ZA2235NTXZ. Debug application and instrumentation APKs were
  replacement-installed successfully; installed version is dev.18. No uninstall,
  app-data clearing or normal key creation/rotation was performed.
- Before installation, the normal public identity SHA-256/Peer ID was
  `61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`.
  It is unchanged afterward. The existing protected registry database checksum
  `dc0181a2e9e665f8beb1fa53d3aefe375b9265b08b4d8549bbe0ed47922f9396`
  and protected Remember Me record checksum also match exactly before/after
  replacement installation. This proves byte preservation, not by itself a
  successful application mailbox exchange. Existing-normal-identity native
  Profile open/sign/verify instrumentation passed.
- Physical Go/CNG <-> Android BC/non-exportable Keystore interoperability PASS:
  100 envelopes each direction, tamper rejection and private-export denial.
  These use explicitly namespaced temporary test keys; both test-key/device
  fixture cleanups passed. They do not substitute for existing normal recipient
  key application acceptance.
- Existing physical NativeMailboxTest PASS: 32 production JNI/provider/SQL
  reopens with stable binding, consent rotation and non-exportability assertions,
  restricted to its temporary test profile and cleaned afterward. The normal
  app then resumed its saved authorization: remember_resume completed, authenticated
  messaging_bootstrap returned, 8 contacts/11 devices restored, 8 conversations
  projected and mailbox service completed with zero fetched envelopes. No new
  message acceptance is inferred from an empty fetch. After normal servicing,
  the encrypted registry DB checksum changed; encrypted database byte equality
  is not a claim about post-login metadata equality or key rotation. Existing-key
  application continuity still requires the planned normal-profile exchange.
- Existing-key application exchanges, normal remembered-authentication/liveness,
  locked/offline-sender tests and notification/history characterization remain
  pending. Windows dev.17 was still running when dev.18 was installed; the user
  was asked to close it normally and open dev.18 with the same profile. No user
  Windows process was force-terminated and the existing server was left running.
