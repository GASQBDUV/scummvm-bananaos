#!/bin/bash
# Creates a Debian 13 (trixie) build chroot with an aarch64 cross toolchain.
# Debian trixie has glibc 2.41, the same as BananaOS 0.5.0.1, so the binaries do not need anything newer than the device has.
# Run as root on a Debian/Ubuntu host (or in WSL):  sudo ./setup-chroot.sh [/opt/trixie]
set -e
CHROOT=${1:-/opt/trixie}
export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -y -qq debootstrap >/dev/null
[ -f "$CHROOT/etc/debian_version" ] || debootstrap --variant=minbase trixie "$CHROOT" http://deb.debian.org/debian
mount --bind /dev "$CHROOT/dev" 2>/dev/null || true
mount -t proc proc "$CHROOT/proc" 2>/dev/null || true

chroot "$CHROOT" sh -c '
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update -qq
# a failing post-install script of an arm64 package (it tries to run an arm64 binary) is harmless here
apt-get install -y -qq git make pkg-config file ca-certificates crossbuild-essential-arm64 \
    libsdl2-dev:arm64 zlib1g-dev:arm64 libpng-dev:arm64 libfreetype-dev:arm64 libjpeg-dev:arm64 \
    libflac-dev:arm64 libvorbis-dev:arm64 libogg-dev:arm64 libmad0-dev:arm64 libdrm-dev:arm64 >/dev/null || true
ldd --version | head -1
aarch64-linux-gnu-gcc --version | head -1
'
mkdir -p "$CHROOT/work" "$CHROOT/out/lib"
echo "chroot ready: $CHROOT"
