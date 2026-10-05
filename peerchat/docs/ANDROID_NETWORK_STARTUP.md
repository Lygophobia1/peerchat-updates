# Android network critical path (no-phone optimization)

## Measured baseline, before request consolidation

The existing production MessagingBackend (not MainActivity) serially performs:

```
protected credential + opaque Profile
  -> remember_challenge -> Keystore signature -> remember_resume
  -> session_info -> presence -> friend_state
  -> native authorized friends/presence + conversation/history projection
  -> native relay requests -> outer server TLS/rendezvous
  -> native peer TLS/Hello/trust -> native DeliveryManager -> ACK/history
```

| Request | Required/dependencies | Execution / bound | Optimization boundary |
| --- | --- | --- | --- |
| remember_challenge | Credential/device proof | Serial blocking; cancellable socket, 10 s operation + wake deadline | Required fresh anti-replay challenge |
| remember_resume | Signed original challenge | Serial after challenge; same bounds | Required device-bound authentication |
| session_info | Authenticated response already identifies account | Serial after resume; same bounds | Redundant account check; authoritative presence also checks account/device |
| presence | Valid session, owned device | Serial; same bounds | Can combine with bounded authorized bootstrap |
| friend_state | Valid session, authorized contact/device state | Serial; same bounds | Can combine with bootstrap; not replace with stale local trust |
| push_register | Authenticated device, token changed | Deferred after native servicing but still blocks coordinator | Optional; must move off native servicing critical path |
| relay_attach | Native owner requests authorized remote device | Per-route worker, pairing deadline 20 s + lifetime deadline | Already concurrent across routes, no new retry owner |
| events | Authenticated device; existing server lease/heartbeat channel | API exists but Android backend does not consume it yet | Primary updates + lightweight heartbeat; bounded reconcile remains |
| group/profile/diagnostic metadata | None in current Android messaging bootstrap | No request | Do not invent work to defer |

Startup/wake uses five pre-route server round trips (challenge, resume, account,
presence, friendship). Device data arrives in friend_state; there is no separate
device request. No history is fetched from the server: native profile owns it.
Optional push registration adds a sixth request. Relay pairing is independent.

On the locked Motorola trace each request took roughly 6 s: 3.5-3.7 s context
preparation, 2.3-2.7 s TLS, tens of milliseconds TCP/server response. Five serial
requests exhaust the 30 s budget before native relay startup. Unlocked typical
requests were 1.5-2.2 s, not a production timing guarantee. Scoped reuse of pinned
algorithm providers is implemented; fresh SSLContext and trust checks remain.
Actual locked performance after that correction still requires physical testing.

Existing safety improvements: native projection occurs inside refresh;
native event/session servicing precedes periodic refresh; raw socket closure
enforces the wake lifetime; actual blocked-worker termination is host-tested.
Periodic refresh currently performs presence+friend_state every 5 s after the
last refresh completes. This is the next steady-state optimization target.

## Target minimal critical path

Auth challenge -> signed resume -> one versioned authoritative bootstrap:
owned-device presence + bounded friend/device state -> immediately install native
authorization/project -> session owner starts route -> delivery owner drains.
Event subscription and optional push maintenance must not gate native servicing.
Events primary, bounded reconciliation on loss/reconnect/missed state; no UI
collector activates networking. No message mailbox or plaintext FCM is added.

## Send/local echo observation

Current Send waits for the serialized backend gate, native durable send, and
native snapshot projection. A concurrent blocking server refresh can delay gate
acquisition. Push hint is queued after durable send, not before it. Instrument
gate/enqueue/projection/route/ACK separately; do not claim new physical timings
without the phone. Optimistic Pending UX remains a later QOL checkpoint: no
Kotlin outbox and Delivered only from native ACK.

## Evidence status

Before/after round-trip counts and host timings must be updated after the
production-path regression passes. No locked/Doze acceptance is inferred from
host tests or charging screen-off observations. No phone use in this pass.
