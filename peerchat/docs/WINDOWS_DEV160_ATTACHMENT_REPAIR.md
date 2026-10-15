# Windows dev.160 attachment repair

## Scope and baseline

This Windows-only repair continues `codex/native-unicode-emoji` from `96de469` in
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
The accepted dev.158 package and existing Pi deployment are preserved. No live
server, Android Work, identity, group key, grant or history migration is included.
Physical acceptance of these repairs remains pending.

The user's dev.158 DM test completed 98,715,413 bytes with integrity verification,
64 KiB chunks and 16 outstanding chunks, reporting approximately 27.08 Mbps
underway average payload speed. This repair does not change those protocol
settings, the 32 KiB × 1 default, or the 256 MiB file limit.

## Confirmed causes and corrections

### Authenticated group cache capability

Windows used its password/account session for cache RPCs. The deployed server
requires a device-bound session and returns `cache_device_session_required`.
The native capability parser did not recognize that bare rejection, threw a
validation error, and retained its initial Waiting label. Group text uses a
different authorization boundary, explaining why text worked.

`AttachmentDeviceSession.cs` now obtains the server's existing enrollment/resume
challenges over pinned TLS. A first enrollment requires explicit confirmation.
Existing saved credentials resume without another enrollment. A current main
session that already passes the real cache capability probe is not reenrolled.
The main messaging session is never replaced by the separate cache RPC client.

The adapter validates the exact account, local device, purpose, challenge ID,
session/credential binding digest and signature input, with a 60-second request
age bound. Native `WindowsMailbox` operation 9 independently checks the published
authenticated account, persisted account binding, current identity, exact scoped
input, freshness and bounded one-shot challenge tracking. It uses the existing
identity signing operation without exporting private keys. Server verification
remains authoritative for authenticated challenge issuance, expiry and replay.

Credentials use a versioned DPAPI-protected sidecar scoped by profile path,
server endpoint/pin, account and device. Corrupt or revoked credentials are not
silently overwritten or reenrolled. First enrollment explicitly warns that the
server replaces an older remembered credential for the same device identity.
It is offered only after the server rejects the current main session as unbound.

The controller waits asynchronously for capability readiness, preserves failed
actions for retry, checks profile/session lifetimes before signing, submitting
and saving, and drains outstanding work before native disposal. Native capability
probes have bounded retirement; late replies cannot satisfy newer request IDs.
Safe capability stage, code and generation appear in copied full diagnostics.

### Original received filenames

The original basename was already in authenticated transfer metadata. Windows
`FileSink` constructed the final path as `<transfer-id>-<basename>`.
DM acceptance now requests an exclusive `<transfer-id>.peerchat-received` child
directory containing the exact original basename and extension. Duplicate names
from different transfers remain independent, and files are never overwritten.
Unsafe names, traversal, reparse paths and incompatible durable tokens fail closed.

The durable token format is unchanged. Reopen accepts both the legacy ID-prefix
layout and the new exact-basename layout, checking metadata and file identity.
Existing received files are not renamed. Save-copy continues using exclusive
creation. Protected group staging/finalization layout is unchanged; its existing
verified export retains the original filename.

### Clipboard screenshot drafts

`ClipboardAttachments.cs` handles image paste in the existing composer, including
bitmap and DIB clipboard data. It validates dimensions and DIB structure, encodes
PNG, and shows the existing attachment draft preview. Normal text paste retains
precedence. The existing Send action is required; paste alone never sends.

DM screenshots use ordinary local PNG source files in the associated profile's
`ClipboardAttachments` directory and the existing durable file-transfer engine.
Accepted sources remain available for restart recovery, like other file sources.
Group screenshots use the existing bounded PNG attachment operation and the same
capability gate as selected files. Draft, preparation and send guards prevent
duplicate submissions and cross-conversation/profile sends. Clipboard drafts are
limited to 16 million pixels and 16 MiB PNG; larger images can be saved and
attached through the ordinary file picker.

## Integration manifest

- Shared Windows files: `windows_mailbox.hpp`, `node.cpp`,
  `windows_transfer_io.hpp`, `windows_group_attachment_application.hpp`.
- Managed adapter/UI: `AttachmentDeviceSession.cs`, `DiscoveryClient.cs`,
  `GroupAttachmentExecution.cs`, `ClipboardAttachments.cs`, `GroupAttachments.cs`,
  `ChatAttachments.cs`, and narrow composer/lifecycle/diagnostics hooks in `App.cs`.
- Local ABI addition: optional `ui_mailbox` operation 9, bounded to 1024 bytes,
  with scoped proof request and public-key/signature response. Operations 0–8
  retain their existing semantics. No new export, message wire format, encryption
  format, transfer journal schema or server authentication protocol is introduced.
- Android's existing remembered-device challenge flow was inspected read-only.
  Android does not need this Windows-local operation or sidecar. No Android source
  is changed. Main Work's new opcode must be preserved in a later shared-source
  integration rather than assigned conflicting semantics.
- Server requirement: existing dev.158 device-proof and encrypted cache APIs.
  This repair requires no Pi update or cache reset.

