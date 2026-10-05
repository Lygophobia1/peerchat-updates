# PeerChat key architecture / key management audit

Audit date: 2026-10-03. Worktree: `windows-android-unified/Create 2D Game/peerchat`.
Branch: `codex/windows-android-unified`; committed HEAD: `a499f50`.
Scope includes the current uncommitted unified Android/FCM implementation, not
just HEAD. This is a source/capability audit, not a live secret-store census.
No production key was created, exported, rotated, revoked, or modified. No merge.
External credential files and their contents were deliberately not opened.

## 1. Executive summary

Recommend one device signing identity and, when its opaque-provider integration
is proven, one device encryption key for **recipient key encapsulation**. Do not
create a permanent key for every feature. Keep local-at-rest protection, group
epoch keys, transport/media secrets, service credentials, and release signing
outside that pair: they have different principals, security properties, and
lifetimes.

Today one normal Android profile has **one long-lived asymmetric signing private
key plus one long-lived symmetric local-record key**. It has no device recipient
encryption key. One current Windows profile has **one long-lived asymmetric
signing identity**, stored as a DPAPI-protected private blob and also imported
into a named CNG container for Schannel. Those are copies of the SAME key, not
two independent identities. Windows local-record encryption uses OS DPAPI, not
an additional PeerChat-created database master key.

Existing peer TLS uses the signing identity on both platforms. It does not supply
a reusable asynchronous recipient decryption key. Existing group keys cannot be
repurposed as private device/DM encryption keys. Android's existing identity is
sign/verify-only; changing it into an agreement key is not a permitted operation.

Recommend a small profile-scoped **DeviceKeyManager / metadata registry**, layered
over existing providers. It must not replace GroupKeyManager, delivery/session
owners, TLS libraries, or server credential ownership; nor contain raw private
keys. Do not implement it during this audit.

Mailbox cryptographic implementation remains paused pending review. A separate
encryption role is justified, but the pinned HPKE library's opaque-key adapter
remains an unresolved implementation gate.

## 2. Evidence and coverage

Searched tracked Windows/native/Android/server/tooling source for key creation,
imports/exports, cryptographic operations, protection, certificates, credentials,
and lifetimes; inspected relevant uncommitted Android/FCM source separately.
Reviewed test identities and legacy prototype crypto instead of assuming every
search match is current product code. Inspected the pinned libdatachannel
certificate lifecycle read-only in the shared dependency source.

Primary evidence:

- `storage.hpp:20`, `storage.cpp` profile generation, validation, serialization;
  `group.cpp` existing P-256 signing and verification.
- `tls.cpp:61` identity import/container and certificate creation;
  `platform/identity_provider.hpp`, `platform/windows_record_protection.hpp`.
- `android/.../platform/SecurityProviders.kt`, `AndroidStoragePlatform.kt`,
  `ServerConfigurationStore.kt`, `RememberedLoginStore.kt`.
- `android/.../platform/tls/{AndroidTlsIdentity.kt,OpaqueTlsSigner.java,PeerTls.java,ServerChannel.java}`.
- `group_key_manager.hpp`, `group_message_envelope.cpp`, `group_key_handoff.hpp`,
  `storage.cpp` group key/outbox/receipt persistence.
- `server/{PeerChat.Server.cs,ServerConfiguration.cs,PeerChat.Server.Admin.cs}`.
- `desktop/{ProfileAccounts.cs,LocalProfiles.cs,VoicePreferences.cs}`;
  `ui_bridge.cpp:23` identity-preserving reset.
- `voice_media.cpp`, `voice_media_topology.cpp`, `screen_share_media.hpp`,
  `media_ice_configuration.hpp`; pinned libdatachannel
  `src/impl/{certificate.cpp,peerconnection.cpp,dtlssrtptransport.cpp}`.
- `updater/UpdateSecurity.cs`, `scripts/{sign-release-manifest.ps1,publish-update.ps1,Start-Discovery-DevServer.ps1}`.
- `network.{hpp,cpp}`, `app.cpp`: legacy PC02 prototype, not the authoritative
  direct builder's current product path.
- `tests/opaque_identity_test.cpp`, `tests/tls-host/*`, Android
  `SecurityProviderTest.kt`; historical physical results in `docs/ANDROID_PORT.md`.

`android/...` abbreviates `android/app/src/main/java/com/peerchat/android`.
Source searches do not prove how many orphaned aliases/certificates exist on an
operator's machine. No certificate/private-key enumeration was performed.

## 3. Current inventory — roles, algorithms and owners

Categories: A identity; B device recipient encryption; C local storage;
D server/service identity or secret; E session/epoch; F derived traffic;
G authentication credential/token; H development/test only. A family may have
multiple per-profile, per-group, or per-connection instances. None of the rows
below prints a secret value.

