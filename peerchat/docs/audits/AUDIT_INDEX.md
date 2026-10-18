# PeerChat audit index

## Android voice completion (in progress)

- [Architecture, security findings and completion sequence](voice/2026-10-17-architecture-and-completion-plan.md) — real shared foundation, reproduced consent/mute fixes, group boundaries and genuine receive validation gate.
- [Android application readiness](voice/ANDROID_APPLICATION_READINESS.md) — permissions, foreground ownership, routing and safe integration order.
- [Pinned Android receive dependency](voice/ANDROID_RECEIVE_DEPENDENCY.md) — Linux build prerequisite, exact pin, unvalidated build recipe and ABI/runtime/license gates.
- [Voice implementation backlog](voice/IMPROVEMENT_BACKLOG.md) — remaining work and acceptance boundaries; not a completed voice package.

## Device authorization

- [Persistent shared device names, dev.180](2026-10-17-device-display-names.md) — display-only labels across Device Manager, History Sync and receipt rows; explicitly approved sharing with accepted contacts, privacy/compatibility tests and packages.
- [Windows Device Manager authorization audit](2026-10-17-device-manager-authorization.md) — preserved pre-implementation authority analysis and approval boundary.
- [Device Manager dev.179 implementation and validation](2026-10-17-device-manager-implementation.md) — approved scoped proof, non-destructive revocation, Windows Settings, Android reconciliation, cache/history preservation and deployment safety. Dev.178 remains available.

## Networking

- [Independent dev.176 networking and Android lifecycle audit](networking/2026-10-17-dev176-independent-audit.md) — audit only; cache readiness reproductions, reconnect interpretation, lifecycle comparison and repair plan. Production changes await approval.
- [Networking improvement backlog](networking/IMPROVEMENT_BACKLOG.md).
- [Focused test instructions and evidence](networking/dev176-audit-tests.md).

## Prior reports retained unchanged

- [Networking concurrency audit](../NETWORKING_CONCURRENCY_AUDIT_20261017.md).
- [Dev.175 latency corrections](../NETWORKING_LATENCY_DEV175.md).
- [Dev.176 delivery confirmation and deployment](../DELIVERY_CONFIRMATION_DEV176.md).
