#!/bin/sh
# Builds the second-screen shim. Runs INSIDE the chroot (copy src/banana-second.c to /root/ first):
#   chroot /opt/trixie sh /root/build-shim.sh
# Result: /out/lib/banana-second.so
set -e
mkdir -p /out/lib
aarch64-linux-gnu-gcc -O2 -Wall -shared -fPIC -I/usr/include/SDL2 -I/usr/include/libdrm \
    /root/banana-second.c -o /out/lib/banana-second.so -ldl -ldrm
aarch64-linux-gnu-strip /out/lib/banana-second.so
aarch64-linux-gnu-readelf -d /out/lib/banana-second.so | grep NEEDED
ls -la /out/lib/banana-second.so