| ID / category | Object, purpose and algorithm/type | Creation and authoritative owner | Features / scope |
|---|---|---|---|
| K1 A | Windows device identity: ECDSA P-256/SHA-256, asymmetric public/private; Peer ID = SHA-256 of canonical public representation | `Storage::profile`; `Profile`; BCrypt ECDSA P-256 | Device trust, Hello/authenticated peer TLS, signed group ledger/envelopes and device proofs |
| K2 A | Android device identity: ECDSA P-256/SHA-256, asymmetric, SIGN/VERIFY only | `AndroidIdentityProvider`; native `IdentityProvider` borrows operations | Same canonical Peer ID/signature format; native Profile, peer TLS, remembered-auth proof |
| K3 A (copy of K1) | Windows Schannel container/certificate key: same K1, not a new key pair; self-signed ECDSA certificate | `TlsCredentials::Impl`, named `PeerChat-<PeerId>` CNG container imported from K1 | Existing mutual peer TLS |
| K4 A (use of K2) | Android peer TLS certificate/signature: same K2; freshly signed public certificate, no new persistent private key | `PeerTls.certificate`, `OpaqueTlsSigner`, `AndroidTlsIdentity` | Existing peer TLS 1.2; host harness also covers 1.3 |
| K5 C | Android local-record AES-256-GCM symmetric key; PCR1 record header/context AAD | `AndroidRecordProtector` / `ProfileSecurity` | Profile/SQLite record protection, saved server trust/configuration, Remember Me protection |
| K6 C | Windows DPAPI OS protection keys; algorithm/lifecycle OS-managed, not selected by PeerChat | Windows DPAPI via `WindowsRecordProtector`, managed `ProtectedData` consumers | Native profile/history/outbox/group secrets; managed account binding/settings; server TURN secret |
| K7 E (persisted group epoch) | Group content key: random 256-bit symmetric AES-GCM key, per group/roster generation | `GroupKeyManager.ensureCurrentKey`; protected `Storage::saveGroupKey` | E2E group-message encryption; reliable peer-TLS key offers; older authorized history |
| K8 E/F | Peer/server TLS ephemeral agreement and derived record traffic secrets; current Android peer suite TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, test TLS 1.3 AES_128_GCM | Schannel / pinned Bouncy Castle TLS, per connection | Reliable peer TLS and outer server HTTPS/TLS; not outbox decryption material |
| K9 E | Media DTLS certificate private keys: fresh library-generated ECDSA where configured; separate from Peer ID | libdatachannel per PeerConnection, media-generation owners | 1:1/group voice and native/WebRTC screen media routes; signaled fingerprints bind media, not new persistent devices |
| K10 F | DTLS-SRTP traffic/master/keying material, derived from media DTLS; selected SRTP profile is negotiated/library-owned | libdatachannel DTLS-SRTP + libSRTP | Media packet confidentiality/authenticity; not persistent recipient keys |
| K11 G | ICE ufrag/password and TURN-issued temporary credential (coturn REST HMAC-SHA1); not message content keys | ICE library; server `MediaTurnCredentials`; `MediaIceConfiguration` | Media ICE/TURN allocation/authentication |
| K12 D | Server TURN REST shared secret, symmetric HMAC root | Operator / `ServerConfiguration` | Issue expiring K11; never delivered to clients |
| K13 G | Account password and salted PBKDF2-SHA256 verifier (310,000 iterations, 32-byte output); salt is public | User; server registration/login + AccountDatabase | Account authentication, not device identity or content encryption |
| K14 G | Random 32-byte account session bearer token; server stores SHA-256 token hash | Server `RandomToken/CreateSession`; profile account/session repositories | Discovery/presence/relay authorization; default configured session lifetime 336 hours |
| K15 G | Random Remember Me credential, bound to account/device and signed one-shot proofs; server stores hash | Server remembered-device enrollment; `AndroidRememberedLoginStore`; remembered-auth application owner | Headless/foreground account resume and revocation; not a TLS/private identity key |
| K16 G | One-shot device-proof challenge nonce and binding; random 32-byte challenge, 60-second expiry | Server `DeviceProofChallenges` | Enrollment/resume/revoke signatures with action and binding validation; nonce is not a private key |
| K17 D | Outer server TLS certificate/private key; operator-provisioned algorithm, Admin development generator RSA-2048/SHA-256 | Server certificate store/config; ServerAdmin provisioning | Authenticate server, encrypt outer server/relay transport; distinct principal from client devices |
| K18 D | Firebase service-account RSA private key (RS256 signing), external PKCS#8 credential | Operator/GCP IAM; server `FcmPushProvider` imports for signing | OAuth JWT assertion for FCM; never Android Peer ID, TLS identity or message encryption |
| K19 G | Firebase OAuth access token and Android FCM registration token / Firebase installation state | FCM/OAuth provider / Firebase SDK; PeerChat registration owner | Send opaque wake / route push; neither proves recipient message acceptance |
| K20 D (publisher, not runtime device) | Release manifest RSA signing key, RSA >=3072/SHA-256 PKCS#1; embedded trusted public key/key ID | Operator certificate store + release signing tool; updater owns public verifier only | Release provenance; Development feed is a separate unsigned-manifest policy |
| K21 H | APK debug signing key; release app signing key only if separately provisioned | Android Gradle tooling; default debug keystore | Package installation/update identity, NOT PeerChat protocol/device identity |
| K22 H | Test/native/JNI P-256 identities; test AES record keys, group keys, test TLS certificates and updater signature fixtures | Host test harnesses; Android `peerchat.test.*` aliases; isolated certificate scripts | Failure/tamper/compatibility tests, never authorized production identities |
| K23 H/E/F (legacy) | PC02 shared 256-bit room secret and HMAC-SHA256-derived directional AES-256-GCM keys | Legacy `Network::generateKey`, `network.cpp::derive`, old `app.cpp` UI | Explicitly experimental host-readable rooms, not current product group crypto |
| K24 G (external tooling) | Git/hosting publication credentials, if operator configures them | Git/gh credential provider; publishing scripts use external authentication | Upload development/release artifacts; not a PeerChat application key |
| K25 G | Optional server PFX password; password string, not an encryption identity | Operator / `ServerConfiguration.CertificatePassword` / command-line override | Unlock an alternatively configured server TLS certificate file |

