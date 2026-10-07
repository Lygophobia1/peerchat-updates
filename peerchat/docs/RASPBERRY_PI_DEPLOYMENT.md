# Raspberry Pi deployment draft

NOT PRIVILEGED DEPLOYMENT ACCEPTANCE. ARM32 dev.33 manual runtime/parity passes
on the restricted Pi test account; see LINUX_ARM32_VALIDATION.md. No systemd
service was installed. True Linux x64/ARM64 runtime validation remains pending.

For a future ARM64 deployment, use a supported 64-bit glibc Linux distribution on ARM64 hardware, .NET runtime
10.0.12 (or a subsequently tested security update), libsqlite3.so.0 and the
distribution's required .NET/OpenSSL/ICU dependencies. The candidate package is
framework-dependent; it does not contain .NET itself. Follow the official distro
installation instructions: https://learn.microsoft.com/dotnet/core/install/linux.

The existing useful Pi runs 32-bit Raspbian; do not reimage it. Its linux-arm
dev.33 package is self-contained and uses existing ARM32 OS libraries. For a
future self-contained service use its apphost in ExecStart, not /usr/bin/dotnet.
The generic service template below remains framework-dependent. Privileged
service installation requires separate approval; coturn stays independent.

Suggested administrator-managed layout:
- /opt/peerchat/releases/4.0.2-dev.32: root-owned immutable runtime files
- /opt/peerchat/current: administrator-selected release link
- /etc/peerchat/server-config.json: explicit configuration, restricted access
- /etc/peerchat/secrets: PFX/password, TURN REST secret and optional FCM credential
- /var/lib/peerchat: SQLite database, WAL and SHM
- /var/log/peerchat: bounded application logs; console also goes to journald

Use a dedicated unprivileged peerchat user/group, no interactive login. Data/log
directories should be 0700; service-readable secret files 0600, owned by peerchat
and not leaf symlinks. Set umask 0077. Keep secret ancestors administrator-owned.
PFX and secret files are never part of runtime ZIPs. Keep certificate password in
CertificatePasswordFile rather than command-line arguments. Do not use Windows
DPAPI blobs or CurrentUser thumbprints on Linux.

The sample service and config under server/portable/deployment are templates only.
Review local dotnet path, hostname, certificate, ownership and directories before
any installation. Use systemd SIGTERM with TimeoutStopSec=40, longer than the
bounded application/push drain. Inspect journalctl and application logs without
copying credentials. Windows ServerAdmin/WPF is not a Linux dependency.

Keep coturn as a separately supervised process. PeerChat only issues short-lived
REST credentials using the configured restricted shared-secret file. Neither
process should own/restart the other. TURN ports/firewall and shared secret need
explicit administrator setup; no TURN/SFU topology changes are introduced here.

Prefer an SSD or high-endurance SD and reliable power. Do not lower FULL
synchronous durability to save writes. Keep the database and its WAL/SHM on a
local filesystem with correct locking; avoid network-share deployment.

Use SQLite online backup/VACUUM INTO with checked completion or a verified
stopped-service backup. Copying only a live main DB while ignoring WAL can lose
committed data. Test restore/integrity in isolation, including mailbox ciphertext,
receipts, account/device records and external configuration/secret recovery.
References: https://www.sqlite.org/backup.html and https://www.sqlite.org/wal.html.

Updates are administrator-controlled: stop/drain, back up SQLite coherently,
install an immutable release, select it, restart and run health/parity checks.
Do not use the Windows desktop updater for the daemon. Do not roll back a database
blindly after future schema changes. No deployment, migration, publication or
production release is authorized by these draft instructions.
