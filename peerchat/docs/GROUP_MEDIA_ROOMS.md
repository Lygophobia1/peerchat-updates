# Group media room-state/signaling foundation

This checkpoint adds **state only**, not group audio/video. Accepted Stage 1A–1C,
one-to-one calls/screens, TURN and transport ownership remain unchanged.

## Ownership and identity

`AccountDatabase` remains authoritative for group account/device membership.
`GroupMediaRooms` owns ephemeral room collections above the existing authenticated
discovery event sessions. It owns no socket, worker, timer, key, capture, encoder,
decoder, SessionTransportManager or reconnect scheduler.

Every mutation is admitted under the current roster database lock, after checking
the authenticated event session, registered device and current member state.
Invitations alone do not authorize rooms. No Friend/DM or additional GroupControl
authorization is granted. Names are presentation only.

Room IDs and the server epoch are opaque random GUID IDs, unrelated to names/routes.
Records retain creator account/device, participants/viewers by account/device and
the active sharer. Session bindings are server-internal and never disclosed.

## Lifetime

- Explicitly created empty rooms remain until closed; restart clears all rooms.
- Creator device or current group owner/admin may rename/close a room. Closing an
  occupied room removes all transient participation.
- Voice join moves an account atomically out of other voice rooms **in this group**.
  Multiple authorized devices may coexist in the same room; moving removes the
  account's other-room device memberships, without implicitly joining those devices.
  Different groups are independent. One participant is valid; there is no ringing.
- Screen rooms have one independently claimed sharer and many voluntary viewers.
  Creator and sharer need not match. Starting does not join voice. A viewer never
  implicitly joins voice. Stop by the sharer or room creator/group moderator clears
  viewers and leaves the room available. Multiple screen rooms may be active.
- Event-session close/replacement clears only memberships bound to that exact
  session, including sharer/viewer cleanup. Abrupt loss uses the existing 75-second
  heartbeat lease. Reconnect requires explicit rejoin; no resume token is invented.
- Roster removal automatically prunes voice/viewer membership. Removing a sharer
  stops that room and clears viewers. Other rooms remain independent.

## Wire and synchronization

`group_room` requests travel on the **existing authenticated events stream**.
Fields: `requestId`, `groupId`, `operation`, optional `roomId/kind/name`;
mutations require `epoch`, `revision`, and `rosterGeneration` from a snapshot.
Operations: `snapshot/create/rename/close/join_voice/leave_voice/start_share/
stop_share/join_viewer/leave_viewer`.

Replies are `group_room_result` with bounded result/operation and an authorized
snapshot where appropriate. Changes broadcast full `group_room_snapshot` frames
to currently authorized live devices, not unversioned UI deltas. A group has a
monotonic revision; no-op duplicate join/leave does not increment it. Requests with
stale revisions fail with a current snapshot, without blindly replaying a mutation.
The client ignores backwards revisions/roster generations and different epochs
on a live channel. Disconnect clears snapshots; a fresh channel accepts the new
server epoch and reacquires remembered group snapshots. Roster reconciliation
also requests snapshots and removes lost group targets. Group selection requests
a current snapshot. The developer panel's
Refresh covers missed-event reconciliation without normal polling.

Limits: 256 retained groups/server, 16 rooms/group, 64 devices/room, 256 aggregate
participation entries/group, 64 UTF-16 code units/192 strict UTF-8 bytes per name.
Control characters, invalid Unicode, foreign identity fields, wrong group-room
pairs, stale sessions/generations and nonmembers are rejected. Event framing
retains the existing 100,000-byte cap; no offline room history is persisted.
Client room requests are bounded to 256 queued/in-flight items; disconnect discards
them rather than silently rejoining. Ambiguous disconnect completion is visible.

## Client and diagnostics

`DiscoveryClient` exposes immutable `GroupMediaState/GroupRoom/GroupRoomDevice`
snapshots and owns request correlation on its existing event lifecycle.
WPF renders these, never declares membership or starts media. Group overflow
offers **Media Rooms (state only)**, with basic test controls; no final room UI
redesign is attempted. Group Voice/Screen Share diagnostics expose separate
collections, revisions, counts, sharer/local device identities, last mutation
and rejection. Snapshot strings are copied through existing DiagnosticsService
retention; one-to-one state is not attributed to groups.

## Validation and next boundary

`tests/test-group-media-rooms.ps1` covers deterministic lifecycle/security/device/
revision tests and three real authenticated TLS event channels, room convergence,
independent voice/viewing, disconnect/reconnect, and server roster removal.
Group authorization/key rotation, envelope/history/tamper, fanout/sender-name,
registration, voice and screen regressions remain required.

Future media coordination attaches endpoint mechanics to these IDs, not one global
group call. The room model contains no per-viewer capture/encoder instance.
Capture-once/encode-once fanout, audio transport/mixing, media endpoint authorization,
TURN allocations, SFU, AEC, room media errors and physical media tests are **not**
implemented by this state checkpoint. Deploy matching client/server for this API.

Deferred UI backlog remains: dark-theme text, Find People presence, DM/group redesign,
separate unread counts, right details pane, diagnostics in Settings, moderation UI,
and final multi-room lists.