**B: no current production device recipient-encryption key was found.** The
mailbox proposal is a design document only. Existing HPKE classes in a dependency
do not mean the application has generated HPKE keys.

Public trust material is also inventoried: verified contact public keys, server
certificate pins/thumbprints, group roster/device public identity mappings,
DTLS fingerprints, release verifier public keys, password salts and Firebase app
configuration/API identifiers. These are NOT decryption secrets. Their integrity
still matters. Do not count them as additional private keys or conflate an API
identifier with the external Firebase service-account private key.

## 4. Persistence, exposure, lifecycle and recovery inventory

Hardware-backed below is not assumed from use of a cryptographic API. OS-backed
software handles and TEE/StrongBox keys have different guarantees.

| ID | Storage / persistence / exportability / application memory | Lifetime, rotation, revocation and recovery |
|---|---|---|
| K1 | DPAPI-protected `profile` record in per-profile SQLite; raw private CNG blob enters native Profile memory and BCrypt imports; **exportable by current design**. BCrypt/CNG software provider, no TPM claim | Profile lifetime; no transparent identity rotation. Replacement changes Peer ID/trust. Native stop wipes Profile blob. Windows backup copies encrypted profile; requires usable original DPAPI context. Reset explicitly preserves identity via `ui_rebuild_profile_identity`; not key regeneration |
| K2 | Alias `peerchat.identity.v1.<profile-hash>`; public marker under `noBackupFilesDir/security/<hash>`; private key opaque, encoded null, not serialized. Historical Motorola test reports identity TEE, not StrongBox; other devices/provider guarantees must be checked | Stable profile lifetime; missing/mismatched key/marker fails closed. No rotation/migration protocol. Uninstall/data loss is not recoverable by copying public metadata; `allowBackup=false`, no private-key backup/recovery implemented |
| K3 | Named Windows MS_KEY_STORAGE_PROVIDER container imports same raw K1; raw blob already exists in K1 memory. Container export policy is not explicitly configured by this code; do not claim non-exportability | Reopened by Peer ID; certificate regenerated per process, NotBefore backdated one day and NotAfter ten years. Reimport possible from accessible K1. No audited orphan-container retirement on profile deletion |
| K4 | Public DER certificate/handles in memory; all private signing delegates to K2. No extra persisted TLS private key | Generated per TLS object; validity backdated one day, ten-year endpoint. Disposal does not rotate K2. Recreate from unchanged identity; existing pin/validity checks still apply |
| K5 | `peerchat.records.v1.<profile-hash>`, AES-256 opaque Keystore key + `records.version` marker; raw key encoded null. Plain records necessarily enter app memory before protect/after unprotect; key bytes do not | Local profile lifetime, no rotation/rekey migration. Missing/corrupt marker/key fails closed, no replacement to decrypt existing data. No portable recovery backup; copying encrypted SQLite alone is insufficient. Hardware level not freshly measured by this audit |
| K6 | OS user DPAPI keys, outside PeerChat registry; PeerChat receives protected blobs/plaintext, not OS master keys. Native context/optional entropy strings are NOT separate secret keys | OS-managed lifetime/rotation/recovery. Current-user scope; machine/user recovery requirements matter. PeerChat backups do not implement standalone DPAPI master-key recovery; no claim of universal cross-machine restore |
| K7 | Protected profile SQLite records `group-key/<group>/<generation>`; protected pending key-offer outbox; raw 32-byte content keys enter native memory for AEAD/handoff, with explicit scratch wipes | Rotate with roster generation; current-member/distributor checks gate handoff and new messages. Keep authorized old generation keys for local history; removal cannot erase a removed member's prior knowledge. No independent cloud/key escrow. Recover only with working protected local profile |
| K8 | Connection/provider memory; not persisted by PeerChat as message-decryption keys. TLS implementation necessarily has ephemeral/traffic secrets internally | Per connection/handshake; destroyed/retired with transport. Renegotiation/reconnect does not replace K1/K2. No backup or mailbox replay of TLS record secrets |
| K9 | Library in-memory certificate private material, no PEM key files configured by current media setup; fresh generation per PeerConnection in inspected dependency | Media route/generation lifetime; rebuilt on replacement; no permanent registration/backup. Different routes may generate multiple keys, not multiple permanent identities |
| K10 | Library/libSRTP memory; raw keying material handled inside native media libraries; no application persistence found | Per DTLS-SRTP association; replaced with media lifetime. No content archive recovery from these keys |
| K11 | ICE/session and server credential-response memory; passwords available to client media/ICE code, excluded from diagnostics. No persistent device key registration | TURN TTL default 600 seconds, config permits 120–3600; ICE credentials per session. Expiry/allocation teardown. Refresh is existing media/server ownership, not a key registry rotation |
| K12 | `TurnRestSharedSecretProtected` in server config, DPAPI CurrentUser; plaintext enters server memory after opening | Operator rotation must coordinate with coturn; no automatic rotation/backup orchestration. Recover only with protected config + usable DPAPI context or operator reprovisioning |
| K13 | Password plaintext transient UI/auth memory; salt+verifier in server SQLite; no client plaintext password persistence in reviewed normal setup | User password lifetime; authenticated account operations govern password changes. Password/verifier is not a reusable HPKE recipient secret. Restore server DB/administrative recovery, not client key registry |
| K14 | Server hashed session row; raw token in runtime; Windows `account-link.dat` DPAPI-protected; Android active session kept by backend, persistent resume uses K15 | Expiry/logout/revocation/account/device authorization checks. No cryptographic identity change when replaced. Windows link reset clears session while keeping account and identity |
| K15 | PCR1-protected `remembered-login.pcr`, raw opaque credential enters auth memory; server `remembered_devices` stores hash only | Explicit enrollment/replacement/revocation; device proofs required. No TTL column on remembered credential itself; issued sessions expire separately. Protected tombstone on local clear avoids backup resurrection. Missing storage key blocks recovery |
| K16 | Server bounded in-memory challenges, signed original bytes in client memory; challenges are consumed once | 60 seconds, one-shot + action/account/device/binding checks; restart discards. No backup; never reuse as content key |
| K17 | CurrentUser certificate store normally; alternate configured PFX path/password supported. Development Admin explicitly exports/imports PFX and marks resulting key Exportable. OS software backing; hardware/operator certificate deployment not audited | Admin/operator regeneration replaces server pin and requires client trust update. Dev cert one year. Manual store/PFX backup policy, no automatic rotation/recovery. Optional PFX password is a config field, not DPAPI-protected by this contract |
| K18 | External administrator-managed JSON, never package/DB; current FCM loader reads PEM/base64/raw PKCS#8 into process memory and signer closure; **file-exportable** service credential | GCP IAM rotation/revocation; operator-managed external file backup/ACLs. No service-account contents or permissions inspected in this audit. This is an operator secret, not a candidate device root |
| K19 | OAuth bearer token cached server memory until provider expiry; FCM token SDK/client runtime and server `device_push` record. Server push mapping expires after 30 days; not local encrypted message state | OAuth response expiry 120–3600 seconds accepted; invalid auth clears cache. FCM `onNewToken` refresh, invalid-token removal, device/remembered revocation removes mapping. SDK installation internals not assumed to be PeerChat device keys |
| K20 | Release private key stays operator certificate provider; clients store only public modulus/exponent, key ID, algorithm and revoked flag | Explicit trusted key set and operator signing/rotation; certificate validity checked. Private export policy/hardware/backup not established by script alone. No key export executed. Release signing is separate from developer client identity |
| K21 | Normal Gradle debug keystore is tooling-owned/persistent and software-exportable; no custom release signingConfig in current app file. Release build success does not prove signed release publication | Debug reinstall signature compatibility; manually replacing signing key changes installation/update authorization. Release/Play signing recovery policy is not implemented here; do not share app signing key with runtime identity |
| K22 | Host software keys may be raw/exportable; Android test aliases use same protected providers with explicit `test_` namespace. Isolated temp files/stores/certificates | Test lifetime/finally cleanup; negative destructive tests only test artifacts. Historical tests assert normal identity/record key untouched and test aliases removed; no tests run on phone for this audit |
| K23 | User-visible/copied shared room secret and native arrays; directional keys derived in memory; prototype is outside current product build | Room/connection lifetime; no current authoritative recovery/rotation promise; host-readable, no forward secrecy. Cannot reuse this legacy scheme for mailbox or current groups |
| K24 | External Git/gh/OS credential store or environment, not managed/read by this audit | Operator/provider expiry/revocation and credential recovery. Do not bring these into profile registry or package |
| K25 | Config field serialized by current Save; alternatively command-line argument; raw password reaches certificate loader. No application DPAPI wrapper in this optional path | Operator certificate/PFX replacement controls password change; no automatic lifecycle. File/command-line exposure and backup permissions require separate operational review; no configured password was read |

