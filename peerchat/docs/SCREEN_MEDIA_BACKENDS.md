# Reusable screen-media mechanics

`ScreenShareMedia` remains the existing one-to-one route/orchestration adapter.
Logical share identity, negotiated backend and media recovery ownership remain
with the existing manager. This extraction does not change backend negotiation.

`screen_media_backend.hpp` separates:

- `ScreenCaptureEncodeBackend`: one native capture/encoder lifetime and bounded
  encoded-AU buffer. `pop()` consumes once; `data()` is borrowed until the next
  pop/close and may feed several authorized routes synchronously. A destination
  must not independently pop the shared source. Packetization/continuity belong
  to each route, not this source.
- `ScreenReceiveDecodeBackend`: the existing H.264 reorder/bootstrap/recovery
  algorithm, complete-AU decoder submission and native decode/presentation
  lifetime. It has no RTC/session/reconnect dependency. Its caller serializes
  receive/tick/state reads and drains callbacks before close. Final counters
  survive close; opening a new lifetime resets them.
- `ScreenMediaApi`: the unchanged adjacent-DLL load/import policy. No WebRTC or
  Media Foundation C++ objects cross the ScreenMedia C ABI.

The production aliases use the existing Windows ScreenMedia implementation.
Template adapter substitution exists for deterministic mechanics tests, not for
another product transport or application state owner.

Native Windows still uses the accepted hardware-encoder path; WebRTC
Compatibility still uses its accepted software H.264 path. WGC, D3D11,
Media Foundation, H.264 algorithms and the ScreenMedia ABI are unchanged.
Callback detachment/draining stays in the route adapter before backend teardown.

Validation: `tests/test-screen-shared-backend.ps1 -DevelopmentDirectory <build>`
checks one-source/AU reuse, decoder lifetimes/recovery and the accepted Native,
Compatibility and mixed local transport cases without building dependencies.
`tests/test-h264-rtp.ps1` retains byte-exact pinned packetizer/recovery coverage.
These local tests do not establish physical TURN/group fanout acceptance.
