#!/bin/sh
# Builds ScummVM (SDL backend, aarch64) and collects the shared libraries BananaOS does not have. Runs INSIDE the chroot:
#   chroot /opt/trixie sh /root/build-scummvm.sh      (copy this file to /opt/trixie/root/ first)
# Result: $OUT/scummvm (stripped) and $OUT/lib/*.so*  (WORK and OUT default to /work and /out)
set -e
WORK=${WORK:-/work}
OUT=${OUT:-/out}

# the upstream commit the release was built from (ScummVM 2026.4.1git)
SCUMMVM_COMMIT=${SCUMMVM_COMMIT:-ec80f5e5ca334ea845b1e823aa3115245d64d279}
# engines to build; "scumm" alone covers Monkey Island, Day of the Tentacle, Sam & Max, Full Throttle, The Dig, Loom ...
ENGINES=${ENGINES:-scumm}

mkdir -p "$WORK" "$OUT/lib"
cd "$WORK"
if [ ! -d scummvm/.git ]; then
    git init -q scummvm
    git -C scummvm remote add origin https://github.com/scummvm/scummvm.git
fi
cd scummvm
for i in 1 2 3 4 5; do
    git -c http.version=HTTP/1.1 fetch -q --depth 1 origin "$SCUMMVM_COMMIT" && break
    echo "fetch failed, retrying"; sleep 5
done
git checkout -q FETCH_HEAD
git log --oneline -1

export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
export PKG_CONFIG_PATH=$PKG_CONFIG_LIBDIR

DISABLE_ALL="--disable-all-engines"
ENABLE=""
for e in $(echo "$ENGINES" | tr ',' ' '); do ENABLE="$ENABLE --enable-engine=$e"; done
./configure --host=aarch64-linux-gnu --backend=sdl $DISABLE_ALL $ENABLE --disable-debug --enable-optimizations \
    --opengl-mode=none --enable-flac --enable-vorbis --enable-mad --enable-vkeybd 2>&1 | tail -15

# configure picks the native binutils names even when cross compiling: point them at the cross ones
sed -i -E 's/^AR := ar cr/AR := aarch64-linux-gnu-ar cr/; s/^RANLIB := ranlib/RANLIB := aarch64-linux-gnu-ranlib/; s/^STRIP := strip/STRIP := aarch64-linux-gnu-strip/; s/^AS := as/AS := aarch64-linux-gnu-as/' config.mk

make -j"$(nproc)" 2>&1 | tail -5
aarch64-linux-gnu-strip -o "$OUT/scummvm" scummvm
file "$OUT/scummvm"

# shared libraries that BananaOS 0.5.0.1 does not ship (the others it needs are on the device: SDL2, ALSA, libpng, zlib,
# freetype, libstdc++, vorbis, ogg)
# the on-screen keyboard pictures (ScummVM finds them through --extrapath)
mkdir -p "$OUT/extra"
cp "$WORK/scummvm/backends/vkeybd/packs/vkeybd_default.zip" "$WORK/scummvm/backends/vkeybd/packs/vkeybd_small.zip" "$OUT/extra/"

cd /usr/lib/aarch64-linux-gnu
for f in libjpeg.so.62 libsndio.so.7 libbsd.so.0 libmd.so.0 libFLAC.so.14 libmad.so.0; do cp -L "$f" "$OUT/lib/"; done
ls -la "$OUT" "$OUT/lib"
echo BUILD-SCUMMVM-DONE
