# PeerChat discovery server

This is the Milestone 1 account and discovery service. It stores account names, PBKDF2-SHA256
password verifiers, public device identity references, hashed session tokens, and short-lived
presence timestamps in one SQLite database. It does not store private device keys, chat messages,
or local chat history.

## Build and run

Build with `..\scripts\build-server.ps1`. The server needs a TLS certificate with a private key. For
development, create one in the CurrentUser certificate store and start the service using its
thumbprint:

```powershell
$certificate = New-SelfSignedCertificate -DnsName localhost -CertStoreLocation Cert:\CurrentUser\My
.\build\v0.3-beta\Server\PeerChat.Server.exe --port 7443 --database .\build\v0.3-beta\Server\peerchat-server.sqlite --certificate-thumbprint $certificate.Thumbprint
```

The desktop client needs the server's SHA-256 certificate fingerprint, not its SHA-1 thumbprint.
For a development certificate:

```powershell
($certificate.GetCertHash([System.Security.Cryptography.HashAlgorithmName]::SHA256) | ForEach-Object { $_.ToString('X2') }) -join ''
```

For a longer-lived hosted service, provide a PFX instead:

```powershell
.\build\v0.3-beta\Server\PeerChat.Server.exe --port 7443 --database .\build\v0.3-beta\Server\peerchat-server.sqlite --certificate .\peerchat-server.pfx --certificate-password '<password>'
```

The server logs startup and storage failures but deliberately never logs passwords, session
tokens, private keys, or private message text. Put a trusted publicly reachable TLS certificate
and the pinned SHA-256 fingerprint into your deployment process before exposing the service beyond
development testing.

## Protocol scope

The TLS protocol uses a versioned, four-byte-length-framed JSON request. Implemented requests are
`register`, `login`, `presence`, and `search`. Later friend, rendezvous, NAT, relay, and update
operations will add new request types; they are not part of this milestone.

Run `..\tests\test-discovery.ps1` for the isolated two-client TLS test. It generates a temporary
CurrentUser development certificate and tests persistence by restarting the service.

## Local administration UI

Run `..\scripts\build-server-admin.ps1` and open `..\build\v0.3-beta\Server\PeerChat.Server.Admin.exe` for the
development admin window. It starts `PeerChat.Server.exe` as a child, displays its pinned
development certificate fingerprint, shows bounded operational logs and account/presence metrics,
and uses a local named pipe to query status or request graceful shutdown. The headless server stays
independent and can continue running when the Admin window closes.
