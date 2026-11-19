#!/usr/bin/env python3
"""Makes the update bundle the radio installs by itself (rootfs/usr/sbin/x6100-update): every file the GUI package
puts in the buildroot target (its CMake install_manifest.txt: the GUI, its rootfs files, its data), a manifest with
a SHA-256 per file, and an ECDSA P-256 / SHA-256 signature of the manifest.

  python3 tools/update/mkbundle.py --target <buildroot target dir> \\
      --manifest <build/x6100-gui-*/install_manifest.txt> --version 1.2.0.1 \\
      --out X6100-Accessibility-v1.2.0.1-update.tar.gz --key update-key.pem

The key's public half is rootfs/usr/share/x6100/update.pub. Adapted from OpenX6X00's tools/update/mkbundle.py
(Joeri Van Dooren ON6URE / Guru-RF). SPDX-License-Identifier: LGPL-2.1-or-later
"""
import argparse
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import time

SKIP_PREFIXES = ("/usr/include/",)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--key")
    ap.add_argument("--notes", default="")
    a = ap.parse_args()

    paths = set()
    with open(a.manifest) as f:
        for line in f:
            p = "/" + os.path.normpath(line.strip()).lstrip("/")
            if p != "/" and not p.startswith(SKIP_PREFIXES):
                paths.add(p)

    entries = []
    for p in sorted(paths):
        src = os.path.join(a.target, p.lstrip("/"))
        if not os.path.isfile(src):
            sys.exit(f"missing in target: {p}")
        with open(src, "rb") as f:
            data = f.read()
        entries.append({"path": p, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data),
                        "mode": "%o" % (os.stat(src).st_mode & 0o777)})
    manifest = {"product": "X6100-Accessibility", "model": "x6100", "version": a.version.lstrip("v"),
                "built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "notes": a.notes, "files": entries}
    mjson = json.dumps(manifest, indent=1, sort_keys=True).encode()

    sig = b""
    if a.key:
        r = subprocess.run(["openssl", "dgst", "-sha256", "-sign", a.key], input=mjson, capture_output=True)
        if r.returncode != 0:
            sys.exit("signing failed: " + r.stderr.decode())
        sig = r.stdout

    def add_bytes(tar, name, data, mode=0o644):
        ti = tarfile.TarInfo(name)
        ti.size, ti.mode, ti.mtime = len(data), mode, int(time.time())
        tar.addfile(ti, io.BytesIO(data))

    with tarfile.open(a.out, "w:gz") as tar:
        add_bytes(tar, "manifest.json", mjson)
        if sig:
            add_bytes(tar, "manifest.sig", sig)
        for e in entries:
            tar.add(os.path.join(a.target, e["path"].lstrip("/")), "files" + e["path"], recursive=False)
    total = sum(e["size"] for e in entries)
    print(f"{a.out}: {len(entries)} files, {total / 1e6:.1f} MB, {'signed' if sig else 'UNSIGNED'}")


if __name__ == "__main__":
    main()
