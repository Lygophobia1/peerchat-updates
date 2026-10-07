# Linux/Pi dev.34 integration candidate

Version: 4.0.2-dev.34. Starting source c95f5e7 / dev.33.
Isolated branch: codex/network-cleanup-phase1. No main/Android branch merge.

## Mailbox startup and shutdown

The native Windows mailbox entry required a positive authenticated local account
before all operations, including public identity operation 7. The desktop could
start mailbox work after assigning discovery but before publishing the initial
authenticated friends/account snapshot. The missing context was incorrectly
classified by the generic protocol-object assertion. Subsequent snapshots
published the context, explaining the apparent recovery.

The desktop now gates mailbox execution on the published authenticated account.
Native command serialization preserves publication-before-mailbox ordering.
Missing context has its own error; wire-size/malformed-object rejection remains.
Shutdown/sign-out/profile maintenance stop new executor work, drain active work,
and close RPC before native teardown. Existing bounded RPC serialization and
ambiguous-operation non-replay behavior are unchanged. EOF and partial transport
frames remain I/O errors, not mailbox parser input. RPC reuse was not the
identified startup cause.

## Android server trust

Normal setup requires host/port, not manual SHA-256 typing. Inspection uses the
existing pinned TLS validator with an impossible inspection pin: the handshake
is rejected after certificate validity/fingerprint inspection, before application
authentication. No trust-all connection is created.

First use shows endpoint/fingerprint and explicit Trust Server/Cancel actions.
Trust re-inspects and checks the exact approved pin before saving through the
existing protected server-configuration store. Cancel writes nothing. Existing
matching pins need no prompt; changed certificates show expected/observed values
and fail closed with no one-tap replacement. Blank edits cannot erase a stored
pin for the same endpoint. Advanced manual pin entry remains available.
No peer/device trust, signing identity, recipient key, or history changes.

## Linux TURN and FCM deployment

The old isolated Pi configuration explicitly disabled both providers and used
test TURN metadata. Dev.34 enables the existing portable providers.
The operator confirmed the correct public TURN address is 173.225.14.37:3478
(not the originally requested 173.226.14.37). No client TURN hardcoding.
TURN realm peerchat, UDP, temporary REST credentials, 600-second TTL.
The existing Windows DPAPI-protected REST secret was transferred securely to
/opt/peerchat-test/secrets/turn-rest-secret. Coturn configuration/process is not
modified or restarted.

The existing Firebase project peerchat-development and existing service-account
credential are reused. Linux credential location:
/opt/peerchat-test/secrets/firebase/peerchat-development-service-account.json.
Secret directories are 0700 and files 0600, outside artifacts/source/diagnostics.
The ARM32 production provider successfully parsed the credential, signed OAuth
using portable RSA, acquired a real Google access token and reused its cache.
Only a safe pass/fail was emitted. No actual device push is claimed by that test.
FCM remains opaque HIGH-priority data wake only; mailbox durability is independent
of push success.

The dev.34 server uses the existing dev.33 SQLite database and TLS certificate,
not a fresh replacement database. Public certificate SHA-256:
62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.
Target listener: 192.168.1.6:7443.

## Automated evidence

- Windows full build/package/import audit passed, including restricted DLL loading.
- Actual native mailbox missing-context classification and ten profile reopen
  cycles passed with unchanged identity.
- Mailbox executor account gating, 100 stop/drain cycles, EOF/partial frame and
  malformed-record checks passed.
- Discovery RPC reuse/bounds, partial/idle closure, reconnect, authentication,
  device ownership, presence and durable restart regressions passed.
- Remembered login/push registration binding, replacement, durable reopen,
  invalid-token retirement and device revocation regressions passed.
- Native HPKE and managed mailbox canonical/tamper/multi-device/sender-independent
  store/fetch/signed ACK/quota/TTL/revocation/restart regressions passed.
- The portable server parity suite passed on ARM32 Pi: SQLite/lifecycle,
  signatures/JSON, mocked FCM, group roster/fanout/rooms, presence and mailbox.