## 5. Actual Android architecture

```text
Account (server authorization; not a private-key root)
  └─ Local profile / Android device
       ├─ K2: ONE Keystore P-256 signing identity
       │    ├─ canonical public key → Peer ID / device trust
       │    ├─ native Profile/sign operation
       │    ├─ remembered-auth device proofs
       │    └─ peer TLS certificate + CertificateVerify (SAME key)
       ├─ K5: ONE Keystore AES-256 local-record key
       │    ├─ native SQLite protected records/history/outbox
       │    ├─ remembered credential (K15)
       │    └─ server configuration/pin record
       ├─ active account token (K14), FCM SDK/token state (K19)
       └─ per-connection TLS agreement/traffic secrets (K8)
            no current asynchronous recipient encryption key
```

Current Android messaging client has no implemented voice/screen-media generation
in this scope. Do not count future Android media keys as already-present keys.
Per-profile counts are not whole-phone counts: another explicitly created profile
or instrumentation namespace can have its own keys.

## 6. Actual Windows architecture

```text
Local profile / Windows device
  ├─ K1: ONE durable P-256 signing identity
  │    ├─ DPAPI-protected private blob in profile SQLite
  │    ├─ native signing (raw key material in memory)
  │    └─ K3 named Schannel CNG container (SAME key material)
  │         └─ process-generated peer TLS certificate
  ├─ OS DPAPI protection (K6), no PeerChat DB-wide AES root
  │    ├─ profile/history/outbox
  │    ├─ account session and settings
  │    └─ group epoch keys / pending handoffs (K7)
  ├─ GroupKeyManager → per-group/generation AES-256 keys
  ├─ TLS providers → connection traffic/agreement secrets (K8)
  └─ Media owners → libdatachannel DTLS certs (K9)
                    → DTLS-SRTP traffic keys (K10) / ICE (K11)
```

