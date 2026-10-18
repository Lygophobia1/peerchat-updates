#!/bin/sh
# Isolated development fixture, NOT a PeerChat deployment or system service.
# Ubuntu packages are downloaded/unpacked only under the test dependency path.
set -eu
root=/home/peerchatbuild/deps/turn-fixture
cd "$root"
for archive in ./*.deb; do dpkg-deb -x "$archive" root; done
export LD_LIBRARY_PATH="$root/root/usr/lib/x86_64-linux-gnu"
ldd "$root/root/usr/bin/turnserver"
sha256sum ./*.deb
address=$(hostname -I | awk '{print $1}')
# WSL NAT interface only. Fixed nonsecret fixture credential, bounded lifetime,
# no TCP/TLS listeners, no admin CLI, no live databases, no physical device.
exec timeout 900 "$root/root/usr/bin/turnserver" -c /dev/null \
  --listening-ip="$address" --relay-ip="$address" --listening-port=34790 \
  --min-port=34800 --max-port=34900 --realm=peerchat-test \
  --user=peerchat-test:fixture-only --lt-cred-mech --fingerprint \
  --no-tcp --no-tls --no-dtls --no-cli --no-multicast-peers \
  --pidfile="$root/fixture.pid" --log-file=stdout --simple-log \
  --total-quota=32 --user-quota=32 --relay-threads=2
