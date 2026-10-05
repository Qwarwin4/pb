#!/bin/sh
set -eu

dist=$(cd "$1" && pwd)
arch=$(uname -m)
t=$(mktemp -d)

if command -v apt-get >/dev/null; then
    apt-get install -y "$dist"/pb_*_"$(dpkg --print-architecture)".deb
elif command -v dnf >/dev/null; then
    dnf install -y "$dist"/pb-*."$arch".rpm
    rpm -V pb
elif command -v zypper >/dev/null; then
    zypper --non-interactive install --allow-unsigned-rpm "$dist"/pb-*."$arch".rpm
    rpm -V pb
elif ! command -v pb >/dev/null; then
    install -Dm755 "$dist"/pb-*-"$arch" /usr/local/bin/pb
fi

pb --version
pb lang en
pb lang ru
pb repair

mkdir -p "$t/app"
cat > "$t/app/build.toml" <<EOF
name = "hello-pb"
version = "1.2.3"
description = "pb smoke test"
license = "GPL-3.0-or-later"
binary = "hello-pb"
EOF
printf '#!/bin/sh\necho hello-from-pb\n' > "$t/app/hello-pb"
chmod +x "$t/app/hello-pb"

pb build "$t/app" --binary "$t/app/hello-pb" --out "$t/out" --deb --rpm --appimage
ls -la "$t/out"

test "$("$t"/out/hello-pb-1.2.3-*.AppImage --appimage-extract-and-run)" = hello-from-pb

if command -v apt-get >/dev/null; then
    apt-get install -y "$t/out/hello-pb_1.2.3_all.deb"
    test "$(hello-pb)" = hello-from-pb
    apt-get remove -y hello-pb
elif command -v dnf >/dev/null; then
    dnf install -y "$t/out/hello-pb-1.2.3-1.noarch.rpm"
    test "$(hello-pb)" = hello-from-pb
    rpm -V hello-pb
    dnf remove -y hello-pb
elif command -v zypper >/dev/null; then
    zypper --non-interactive install --allow-unsigned-rpm "$t/out/hello-pb-1.2.3-1.noarch.rpm"
    test "$(hello-pb)" = hello-from-pb
    zypper --non-interactive remove hello-pb
fi

rm -rf "$t"
echo smoke-ok