- Linux x64/ARM64/ARM32 build, version and runtime-only packaging audits passed.
- Android ARM64/JNI/dependency/credential-containment audit passed.
- All 90 Android JVM tests, debug/test/release APK builds and lint passed.
- Development updater dev.33 -> dev.34 manifest, SHA-256 and safe extraction
  passed. Unsigned development policy only; no publication.

The new real-TLS host fixture explicitly selects JDK SunJSSE for its server.
Robolectric installs Conscrypt globally during the full JVM suite; selecting an
implicit provider caused JDK 22 reflective java.net access failures in the fixture.
No production TLS behavior/module-access policy was weakened to fix this.

## Deployment and real provider evidence

Source/build checkpoint: c2a4e4d63f20243d6587831441d689dea1cfc9cb.
The existing tooling cleanly stopped only dev.33 and started dev.34 (PID 15754)
at 192.168.1.6:7443. Port 17443 is closed. Existing SQLite path and account,
device, push-registration and mailbox row counts were preserved; integrity_check
returned ok. The PFX and public pin are unchanged. Coturn remains PID 2718.
Dev.33 artifacts/configuration are retained.

The existing Windows discovery TURN harness passed against Linux dev.34:
anonymous/logged-out issuance rejected, real temporary credentials accepted by
existing public coturn, relay-only ICE/DTLS data and DTLS-SRTP media passed,
bad and genuinely expired credentials rejected, and fresh reissue passed.
The configured 600-second TTL was not shortened to accelerate this test.
This proves a usable Windows media relay path, not physical voice/screen quality.

Actual Linux certificate inspection and pinned reconnect also passed using the
production Android ServerChannel/Bouncy Castle code on the host JVM. This is
not substituted for a physical Android trust-dialog test.

FCM configured successfully at Linux startup. Credential transfer hashes match;
secret contents/hashes were withheld. Dev.34 logs were checked against actual
secret/private-key material and credential-bearing patterns with no matches.
At handoff the existing Pi DB has zero push registrations. Android must log in
normally to register its current token; no old registration was copied and
successful OAuth is not claimed as proof of an actual Android opaque wake.

The Motorola reconnected during handoff: moto g power (2025), ADB authorized.
The old installed version was dev.22 (explaining its old manual-pin error).
Dev.34 was replacement-installed with adb install -r and launched successfully.
firstInstallTime remained unchanged; no uninstall, data clear, identity/key
rotation, protected-record deletion or Remember Me/history reset was performed.
Physical identity/history continuity and trust/FCM acceptance remain user checks.

## Preserved artifacts

- Windows: build/development/4.0.2-dev.34/Client/PeerChat.Desktop.exe
- Development ZIP: build/development/4.0.2-dev.34/DevelopmentUpdater/PeerChat-4.0.2-dev.34.zip
  SHA-256 4421e0f5ac2e446a9acea89f5750920962c2c37e02485d9253864123b4e09ab6
- Android: build/android/4.0.2-dev.34/app-debug.apk
  SHA-256 86f9b8c10b7a2f6749e2207c04b2d8a87c3c9cf547d38325ae356d798359bc5d
- ARM32: build/linux/4.0.2-dev.34/PeerChat.Server-4.0.2-dev.34-linux-arm.zip
  SHA-256 d279eb1e3432fe32aa210f512f09c5f967508a7cc4c32ee3a4485cf601ecbc42
- Pi runtime: /opt/peerchat-test/server/4.0.2-dev.34/
- Pi configuration: /opt/peerchat-test/config/4.0.2-dev.34/server-config.json

Development-only, not published. Only the pre-existing three untracked object
files remain excluded from source checkpoints.

## Remaining physical checks

User physical checks: Windows login/close/reopen, Android first-use Trust/Cancel
and mismatch rejection, messaging, Stored then sender-exit mailbox delivery,
background/locked opaque FCM wake/notification/ACK, voice, screen share, and
presence remaining Online while idle peer routes retire.