## Validation and physical acceptance

The dev.160 Windows build and runtime dependency/HPKE/media ABI package audits
pass. Focused validation includes:

- Native scoped-proof rejection for absent/wrong account, wrong device/purpose,
  changed signature input, stale/future timestamps and replay; valid signatures
  verify with the existing CNG public key.
- Pinned-server enrollment/resume, actual `CoreAdapter → Node → WindowsMailbox`
  signing, encrypted sidecar persistence, restart identity stability, profile
  isolation, stopped-profile guards, replay rejection and revocation without
  automatic reenrollment. The ordinary token's real `cache_pending_keys` request
  is rejected; enrolled and restarted/resumed native-proof sessions receive
  `accepted`, correlated request IDs and cache version 1. Final evidence:
  `C:/Users/Livel/AppData/Local/Temp/PeerChat-attachment-device-326f0c3df8f74d62b221d586c0b6ebea`.
  Cache evidence:
  `F:/PeerChat-isolated-cache-tests/PeerChat-attachment-cache-test-aacf4dc8cd014314b0d091a4ba17768a`.
- Actual password-login rejection projected by the native capability controller,
  followed by the production Application/TransferManager/native HPKE/TLS cache
  fixture: one upload, two recipients, sender offline, key acknowledgement,
  restart during preparation/download/finalization, verified export, independent
  signed receipts, server reconnect/restart, late-member/rejoin rejection and
  disabled-cache refusal. Evidence:
  `F:/PeerChat-isolated-cache-tests/application-28ce44a2245c4312848bf362d96a81b7`.
  This fixture seeds its positive device sessions; the separate pinned-server
  fixture exercises the new actual native enrollment/resume adapter.
- Original-name sink partial/completed reopen, duplicate names, old durable
  tokens, traversal/reserved-name rejection, altered metadata and commit races.
- Final package group attachment UI, clipboard bitmap/DIB/PNG, malformed and
  truncated DIB rejection, text precedence, draft scope, verified attachment
  previews/history/export, Unicode and emoji/favorites/multiple insertion.
- All 32/64 KiB × 1/4/8/16 pipeline integrity/credit cases, legacy negotiation,
  timing/credit/budget assertions and packaged host pipeline cases, including
  64 KiB × 16. No performance change is claimed from the host fixtures.
- Concurrent screen sharing and verified file transfer, screen-stop isolation,
  invalid local mailbox rejection, fresh relay pairing and confirmed-prefix
  recovery. Evidence: `build/tests/media-transfer-8f78dee2265f4fd4b490f4ac822ba56d`.
- Native peer-TLS file session with unverified-peer rejection, exact Unicode
  binary payload, profile close/reopen, partial restart, reconnect, final hash,
  message ACK coexistence and protected long-text completion/reopen.

Two test-fixture assumptions were corrected rather than weakening checks: the
native bridge now waits for the asynchronous Ready event before opening its
profile, and older transfer/media fixtures read partial/completed files in the
new original-name receive directory. The managed cache fixture uses the existing
.NET Framework long-path AppContext switches and an isolated drive with the
required free-space reserve; neither production cache safety nor server behavior
was changed. All final focused tests pass. Existing compiler warnings remain.

## Checkpoints and package

- `0a7b18c`: durable original-basename sink, legacy reopen and collision tests.
- `b12f24f`: scoped account/device proof adapter and native security regressions;
  activates original-name DM acceptance.
- `c0b063b`: dev.160 cache readiness/lifecycle, screenshot drafts and Windows
  regression integration. This is the compiled implementation checkpoint.
- The following handoff checkpoint adds the final real-server fixture, integration
  notes and packaging audit script without changing compiled production sources.
  Its exact hash is embedded in the candidate manifest.

Windows runtime:
`build/development/4.0.2-dev.160/Package/PeerChat.Desktop.exe`.
Windows archive:
`build/development/4.0.2-dev.160/PeerChat-Windows-4.0.2-dev.160.zip`.
The working tree is clean after checkpointing; ignored build and fixture evidence
remain preserved. Dev.159 is an intermediate candidate, not the handoff package.

Controlled fixtures do not establish physical group delivery or network
throughput. Versioned ZIP integrity and the exact source checkpoint are recorded
in `candidate-manifest.json` and `windows-repair-package-audit.json` beside the
package. The package contains only the Windows client runtime and this handoff,
not a Pi server update.

For manual testing, close the old client and open the new package using existing
profiles on both Windows PCs. Do not copy profile data into the package or change
trust/grants. In Family, choose Attach and explicitly confirm device verification
if offered. Confirm a picture and a file upload and reach both recipients, then
check reconnect/restart and recipient verified Save/Open. Test Ctrl+V from Snipping
Tool in both DM and group conversations: preview first, explicit Send, removable
draft, no duplicate send, and normal text paste. Receive `PeerChat.rar`, verify
its exact basename/extension inside its unique transfer folder, repeat the same
filename, and verify Open/Save after restarting.

Stop for the user's physical results. No Pi redeployment is authorized by this
repair handoff.
