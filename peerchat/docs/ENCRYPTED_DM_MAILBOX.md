# Temporary encrypted DM mailbox — security boundary and implementation contract

Status: design prerequisite, not implemented or physically accepted. The live
delivery path remains unchanged. No mailbox payload may be uploaded until the
encryption, authorization, and receipt gates below pass.

## Existing boundaries verified in the current source

- `platform/identity_provider.hpp` exposes public key and original-message signing,
  not decryption or key agreement.
- `AndroidIdentityProvider` in `SecurityProviders.kt` creates its existing P-256
  Keystore identity with SIGN/VERIFY and SHA-256 authorization only. Preserve it.
- Direct text in `portable/reliable_endpoint.hpp` is protected by authenticated
  peer TLS. Its serialized text frame is not independently encrypted for storage
  on the server. TLS records cannot serve as independently decryptable mailbox
  envelopes after that TLS connection ends.
- `DeliveryManager` owns durable message acceptance and authenticated ACK handling.
  Its current ACK authorization is tied to an actual submitted live transport.
  A mailbox receipt needs its own verified proof input, not a fabricated transport
  instance or relaxation of those checks.
- Existing delivery values are Queued=0, Sent=1, Delivered=2. Preserve their numeric
  meanings; a new Stored state must not reinterpret Sent or Delivered.

## Approved encryption-key separation; provider integration blocked

The user approved a separate purpose-specific device mailbox encryption key,
authenticated by the unchanged signing identity. The user additionally requires
stopping rather than exporting mailbox private-key material if the HPKE library
cannot consume an opaque platform key. No production mailbox key was created.

Recommended: a purpose-specific recipient mailbox encryption key, bound by a
domain-separated signature from the unchanged device identity. This is not a new
account, Peer ID, TLS identity, or replacement signing key. Use an established
HPKE implementation and standard suite, with Windows/Android interoperability and
known-answer/tamper tests before enabling uploads. The pinned Android Bouncy
Castle 1.86 artifact contains HPKE classes, but that alone does not prove a viable
shared Windows/Android provider or opaque-key adapter. Audit that boundary first.

The encryption-key descriptor must bind protocol/suite, account, device Peer ID,
key ID/generation, public key, validity, and mailbox capability. Validate its
signature against the existing verified contact key and current account/device
authorization. Server publication alone must never establish a new trusted key.
Missing, conflicting, revoked, expired, or unverifiable descriptors fail closed;
the local durable message remains Queued and live delivery remains available.
Protect private encryption material locally; never upload it or export the
existing Keystore signing key. Key persistence and retirement require restart
tests and an explicit policy for still-pending envelopes.

### Candidate suite and verified limitation

Candidate library: the existing pinned `bcprov-jdk18on:1.86`, not a floating new
dependency. Candidate RFC 9180 suite: DHKEM(P-256, HKDF-SHA256) (0x0010),
HKDF-SHA256 (0x0001), AES-128-GCM (0x0001), Base mode with existing device
signatures for binding, envelope attribution, and receipts. This suite is not
enabled or selected as a completed platform integration.

Inspection of the actual pinned JAR with javap and the corresponding Bouncy
Castle r1rv86 source proves that built-in DHKEM decapsulation calls
`calculateRawAgreement(rawAgreement, kpR.getPrivate(), pkE)`. Its
`ECDHCRawAgreement.init` casts the key to `ECPrivateKeyParameters`, and
`ECDHCBasicAgreement` reads `getD()` for scalar multiplication. An opaque Android
Keystore PrivateKey cannot satisfy that path without exposing its scalar.

The HPKE constructor accepts a KEM implementation, but KEM's abstract methods
are package-private in the pinned artifact; it is not an ordinary application
package callback adapter. DHKEM's rawAgreement field is private and no supported
constructor injects a JCA KeyAgreement. Do not put an exportable scalar into a
placeholder key, use reflection to mutate the provider, or add application code
under the library package to claim supported opaque-key integration.

Recommended feasibility boundary: an upstream-supported or explicitly reviewed
minimal external-DH hook in the vetted library's existing P-256 DHKEM, retaining
its RFC 9180 validation, ExtractAndExpand, key schedule, and AEAD. Android would
generate a separate P-256 Keystore key with PURPOSE_AGREE_KEY (API 31+) and use
`KeyAgreement.getInstance("ECDH", "AndroidKeyStore")` with the opaque PrivateKey.
Windows would use a distinct persisted non-exportable CNG ECDH key. This is a
proposal, not a verified implementation; provider support, raw DH encoding,
failure propagation, known-answer vectors, and cross-platform interoperability
must pass before registration or uploads.

Sources:

- https://raw.githubusercontent.com/bcgit/bc-java/r1rv86/core/src/main/java/org/bouncycastle/crypto/hpke/DHKEM.java
- https://raw.githubusercontent.com/bcgit/bc-java/r1rv86/core/src/main/java/org/bouncycastle/crypto/hpke/HPKE.java
- https://developer.android.com/reference/android/security/keystore/KeyProperties#PURPOSE_AGREE_KEY

Alternative: find and pin another mature HPKE implementation with an actual
external key-agreement callback. Protected-but-exportable software mailbox keys
are not authorized. Prior peer-TLS shared-secret provisioning is not the approved
mailbox encryption-key design and must not be substituted silently.

Neither initial static-key design establishes forward secrecy against later
compromise of the recipient mailbox decryption key. Do not claim it. A prekey or
ratcheting protocol would require a separate explicit scope/design decision.

## Ownership and policies

DeliveryManager retains message IDs, local durable outbox/history, final delivery
state, ACK verification, and dedupe. SessionTransportManager retains live route
selection/reconnect/backoff. Platform adapters execute bounded authenticated server
I/O; they do not become another outbox or retry owner.

Separate `ServerStoragePolicy` (Allowed/Forbidden) from `RetentionPolicy`
(Permanent/Ephemeral/DeviceOnly). Only temporary delivery is implemented here;
no archive/history synchronization or disappearing-message UI is implied.
Forbidden must prohibit payload upload on every route, retry, and restart, not
merely hide an upload control. Unsupported retention semantics fail explicitly.
Recipient storage permission must also be authenticated before upload; absence
of that permission is not consent.

After durable local enqueue, use an already-usable live route immediately.
Otherwise use the mailbox only if all policy, capability, trust, and encryption
requirements are satisfied. Do not wait a full Android wake budget for live
route recovery. Stable IDs and existing native dedupe resolve concurrent live
and mailbox delivery; no second retry scheduler is introduced.

## Versioned envelope and receipt

An envelope is per authorized recipient device, with a stable logical message ID.
Bind version/suite, sender account/device, recipient account/device, encryption
key generation, message ID, policy, creation/expiry metadata, and encapsulation
to authenticated encryption and the sender's existing device signature. Keep
message body and unnecessary metadata encrypted. Preserve existing logical
ordering and ID conflict rejection. Never accept sender-supplied display names
as authenticated identity.

The exact canonical encoding and suite are not selected by this document.
Publish test vectors and strict bounds before declaring a wire version supported.

Queued means only local durable acceptance. Stored means durable server ciphertext
acceptance and is not Delivered. Delivered requires recipient acceptance committed
through the normal native receive/history/dedupe boundary and a verifiable receipt.

The recipient receipt must bind recipient device, sender device, message ID,
envelope digest and protocol domain/version, signed by the recipient's existing
identity. Native DeliveryManager verifies it against the intended authenticated
recipient and its persisted submission before marking Delivered. A server event
or authenticated server response alone cannot impersonate a recipient ACK.

Only after durable native receive may a recipient submit the receipt and authorize
server ciphertext deletion. Replays must reproduce successful receipt behavior
without another visible message. Retain bounded receipt reconciliation data after
ciphertext deletion so an offline sender can later learn Delivered. Receipt expiry
must not fabricate delivery; specify that limit before implementation.

## Server storage and wake

Use the current server database/migration and transaction conventions. Store only
bounded ciphertext/routing metadata, with per-device/account byte and item quotas,
maximum envelope/request sizes, TTL, and expiry cleanup. Idempotency binds sender,
message ID, recipient device and wire version; reject conflicting reuse rather
than replace ciphertext. Commit before replying Stored or sending an opaque wake.

Store/fetch/ACK/reconcile require current authenticated account/device authorization
and friendship/block/revocation checks. A revoked recipient cannot fetch or ACK;
pending data for a revoked device must not be redirected to another device. No
retroactive device-key substitution. Expired entries are not fetched/delivered.

FCM remains high-priority opaque wake only. Android's existing remembered-auth and
bounded wake path fetch ciphertext, invokes native authenticated envelope receive,
commits history/dedupe, submits the receipt, then posts a real notification.
Thermal/interruption retry and coalescing remain intact. No foreground Activity
or simultaneously running Windows sender is required for that flow.

## Gates

Before physical use, prove standard crypto interoperability/tampering, protected
key persistence, forged receipt rejection, real native receive/dedupe, durable
server restart, sender restart/reconciliation, lost store responses, quota/expiry,
revocation, live/mailbox races, and Forbidden producing no server payload writes.

Then perform replacement-install-only foreground, background, three locked runs,
and the critical sender-completely-closed-after-Stored test. The current delayed
dev.7 live delivery result does not constitute mailbox acceptance. Preserve all
existing profiles, Keystore identity, Remember Me state, and immutable packages.
