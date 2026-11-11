# Third-party software in the release zip

The zip contains the ScummVM program and six shared libraries. Everything is built from or taken unchanged from the sources below
(Debian 13 "trixie" packages, for aarch64). No source file of these projects was modified.

| Component | Version | Licence | Source |
|---|---|---|---|
| ScummVM | 2026.4.1git, commit `ec80f5e5ca334ea845b1e823aa3115245d64d279` | GPL-2.0-or-later | <https://github.com/scummvm/scummvm/tree/ec80f5e5ca334ea845b1e823aa3115245d64d279> |
| libjpeg-turbo (`libjpeg.so.62`) | 1:2.1.5-4 | IJG / BSD-3-Clause / Zlib | <https://packages.debian.org/source/trixie/libjpeg-turbo> |
| sndio (`libsndio.so.7`) | 1.10.0-0.1 | ISC | <https://packages.debian.org/source/trixie/sndio> |
| libbsd (`libbsd.so.0`) | 0.12.2-2 | BSD-style (several) | <https://packages.debian.org/source/trixie/libbsd> |
| libmd (`libmd.so.0`) | 1.1.0-2 | BSD / ISC / public domain | <https://packages.debian.org/source/trixie/libmd> |
| FLAC (`libFLAC.so.14`) | 1.5.0+ds-2 | BSD-3-Clause | <https://packages.debian.org/source/trixie/flac> |
| libmad (`libmad.so.0`) | 0.15.1b-11 | GPL-2.0-or-later | <https://packages.debian.org/source/trixie/libmad> |

The data files in `roms/ports/ScummVM/extra/` (`*.dat`, `queen.tbl`, `sky.cpt` ...) are ScummVM's `dists/engine-data` (GPL-2.0-or-later,
some with their own licences, see the README in that folder), unchanged; the engines for Drascula, Kyra, Hugo, Queen, Lure, Sky and others
refuse to start without them.

The menu theme `scummbanana.zip` is ScummVM's *scummremastered* theme (GPL-2.0-or-later) recolored, with the **Inter** font
(`Inter-Regular.ttf`, `Inter-Bold.ttf`; SIL Open Font License 1.1, licence text in `roms/ports/ScummVM/extra/Inter-LICENSE.txt`,
<https://github.com/rsms/inter>). Its colours follow Banana Player's Midnight theme. `gui-icons.dat` is built with ScummVM, unchanged.

`banana-second.so` is this repository's own code (GPL-3.0-or-later).

ScummVM links libmad (GPL), so the zip as a whole is distributed under the GPL. To get the exact sources: the commit above for
ScummVM, the Debian source packages above for the libraries (the version numbers are those of the build chroot, see
[build/setup-chroot.sh](build/setup-chroot.sh)), and this repository for the rest.

The libraries the zip does **not** carry because bananaOS 0.5.0.1 already has them: SDL2, ALSA, libpng, zlib, FreeType,
libstdc++, Ogg and Vorbis, libdrm.