One PC may separately run Server/Admin and release tools. Their K12/K17/K18/K20
are different principals, not client-profile identity duplication.

## 7. Server secrets and public-key ownership

Server owns account password verifiers, session/remembered hashes, push routing
tokens, its own TLS private credential and TURN/FCM service secrets. Existing
authorized device public keys and signed roster/proof data are public verifiers,
not content decryption keys. Server does not own group content keys or peer TLS
record plaintext. No DM mailbox key/registration exists today.

The FCM private-key-in-memory and optional PFX-password configuration are concrete
existing exposure boundaries, not newly demonstrated exploits. Record them for
separate secret-management review; do not alter them in this audit. A client
KeyManager must not silently take ownership of server/operator credentials.

## 8. Why persistent keys exist; redundancy and unsafe consolidation

- K1/K2 establish a stable device principal. Deriving a new identity from account
  credentials would merge account compromise and device trust; it would also
  destroy existing Peer IDs and pins.
- K5/K6 protect local data/credentials. They must survive transport reconnects
  without being advertised to remote senders. K5 cannot be derived from the
  non-exportable signing key using current APIs; DPAPI is an independent OS trust
  boundary. Neither belongs in remote recipient-encryption protocols.
- K7 is a shared group/membership-epoch secret, intentionally distinct from any
  member's signing identity and unavailable to removed/future members. It is not
  redundant with device storage or prospective recipient encryption.
- K3 duplicates **storage of** K1 because current Schannel needs a persisted
  provider handle while native signing loads a protected blob. There is no second
  independently generated signing identity to remove. A future Windows opaque
  provider could remove raw-copy reliance, but that is a migration, not this audit.
- K4 already avoids a second Android TLS key. No consolidation needed.
- K9/K10 are deliberately fresh media-route secrets/certificates, not permanent
  device key sprawl. Do not consolidate them with Peer ID for aesthetics.
- Local record contexts, configuration records, remembered credentials and TLS
  signatures reuse their existing appropriate providers; they do not each create
  another persistent record/identity key.

No unnecessary independent permanent **product device identity** was demonstrated.
Potential orphaned CNG containers and test artifact hygiene need a separate live
inventory if requested; do not infer deletion is safe from a source audit.

## 9. Identity-key capabilities

Android: exactly `secp256r1`, SIGN | VERIFY, SHA256 digest. No PURPOSE_AGREE_KEY,
no exported private scalar. Existing key authorization cannot be expanded in place
by changing Kotlin configuration. Generating/importing a replacement is a different
key and violates the unchanged-identity requirement. Native Profile rejects an
opaque provider accompanied by a private blob and validates public identity/proof.

Windows: BCrypt ECDSA P-256 generation/import, exported ECC private blob protected
at rest. Schannel imports that same identity into a software KSP. There is no current
ECDH provider operation or persistent encryption-key role. Mathematical ability to
reinterpret a curve scalar is NOT permission to convert a signing key into an
agreement key, export it elsewhere, or claim platform capabilities are identical.

Replacing either public key changes the public-key hash/Peer ID and peer trust.
Windows is currently exportable; Android is currently opaque/non-exportable.
**The existing identity cannot safely and practically handle mailbox decryption.**

## 10. TLS findings

Peer TLS uses K1/K2 for signatures and certificate identity, plus fresh protocol
agreement/traffic secrets. A public certificate is not another private key.
Regenerating a certificate does not rotate its underlying device key. Android's
opaque signing adapter signs original inputs, not a digest that gets hashed twice.

Neither endpoint can later decrypt an arbitrary archived TLS record independently
of the live TLS state. Do not persist/reuse TLS traffic secrets to solve mailbox
delivery. Outer server certificate belongs to the server and would violate E2EE
if reused to make the server a decryption recipient. Media DTLS keys have transient
fingerprint/lifecycle semantics; also unsuitable as stable mailbox recipients.

