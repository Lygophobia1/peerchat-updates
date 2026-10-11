# History Sync UX — dev.74

Windows presentation redesign on top of dev.73. Dev.67 remains the original
history physical candidate; dev.73 remains the attachment candidate. Neither
immutable package is overwritten. No publication, merge, identity changes, data
deletion, Pi deployment, or server history changes are part of this work.

## Normal workflow

Settings / Privacy & Security shows History Sync, THIS DEVICE, YOUR HISTORY
DEVICES, native authorization state, saved Receive/Provide permissions, and
current directional activity. Local identity is never an item in a remote-device
dropdown. Platform/availability are unknown unless existing authenticated state
proves them. Peer IDs and authorizer controls are in collapsed Advanced sections.

Authorize device opens a dedicated New history device dialog with short identity,
nickname, modern Receive / Provide / Sync automatically ON/OFF sliders, Cancel,
and Authorize device. Authorization still confirms the exact full device identity
and account. Explicit authorizer trust remains required in Advanced; signing in
and registering a device never infer it. Manage provides nickname editing,
permissions, automatic scheduling, status, Sync now, Advanced, and Revoke device.
Permission changes require explicit signed consent, not merely a changed widget.

Nicknames are cosmetic, DPAPI-protected local profile/account/device metadata.
They persist across restarts, can be renamed without reauthorization, and do not
replace identity. Duplicate names get a list ordinal; all commands still use the
exact Peer ID. Names are not synchronized to another profile/device in this pass.

The normal workflow auto-loads when the panel opens and after commands. It uses
the existing bounded history worker for authenticated metadata refresh while the
panel is open, including sign-in/reconnect and registered-device changes. No
second sync/retry owner is added. Manual Refresh is under Advanced only.

## Permissions and automatic behavior

Saved signed permissions are presented separately from effective permissions:
History Sync OFF pauses their use, but does not erase either toggle or grant.
Receive and Provide can be changed independently through existing signed grant
replacement. A non-revoked grant must retain one permission. Turning the last
permission OFF restores its switch and explains:

"At least one history permission is required while this device is authorized.
Revoke the device if you want to disable history access completely."

Cancel / Revoke device is explicit; zero-capability grants are not introduced.
Multiple-authority permissions are an aggregate; editing changes this device's
issued grant only. Another authority's grant is not silently overwritten or
revoked. Revoke cannot erase history already received.

Sync automatically is a new protected, account/local-device/source-scoped initial
selection preference (default ON), enforced by existing native initial-sync
selection. It grants no permission. OFF pauses an active download for that
source on this profile. Explicit Pause/completion intent is preserved; turn ON
then use Sync now to resume an already paused scan. It does not remotely change
the other device's scheduling preferences. Manual Sync now still works with
automatic selection OFF, subject to all native authorizations.

New local application operations 19–21 expose automatic-selection preference,
read-only active providing sessions, and saved grant capabilities. IDs 16–18
remain reserved for Android's existing attachment operations. These are local
application projections, not changes to peer/server wire format. Record contexts
use the existing digest to fit protected Storage key bounds and bind account,
local identity, and remote identity. Corrupt preference records fail closed.

## Truthful direction and progress

Receiving activity says Downloading history from a friendly device name; source
activity says Providing history to the native-authorized connected reader(s).
Providing reflects an active native source session, not a fabricated message ACK
or bytes uploaded to a plaintext server. A small sidebar activity button opens
the History settings page. Current reader message/conversation counters come
from the existing snapshot. Provider sent totals are not currently exposed.

Current native snapshots have no reliable total, so progress is indeterminate.
The presentation helper supports known totals and tests percentage arithmetic,
but the production UI never supplies an invented total. Idle is not claimed to
mean globally Up to date; Complete means the last authorized scan completed.
Offline is not inferred from absent device metadata. Raw diagnostic state is
available under Advanced. Historical attachment testing controls remain there;
normal attachment interaction stays in conversations.

## Validation / limitations

Focused tests cover nickname assignment/rename/reopen/account isolation,
independent signed permissions/reopen/revoke, zero-permission rejection without
mutation, same-account denial, stale metadata, automatic OFF selection/reopen,
corrupt preference fail-closed, saved-versus-effective permissions, status
translation, indeterminate/known-total progress, counters, rendered slider
template, separate sections, no normal device dropdown, collapsed Advanced,
and signed-out no-consent behavior. Existing archive security/provenance/history
storage, packaged archive Node/application, attachment/chat, mailbox, reliable
session, Windows build/import/version/updater, and Android build/JVM/ABI gates
are retained. Physical acceptance is still pending.

Android retains its existing settings presentation; shared initial-selection
policy remains compatible and Android builds/tests are checked. Future Android
UI parity can consume these same owners; no Kotlin history engine is introduced.

Archive cryptography, signed grant semantics, authorization, revocation,
generations, provenance, cursor semantics, original authorship, and transports
are unchanged. The only scheduling addition is the explicit local automatic
selection opt-out described above. Existing explicit pause/resume policy remains.

## Physical checklist

1. Sign in on two same-account devices and open History Sync. Verify local/remote
   separation and registration updates without manual Refresh.
2. In Advanced explicitly establish required authorizer trust on each device.
   Authorize the exact other device in New history device; assign Work VM.
3. Manage / rename it; restart and verify nickname and saved permissions.
4. Sync now; verify direction, indeterminate progress, genuine counters, and
   sidebar indicator. On the source verify the providing-session indication.
5. Change Receive/Provide independently, attempt last-permission OFF, then Cancel
   or explicitly Revoke. Unauthorized/revoked devices must remain denied.
6. Test automatic initial selection OFF/ON, explicit Pause/Sync now, restart,
   ordinary messaging/mailbox, and dev.73 attachment cards.
