# Windows relay socket retirement regression

Physical dev.59 observation: a first voice call succeeds; after idle route
retirement, another call oscillates between Connecting and Retrying with backoff.
Restarting both clients restores establishment. This does not prove a TURN defect.

## Proven adapter defect

1. SessionTransportManager retires the idle relay transport.
2. Node reaps the connection, marks its RelayPipe closed, and removes it.
3. Node::takeRelayOutput returns -1 when that pipe is closed or absent.
4. RelayTransport.WriteLoop previously returned without closing its TcpClient.
5. RelayTransport.Run could remain blocked reading the server TLS stream, retaining
   the server attachment even though the native session owner had retired it.

The server rejects duplicate endpoint bindings with relay_duplicate_attachment.
It currently sends relay_demand before this rejection. The physical UI timing log
shows repeated incoming demands; the Windows forced refresh can reset retry due
time. These facts explain a plausible amplification mechanism, but the exact
server error for every physical attempt was not captured. Do not claim complete
per-attempt physical attribution from this evidence.

## Narrow correction

The existing relay writer closes its own TcpClient in finally on every exit,
including the native -1 retirement signal. This interrupts the paired reader and
allows normal server attachment cleanup. A bounded reason distinguishes native
retirement from writer exceptions. No new lifecycle/retry owner, protocol change,
permanent connection, trust exception, or media/TURN change is introduced.

## Automated evidence

tests/test-relay-retirement.ps1 compiles the production RelayTransport against
test-only native/discovery boundaries. A real loopback TCP connection proves that
native retirement releases the socket before Dispose is called. The original
code fails by read timeout; the corrected code passes 100 cycles alternating
native retirement and owner exceptions. This is a socket-lifetime regression,
not a full authenticated server/TLS or physical media acceptance test.

tests/test-relay-requests.ps1 covers 1,000 manager-owned relay lifecycles, scoped
authorization, retry, revocation, stale attempts, idle demand and retirement.
tests/test-network-presence.ps1 covers presence ownership and generation checks,
offline composer state, and active stop/drain/restart mailbox lifecycle.

## Physical retest

Use the new Windows candidate on both clients without changing the Pi server.
Call, end, wait for normal zero-peer idle retirement, then call again. Repeat at
least five times; repeat with screen sharing. Confirm one winning authenticated
route, no indefinite duplicate/backoff churn, and zero idle peers afterward.
Keep successful and failed diagnostics. The startup archive rejection popup and
the requested comprehensive per-attempt diagnostic readout remain separate work.
