# PeerChat

PeerChat is a Windows-first encrypted chat project with a C# WPF client, a C-compatible adapter,
and a C++ direct peer-to-peer core. A separate C# discovery server provides accounts, presence,
and username search; private messages and private device keys stay off that server.

## Current builds

The frozen known-good fallback is `releases\v0.1-beta`. Current development is v0.3-beta:

- `releases\v0.3-beta\Client\PeerChat.Desktop.exe`
- `releases\v0.3-beta\Server\PeerChat.Server.Admin.exe`
- `releases\v0.3-beta\Server\PeerChat.Server.exe`

Build and package from the project root:

```powershell
.\scripts\package-v0.3-beta.ps1
```

Temporary compiler and test files stay under `build`. Do not run executables from `build` when
testing a packaged release.

## Direct P2P

Each client creates a persistent cryptographic identity and listens for peers. Use
**Settings → Advanced → Direct P2P Connection** for a hostname/IP and listening port, or use the
advanced Copy Peer ID bundle. The first connection requires peer-identity verification. Once a
peer is trusted, contacts, endpoints, queued messages, acknowledgements, and encrypted local
history persist on that PC.

Direct P2P does not require the PeerChat discovery server. LAN access may require allowing the
packaged client through Windows Firewall on Private networks. Internet access still requires a
reachable endpoint, VPN, or port forwarding until later rendezvous/NAT/relay milestones.

See [two-PC testing](docs/TWO_PC_TEST.md) for the detailed checklist.

For the current Milestone 3 automatic friend-to-chat, diagnostics, server-independent fallback,
ordering, and firewall validation, use the [Milestone 3 test guide](docs/MILESTONE3_TEST.md).

### Windows Firewall

When the client starts its listener, it checks for the `PeerChat P2P (Private)` inbound TCP rule.
If the rule is missing or the configured listener port changed, Windows may show a normal UAC
prompt so the client can create or update a rule scoped to the packaged PeerChat executable and
the Private network profile. PeerChat continues to start if the prompt is denied; the Connection
card will say that inbound P2P may be blocked. Public-network firewall access is not opened
automatically. If needed, allow `releases\v0.3-beta\Client\PeerChat.Desktop.exe` through Windows
Defender Firewall for Private networks, then restart PeerChat. The rule is reused on later launches
and is updated when the listening port changes rather than duplicated.

## Account and discovery server

Milestone 1 and the first friend-system slice are complete: account creation/login, persistent
sessions, public device registration, presence, username discovery, friend requests, friendships,
and debounced prefix suggestions are implemented. Start the normal home/development server with
`PeerChat.Server.Admin.exe`; the headless server remains usable without the Admin UI. Runtime
configuration, database, and bounded logs are under
`C:\ProgramData\PeerChat\Server`; the development TLS private key stays in `CurrentUser\My`.

Current v0.3-beta development is focused on automatic direct friend-to-chat connectivity, Windows Firewall
integration, and deterministic message ordering. Friend-request cards are backed by the server
state; direct P2P remains usable without the discovery service.

## Updates

The package includes a standalone updater. In **Settings → Updates**, Release is the normal
installation-wide channel and Development is an explicit pre-release channel; neither changes a
profile, its history, or account session. An update is downloaded with resume support, verified,
staged, applied only after PeerChat exits, and rolled back if the replacement does not complete
native profile/core startup.

See [updater/README.md](updater/README.md) for the feed, Development test, signing-key backup, and
exact-byte Release-promotion workflow. Release updates require a manifest signed by an embedded
trusted key and remain fail-closed for unsigned or invalid packages.

## Tests

```powershell
.\scripts\run-tests.ps1
.\tests\test-desktop.ps1
.\tests\test-desktop-ui.ps1
.\tests\test-discovery.ps1
```

Tests use synthetic identities, loopback endpoints, isolated databases, and output under
`build\tests`. They do not use message plaintext or private keys in server logs.

See [ARCHITECTURE.md](ARCHITECTURE.md), [PROJECT_STATUS.md](PROJECT_STATUS.md), and the
[development goal](PEERCHAT_DEVELOPMENT_GOAL.md) for the current design and milestone boundary.