## 11. Local database/profile protection

This is protected **records** in ordinary SQLite, not SQLCipher or one database
encryption identity. SQLite operational metadata/schema are not claimed encrypted.
Windows native payloads use CurrentUser DPAPI with record context; managed account
links/preferences use their existing DPAPI boundaries. Android PCR1 uses K5 and
binds header/profile/context as AAD. Missing/tampered protection state fails closed.

Profile recovery differs by platform. Windows reset preserves K1 explicitly;
backup copies already-protected records, not a portable escrow of OS keys.
Android disables backup and uses noBackupFilesDir; its non-exportable keys are not
restorable merely from an APK or database copy. Do not consolidate local record
protection with an remotely advertised HPKE key.

## 12. Device encryption-key necessity

Offline encryption requires recipient secret material available without a
simultaneous peer TLS session. Current device signing keys, local protection keys,
group secrets, tokens, server certificates and media keys cannot safely supply it.

A previously negotiated pairwise TLS-delivered mailbox secret could support
some established peers, but creates per-peer secret lifecycle and first-session
availability constraints. It is not the requested independently published device
recipient-key architecture and does not solve offline first provisioning.

Therefore the proposed asynchronous, device-specific public-key mailbox needs a
distinct **Device Encryption Key** role. This need does not establish that every
future feature needs its own permanent private key or that HPKE integration is
already solved.

## 13. Recommended long-term key architecture

```text
Account → authorized devices (no shared account private decryption key)
  Device/profile
    ├─ IdentitySigning handle: existing stable P-256 principal
    ├─ DeviceEncryption handle: independently versioned opaque recipient KEM key
    │    └─ each protocol/envelope creates independent HPKE context/key material
    ├─ LocalRecordProtection provider: existing AES/DPAPI boundary
    └─ existing scoped owners: group epochs, TLS/media traffic, auth credentials
```

DeviceEncryption is not a generic "derive any secret" facility. Only explicitly
allowed, versioned recipient encapsulation/decapsulation protocols use it.
Do not enable signing, TLS client authentication, or arbitrary ECDH under that key.
KEM necessarily needs platform agreement operations; restrict their application
exposure rather than confuse an internal ECDH primitive with a new generic identity.

For multi-device delivery, each currently authorized recipient has its own signed
encryption-key descriptor and independently encrypted envelope. Keep one logical
message ID with per-device delivery metadata/proof. Never clone private keys between
Windows/Android or use an account-wide symmetric decrypting secret at the server.

## 14. Compromise tradeoffs and domain separation

| Model | Benefit | Cost / security limit |
|---|---|---|
| Signing + one recipient-encryption key | Simple per-device trust/rotation and bounded opaque providers; signing compromise and decryption compromise are distinct roles | Encryption-key compromise exposes every protocol that reused that recipient key, including captured old envelopes; no automatic forward secrecy |
| Permanent key per feature | Independent provider permissions, revocation/rotation and cryptographic compartmentalization when keys truly isolated | More registrations, recovery/retirement/migration paths. A full app/device compromise may still access all handles; separation is not automatically effective |
| Root deriving all subkeys | Potentially compact management where supported hardware KDF and scoped access actually exist | Current signing root cannot be a KDF root; extracting it is forbidden. Exported master compromise affects all descendants. No present supported root derivation facility was demonstrated |

Recommend the first model initially, with explicit exceptions for different
administrative principals, trust domains, retention/recovery requirements, or
demonstrated independent hardware access controls. Do not adopt a root hierarchy.

HPKE `info` should include a canonical versioned protocol domain/suite/binding;
AAD must bind canonical account/device IDs, key generation, message/envelope IDs,
and relevant routing/policy metadata. Recipient must enforce the expected domain,
not blindly accept sender-provided domain labels. Suggested domains:
`PeerChat/mailbox/v1`, `PeerChat/file-key-envelope/v1`,
`PeerChat/history-sync/v1`, `PeerChat/device-bootstrap/v1`.

Domain separation prevents cross-protocol confusion and derives independent
contexts; it does **not** make a compromised recipient private key affect only
one feature. Independent random content/session keys are still required. Static
recipient HPKE does not provide forward secrecy against later recipient-key
compromise. Deleting ciphertext at TTL is not erasing an adversary's captured copy.

## 15. KeyManager recommendation and ownership

Recommend a narrow profile-scoped DeviceKeyManager as a capability/lifecycle
facade, not a cryptographic implementation or process-global singleton.

- Reuse IdentityProvider (publicKey/sign), RecordProtector (protect/unprotect)
  and StoragePlatform rather than replacing them.
- Add a distinct recipient-encryption provider only after the opaque-key proof.
- Provide typed handles, purpose checks, create/open, public descriptor, signature
  binding, encryption generation/status, and explicit rotation/revocation actions.
- Fail closed on missing/mismatched keys; never regenerate merely because open
  failed or a server record is absent.
- Keep group content/key-distribution authority in GroupKeyManager, live sessions
  in SessionTransportManager and durable delivery in DeliveryManager.
