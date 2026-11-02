#!/usr/bin/env python3
"""Builds the install zip: package/ (launchers, button map, split list) + the binaries from the chroot's /out.

    python3 package.py <dir with scummvm and lib/> <output.zip>

The zip is unpacked at the root of BananaOS' data partition (the one that holds roms/). Unix file modes are stored
explicitly, so the launchers and the binary stay executable after unzipping."""
import os
import sys
import zipfile

here = os.path.dirname(os.path.abspath(__file__))
package = os.path.join(here, "..", "package")
out_dir, target = sys.argv[1], sys.argv[2]


def add(z, src, arcname, mode):
    info = zipfile.ZipInfo(arcname.replace(os.sep, "/"))
    info.create_system = 3  # Unix
    info.external_attr = (0o100000 | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    data = open(src, "rb").read()
    if b"\r\n" in data and not src.endswith((".so", ".zip", "scummvm")) and ".so." not in src:
        sys.exit("CRLF line endings in " + src)
    z.writestr(info, data)


with zipfile.ZipFile(target, "w") as z:
    for root, _dirs, files in os.walk(package):
        for name in sorted(files):
            src = os.path.join(root, name)
            rel = os.path.relpath(src, package)
            add(z, src, rel, 0o755 if name.endswith(".sh") else 0o644)
    add(z, os.path.join(out_dir, "scummvm"), "roms/ports/ScummVM/scummvm", 0o755)
    extra = os.path.join(out_dir, "extra")  # on-screen keyboard pictures
    for name in sorted(os.listdir(extra)):
        add(z, os.path.join(extra, name), "roms/ports/ScummVM/extra/" + name, 0o644)
    lib = os.path.join(out_dir, "lib")
    for name in sorted(os.listdir(lib)):
        add(z, os.path.join(lib, name), "roms/ports/ScummVM/lib/" + name, 0o755)
print("wrote", target)
