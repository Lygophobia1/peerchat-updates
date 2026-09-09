# PeerChat v0.3-beta Milestone 3 Test Guide

Use this guide with two Windows PCs on the same private LAN. Test the packaged files only:

```text
releases\v0.3-beta\Client\PeerChat.Desktop.exe
releases\v0.3-beta\Server\PeerChat.Server.Admin.exe
releases\v0.3-beta\Server\PeerChat.Server.exe
```

Keep the Core DLL, config file, and icon beside the client executable. Do not run the copy under
`build` for this test.

## Reset manual development settings safely

The client stores its normal profile under `%LOCALAPPDATA%\PeerChat`. Do not delete that folder
for this test: it contains the persistent cryptographic peer identity, contacts, and encrypted
message history.

To reset only the discovery-server address, development certificate pin, and saved account session:

1. Close PeerChat on that PC.
2. Make a backup if desired:

```powershell
Copy-Item "$env:LOCALAPPDATA\PeerChat" "$env:USERPROFILE\Desktop\PeerChat-backup" -Recurse
```

3. Remove only these two files:

```powershell
Remove-Item "$env:LOCALAPPDATA\PeerChat\discovery-service.dat" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\PeerChat\discovery-session.dat" -Force -ErrorAction SilentlyContinue
```

The next launch has no saved endpoint override, so packaged `service-bootstrap.json` supplies the
service endpoint and the normal one-time development-server trust confirmation appears. Sign in
normally afterward.

If a manually saved peer endpoint is still listed, use the client's **Remove Contact** action. Do
not delete `peerchat.sqlite`; that would remove the local contact records, persistent identity, and
encrypted history together. After removing the old contact, find the person through **Find Friends**
and open their friend DM so the endpoint comes from authenticated discovery.

For a completely disposable fresh test profile, launch a separate copy with a temporary data
directory. This creates a new peer identity and has no access to your normal history:

```powershell
& ".\releases\v0.3-beta\Client\PeerChat.Desktop.exe" `
  --data-dir "$env:TEMP\PeerChat-M3-Test-A" --port 0
```

Use a different temporary directory for the second PC/profile. Do not use this disposable mode if
you need to test existing contacts or saved history.

## Before starting

1. Confirm both PCs are on the same private LAN and can reach each other.
2. Start `PeerChat.Server.Admin.exe` on the discovery-server PC and click **Start Server**.
3. Start the packaged PeerChat client on both PCs.
4. If Windows asks about **PeerChat P2P (Private)**, approve it on the private network. A later
   launch should reuse the rule without another prompt.
5. Use separate accounts. Sign in on both clients and make sure each account has a username.
6. Add each account as a friend and accept the request. The friend should show as online while the
   other client is running.

## Test 1 — Automatic friend to direct chat

On Client A:

1. Open **Messages** and select Client B in the Friends list.
2. A DM should open even if B is temporarily offline.
3. Watch the conversation header. Expected progress is **Finding peer**, **Connecting**, possibly
   **Awaiting peer verification**, then **Ready**.
4. If a trust prompt appears, compare the persistent peer fingerprint with the other PC through a
   trusted channel before accepting it.
5. When the state is **Ready**, type a message and press **Send**. The composer must enable without
   requiring another server login.
6. Reply from Client B. Confirm both directions show delivery acknowledgements and the messages
   appear in the same order.

## Test 2 — Discovery server stopped after connection

1. Leave the direct conversation connected.
2. Stop the server from Server Admin.
3. Send messages in both directions.
4. Existing direct chat must continue working. The discovery server being offline must not disable
   the composer or disconnect an already-authenticated direct session.
5. Restart Server Admin and confirm account/friend presence recovers after reconnect.

## Test 3 — Manual Direct P2P fallback

1. Stop the discovery server completely.
2. On one client open **Settings → Advanced → Direct P2P Connection**.
3. Enter the other PC's current LAN address and listening port (normally `7777`).
4. Connect, verify the peer identity when prompted, and open the conversation.
5. Send messages both ways. This proves the permanent server-independent fallback still works.

## Test 4 — Connection diagnostics

Try one deliberately invalid endpoint at a time. The header/status should identify the actual
condition instead of reporting every problem as a TLS error:

| Test | Expected state |
| --- | --- |
| Invalid hostname | DNS/hostname failure |
| Closed port | TCP refused or TCP timeout |
| Offline/unreachable PC | Offline / unreachable or TCP timeout |
| Different/untrusted peer identity | Awaiting peer verification or Identity mismatch |
| Valid trusted peer | Ready |

## Test 5 — Deterministic ordering

1. With both clients connected, send at least 10 short messages rapidly from Client A.
2. Send messages at the same time from both PCs.
3. Close one client, send several messages from the other, then reopen the closed client.
4. Allow queued messages and acknowledgements to finish.
5. Restart both clients and reopen the conversation history.
6. Compare the final order on both PCs. The order must match after delivery and after history
   reload. Do not judge order by the displayed wall-clock timestamp; timestamps are informational.

## Test 6 — Firewall reuse

1. Close and reopen the packaged client.
2. Confirm Windows does not ask for a second rule.
3. In an elevated PowerShell window, inspect the rule:

```powershell
netsh advfirewall firewall show rule name="PeerChat P2P (Private)" verbose
```

It should be inbound TCP for the current client executable, the current listener port, and the
Private profile only. Public-network access should remain disabled.

## What to report

Record:

- Windows version and which PC was A/B
- LAN address and listening port used
- whether automatic friend-to-DM connected
- which connection state appeared if it failed
- whether chat continued with the discovery server stopped
- whether manual Direct P2P worked
- whether both clients showed identical ordering after restart
- whether the firewall rule was reused without another UAC prompt
- any exact error text or screenshots

Do not send private keys, DPAPI data, passwords, session tokens, or private message logs.