- Return operations/opaque references, not raw signing/private-key blobs. Windows
  legacy identity handling must be an explicit compatibility adapter, not falsely
  labelled non-exportable.
- No server, updater-signing, Firebase, TURN or APK-signing keys in this client
  manager. No WPF/Kotlin UI key lifecycle owner.

Do not expose one generic `derive()` that bypasses role/protocol authorization.
Implementing this abstraction is a separate reviewed bounded step; no refactor now.

## 16. Non-secret registry recommendation

Recommend a small protected/integrity-checked profile-local registry with key ID,
role, algorithm/suite, public descriptor/hash, provider alias/identifier, schema
version, generation, creation time, active/retired/revoked status and registration
state. It contains **no private material**. Public metadata is not automatically
safe to modify: validate against provider public key and signed bindings on open.

Keep remote authorization separate: "locally active" and "server accepted" are
different states. A crash between key creation, local persistence and registration
must reconcile idempotently without creating another key. Preserve existing
identity/public and records.version markers until a separately reviewed migration.
Never use a mutable display name, IP, route or session ID as the persistent key ID.

## 17. Rotation, retirement, revocation and pending content

Current identity and local-record roles are v1 markers, not full online rotation
protocols. Group content keys already rotate with roster generation and retain
authorized history keys. TLS/media traffic rotates naturally with connection
lifetime. Credential revocation is not signing identity rotation.

Proposed DeviceEncryption generation workflow:

1. Create/open a separate new opaque encryption generation; preserve signing key.
2. Persist registry/provisioning intent atomically with explicit pending state.
3. Sign canonical domain/account/device/algorithm/key/public/generation binding
   plus expected predecessor and fresh server authorization proof as needed.
4. Server verifies against the **already-authorized** device public identity,
   rejects replay/rollback/conflicting generation and session-only substitution.
5. New uploads use the accepted active generation; explicit recipient key IDs
   prevent guessing/rebinding ciphertext to another generation.
6. Retired keys may decrypt previously authorized pending/local content for a
   bounded transition interval; they cannot receive new uploads.
7. Revoked keys/devices deny fetch/new delivery immediately. Pending ciphertext
   addressed to a revoked device is purged under bounded fail-closed cleanup;
   never readdress it using server-only substitutions. Sender learns a bounded
   failure/pending disposition, not fabricated Delivered.
8. Destroy retired handles only after the retention/pending-content contract
   permits it. Lost keys have explicit recovery failure; do not regenerate and
   pretend old envelopes are decryptable.

Identity-authorized encryption rotation is not identity rotation. Identity
compromise requires independent trust/account-device recovery; signatures from a
compromised identity alone cannot make that recovery trustworthy. Non-exportable
encryption means no transparent private-key backup; recovery/archives may require
another separately authorized device, not export of the identity.

## 18. Future-feature strategy

These are recommendations, not implemented capabilities.

| Feature | Permanent-key strategy |
|---|---|
| Encrypted mailbox | B: one per-device recipient encryption key; independent per-envelope HPKE secrets; existing signing key for attribution/receipts |
| File transfer / attachment spool | B+C where asynchronous: wrap fresh per-file symmetric content keys to authorized devices; no FileTransfer identity by default. Live-only transfer can use session keys without new permanent key |
| History synchronization / archive device | B+C: explicit authorized archive-device recipient key and independent session/batch keys; no server history decryption key. Archive device is another principal/device, not another feature key on every client |
| Profile/device metadata sync | A/B/C depending on data: public signed metadata needs no new secret; private sync uses recipient envelopes. Never sync/export signing private identity under this label |
| New-device bootstrap | B+C plus independently authenticated admission/trust ceremony; encryption alone cannot authorize the new device. Fresh device has its own identity, not a clone of the old private key |
| Disappearing messages | C with expiry/key-erasure policy; no new permanent identity. Reusing static HPKE does not guarantee cryptographic disappearance of previously captured/accessible content |
| Android voice/video/screen | A+C/E/F: existing identity authorizes signaling, fresh media DTLS/SRTP/session keys; no permanent voice/video encryption identity |
| Special independent recovery/organization trust domain | D only if an explicit threat model genuinely requires an independent principal/rotation/access policy; not assumed for any above feature |

## 19. Android Keystore and Windows CNG constraints; HPKE decision

Android supports key-agreement-purpose Keystore keys starting API 31. The current
app minSdk is **26**: agreement-backed mailbox capability cannot be advertised on
26–30 without a separately approved non-exporting supported provider alternative.
Do not silently fall back to raw software private keys or raise minSdk during this
audit. The Motorola API 36 can be a proof target, but an API number is not proof
that a particular HPKE library interoperates with its opaque provider.

Keystore keys need the appropriate agreement authorization at creation. This
applies to a NEW distinct encryption key, not the existing signing key. Provider
availability, locked-device usage policy, hardware level, error propagation and
agreement output encoding require explicit tests. No user-auth-per-operation flag
should be guessed if background mailbox decrypt is a requirement.

Windows CNG supports persisted ECDH handles and `NCryptSecretAgreement`. A new
encryption role should disable private export and use an appropriate KSP; software
KSP is OS-backed, not automatically TPM-backed. The current legacy ECDSA blob path
must stay unchanged. Raw agreement output normalization and interoperability must
be verified, not inferred from shared curve names.

