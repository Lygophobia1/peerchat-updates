# Two-PC text milestone

This is the first test that needs another Windows computer. Use a LAN or private
VPN first. Internet testing may require forwarding the listening TCP port through
the host router.

1. Copy the whole `releases\v0.3-beta\Client` folder to each computer and launch
   `PeerChat.Desktop.exe`. Keep its Core DLL, icon, and EXE.config alongside it. Windows
   10/11 x64 with .NET Framework 4.8 is required. To build locally, use
   `scripts\package-v0.3-beta.ps1` and run `releases\v0.3-beta\Client\PeerChat.Desktop.exe`.
2. On the listening computer, open **My Profile**, choose a display name and a
   free port, and save. When the listener becomes ready, PeerChat requests normal
   Windows elevation to create or update its one `PeerChat P2P (Private)` inbound
   TCP rule. Approve that prompt. It is scoped to the packaged executable and the
   **Private** network profile only. If the prompt was denied, the Connection card
   reports that inbound P2P may be blocked; use the Firewall section of
   `../README.md` to add the same Private-only exception manually.
3. On the host computer, choose **Copy My ID** and send the resulting
   `P2PCHAT1:...` text through a trusted channel. It contains the configured
   hostname/IP, port, and public peer identity reference; it never contains a
   private key. The default hostname must be resolvable from the other PC; use
   **Show Details** to choose your LAN/VPN IPv4 address if needed. Reachability
   across the Internet still requires a usable direct route/forwarded port.
4. On the second computer, choose **+ Add Friend**, paste the ID, and submit it.
   The contact is saved immediately and connection is attempted even if the host
   is temporarily offline.
5. For troubleshooting, **+ Connect manually** remains available for entering an
   IP/hostname and port directly.
6. Compare the displayed peer ID with the host over the trusted channel. Choose
   **Yes** only when it matches.
7. Send Unicode text containing `https://example.com/`. Confirm it appears on both
   computers and that clicking the link opens the default browser. Incoming text is
   displayed as text; it is never executed as HTML.
8. Close one application, send a message from the other, restart the closed app,
   and verify that the queued message arrives. Close and reopen both apps to verify
   their local history remains available.
9. Restart the listening client and confirm that Windows does not ask to create a
   second firewall rule. If testing a changed listener port, save the new port in
   **My Profile**, approve the update prompt, and verify the same named rule now
   permits the new TCP port.

Record the result in `../PROJECT_STATUS.md`: Windows versions, LAN/VPN or forwarded
port, whether reconnect worked, and any firewall or identity-verification issues.
Do not send a private identity key or DPAPI database through chat; the peer ID is
safe to compare, while the Windows profile and its private key must stay local.
