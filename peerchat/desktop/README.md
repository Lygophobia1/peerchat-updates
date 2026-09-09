# PeerChat WPF presentation

Build from the peerchat directory with `powershell -File scripts\build-desktop.ps1`.
Run `build\v0.3-beta\Client\PeerChat.Desktop.exe` for development, or package and run
`releases\v0.3-beta\Client\PeerChat.Desktop.exe`. Requires 64-bit Windows 10/11 and .NET
Framework 4.8 (included in current Windows versions). The build uses the installed
MinGW C++ compiler and Windows' Framework C# compiler; no NuGet packages or browser
runtime are needed. Copy the EXE, Core DLL and EXE.config together when distributing.

## Windows Firewall

The listening computer must allow the configured TCP port through Windows Firewall.
When Windows prompts on first launch, allow `PeerChat.Desktop.exe` for **Private
networks**. If there is no prompt, open **Windows Security → Firewall & network
protection → Allow an app through firewall → Change settings**, add the packaged
`PeerChat.Desktop.exe` if needed, and enable its **Private** checkbox.

As an administrator, an explicit port rule can be created with PowerShell:

```powershell
New-NetFirewallRule -DisplayName "PeerChat TCP 7777" -Direction Inbound -Protocol TCP -LocalPort 7777 -Action Allow -Profile Private
```

Replace `7777` with the port shown in **My Connection**. Firewall permission does
not configure router port forwarding, defeat NAT, or make a hostname resolvable;
use a LAN/VPN endpoint or **Show Details** when testing between PCs.

## Boundaries

- Main.xaml: layout, templates, colors, spacing, wrapping and resizing.
- App.cs: window lifecycle, dialogs, control bindings, safe clickable links.
- ChatViewModel.cs: dispatcher-owned conversation/message state and draft retention.
- CoreAdapter.cs: P/Invoke wrappers and public event DTOs.
- ../ui_bridge.cpp: C ABI forwarding into the original Node.
- ../node.cpp, tls.cpp, storage.cpp: existing services, unchanged by the migration.

There is one process containing the WPF presentation and C++ core. Network and
database work stays on Node's worker. No plaintext chat files, web service, browser
cache, or duplicate persistence system is introduced. The default data directory
is still `%LOCALAPPDATA%\PeerChat`, so the legacy identity/history is reused.
Do not run two GUIs against the same data directory at once.

## Validation

`tests\test-desktop.ps1` checks the actual ABI/view model against two nodes, including
new contact trust, bidirectional Unicode messages, acknowledgements, persistent
identity, contacts, restart, reconnect and encrypted history reload.

`tests\test-desktop-ui.ps1` stages the current packaged client under `build\tests` and uses those isolated test
profiles in two separate WPF processes. It exercises actual controls, DM/Groups,
draft retention, search, Copy My ID, dialog opening/cancellation, sending, receiving,
delivery state, older history and reconnect. It renders full and compact WPF views
to `build\tests\runtime` and closes the windows normally. Run the adapter test first.

DPAPI tests need the normal Windows account. They can fail in a restricted sandbox
because that account cannot access the user's cryptographic context.

The app also supports `--data-dir PATH`, `--port PORT`, and the diagnostic
`--capture PATH.png` (renders the real view after startup, then closes normally).
Capture mode never populates fake conversations. `--compact` uses the minimum size.

Groups and attachments remain explicitly unavailable. A missing/unreachable
endpoint is a direct-connect limitation; Show Details allows selecting the host/IP
included in Copy My ID without changing the listener or implementing NAT traversal.