The candidate remains RFC 9180 DHKEM(P-256, HKDF-SHA256), HKDF-SHA256,
AES-128-GCM, Base mode plus existing device signatures. This is NOT a selected
production implementation. The actual pinned `bcprov-jdk18on:1.86` contains HPKE,
but built-in DHKEM decapsulation requires ECPrivateKeyParameters and reads its
scalar. The external KEM abstract methods are package-private; there is no clean
application-package JCA opaque-key callback in the inspected artifact. Registry
or domain separation does not fix that limitation.

Safest next implementation investigation AFTER audit review: prefer a vetted
HPKE library with a supported external-agreement hook; otherwise an upstream or
explicitly reviewed minimal hook preserving the existing vetted DHKEM/key schedule
and delegating only ECDH to Keystore/CNG. Do not reimplement HPKE/KDF/AEAD or use
reflection/split-package tricks to imply official opaque-key support. Prove RFC
vectors, cross-platform encryption/decryption, malformed points, wrong binding,
AAD/ciphertext tamper, revocation and private-key non-export before registration.
No external-DH proof was implemented or executed during this audit.

Primary platform/standard sources:

- [Android key purpose and API levels](https://developer.android.com/reference/android/security/keystore/KeyProperties#PURPOSE_AGREE_KEY)
- [Android Keystore guarantees and authorization constraints](https://developer.android.com/privacy-and-security/keystore)
- [Windows NCryptSecretAgreement](https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptsecretagreement)
- [RFC 9180, including security considerations](https://www.rfc-editor.org/rfc/rfc9180.html)
- [Pinned-version BC DHKEM source](https://raw.githubusercontent.com/bcgit/bc-java/r1rv86/core/src/main/java/org/bouncycastle/crypto/hpke/DHKEM.java)
- [Pinned-version BC HPKE source](https://raw.githubusercontent.com/bcgit/bc-java/r1rv86/core/src/main/java/org/bouncycastle/crypto/hpke/HPKE.java)

## 20. Explicit security-review answers

1. **Android today:** one long-lived asymmetric private identity per normal
   profile; also one distinct long-lived AES local-protection secret. No encryption
   recipient private key. Do not call the AES key a second asymmetric identity.
2. **Windows today:** one long-lived asymmetric identity per profile, in two
   storage/handle representations; DPAPI keys are OS-managed. Persisted group keys
   are variable-count epoch secrets. Server/tooling keys are additional principals.
3. **Mailbox adds:** one distinct recipient encryption private key per device,
   with temporarily retained old generations during rotation; no new signing/TLS
   identity. It does not add one permanent private key per conversation/message.
4. **Remain separate:** signing vs recipient encryption, local protection vs
   remotely addressed encryption, group epochs vs device keys, server/operator
   secrets vs clients, release/package signing vs protocol identity.
5. **Redundant?** No redundant independently generated production device identity
   found. Windows raw blob + CNG container duplicates custody; audit that future
   migration separately. Do not delete containers or legacy code during audit.
6. **One signing + one encryption target?** Yes, plus existing local/scoped keys.
7. **Cross-feature compromise acceptable?** A deliberate initial tradeoff, not
   absolute isolation. Shared private-key compromise affects all its recipient
   protocols; explicit higher-isolation features may justify exceptions.
8. **Domain separation sufficient?** For protocol/context confusion and independent
   derived contexts when enforced, not for private-key compromise compartmentalization
   or forward secrecy.
9. **Android operations?** Agreement purpose on API 31+; current key lacks it and
   the pinned HPKE adapter is not proven. Hardware/background behavior needs tests.
10. **Windows operations?** CNG supports the model; current identity path is not
    an opaque ECDH provider. Shared HPKE integration still needs proof.
11. **KeyManager?** Yes, narrowly profile-scoped typed operations/capabilities,
    existing owners unchanged; do not implement during this audit.
12. **Registry?** Yes, non-secret integrity-checked version/lifecycle metadata,
    not another private-key store or source of remote authorization by itself.

## 21. Exact recommendation, validation and stop

Adopt **DeviceEncryption** as a narrowly authorized per-device recipient-envelope
role, initially used only for mailbox; reserve reviewed protocol/domain-separated
uses for future features instead of proliferating permanent feature keys. Preserve
current signing Peer ID/TLS identity and local-at-rest boundaries. Plan the small
opaque-provider facade and registry, not a broad crypto/session rewrite.

This audit changed documentation only. Existing source and dirty work were not
reset, staged, committed, or merged. No phone manipulation, key generation,
private-key export, new crypto implementation, package rebuild, or new physical
validation was necessary. Earlier provider-reuse/native delivery regression passed
before this audit; it is not evidence that mailbox crypto works. Historical
Keystore TEE/non-export tests are identified as historical, not freshly rerun.
Check the document diff/whitespace; leave the audit for review before any external-DH
proof, production encryption key, registration, or mailbox payload storage.

MAILBOX DEVICE ENCRYPTION KEY REQUIRED

KEY MANAGER RECOMMENDED
