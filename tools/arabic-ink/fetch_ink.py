#!/usr/bin/env python3
"""fetch_ink.py - download the free online (pen-trajectory) Arabic-script
handwriting sets the training adds to ADAB, and unpack them.

Purpose:  More writers and more words than ADAB's place names: the network
          learns a hand from its strokes, so only sets that kept the pen's
          points (not scans) serve.  Nothing here is committed; each set's
          licence asks for its own citation (below).  ink_sets.py turns
          them into samples.

Sets:     ahwdb1, ahwdb2  AHWDB (Mutah University, 2019: "New Online/Offline
                          text-dependent Arabic Handwriting dataset for
                          Writer Authentication and Identification", ICSPIS
                          2019): 200 writers x 10 trials each, x/y/pressure/
                          time on a tablet.  Free for research and teaching.
          calliar         Calliar (Alyafeai et al., "Calliar: An Online
                          Handwritten Dataset for Arabic Calligraphy",
                          Neural Computing & Applications 2022; MIT):
                          2,500 sentences, strokes labelled by letter.

          Not here, by the owner's decision (2026-11-11): Persian (POH-Db) or
          any other language's writing in the Arabic script.  The network
          learns which letters follow which as well as their shapes, so
          another language's spelling would leak into its Arabic readings.
          Nor Calliar's calligraphy as training (traced compositions, often
          stacked, not writing along a line).

Inputs:   none (the network).
Outputs:  build/arabic-ink/downloads/<set>/ (the archive and what it
          unpacks to) and build/arabic-ink/downloads/manifest.json (URL,
          licence, size, SHA-256).
Usage:    python tools/arabic-ink/fetch_ink.py [--only SET...] [--seven-zip PATH]
          (about 260 MB; 7-Zip unpacks the .rar archives)
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile

SETS = {
    "ahwdb1": ("https://mutah.edu.jo/biometrix/MZ/Image_Text_DB1.rar", "Image_Text_DB1.rar",
               "free for research and teaching (Mutah University)"),
    "ahwdb2": ("https://mutah.edu.jo/biometrix/MZ/Image_Text_DB2.rar", "Image_Text_DB2.rar",
               "free for research and teaching (Mutah University)"),
    "calliar": ("https://github.com/ARBML/Calliar/archive/refs/heads/main.zip", "Calliar-main.zip",
                "MIT (ARBML/Calliar)"),
}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(url, path):
    tmp = path + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": "newton-re-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=300) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    os.replace(tmp, path)


def seven_zip(given):
    for c in (given, shutil.which("7z"), r"C:\Program Files\7-Zip\7z.exe", "/usr/bin/7z"):
        if c and os.path.exists(c):
            return c
    return None


def unpack(path, into, sz):
    if path.endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            z.extractall(into)          # zipfile drops '..' and roots
        return True
    if not sz:
        print("  no 7-Zip to unpack %s (--seven-zip)" % path, file=sys.stderr)
        return False
    # 7-Zip writes inside the target only (it drops '..' and absolute paths)
    return subprocess.run([sz, "x", "-y", "-o" + into, path], stdout=subprocess.DEVNULL).returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="build/arabic-ink/downloads")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--seven-zip")
    args = ap.parse_args()
    sz = seven_zip(args.seven_zip)
    os.makedirs(args.out, exist_ok=True)
    mpath = os.path.join(args.out, "manifest.json")
    manifest = json.load(open(mpath, encoding="utf-8")) if os.path.exists(mpath) else {}
    for name, (url, fname, licence) in SETS.items():
        if args.only and name not in args.only:
            continue
        d = os.path.join(args.out, name)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, fname)
        known = manifest.get(name)
        if not (known and os.path.exists(path) and os.path.getsize(path) == known["bytes"]):
            print("%-8s fetching %s" % (name, url), flush=True)
            for attempt in range(4):
                try:
                    fetch(url, path)
                    break
                except Exception as e:
                    print("%-8s failed (%s); again in %d s" % (name, e, 20 * (attempt + 1)), flush=True)
                    time.sleep(20 * (attempt + 1))
            else:
                print("%-8s FAILED" % name, file=sys.stderr)
                continue
            manifest[name] = {"url": url, "file": fname, "licence": licence,
                              "bytes": os.path.getsize(path), "sha256": sha256(path)}
            with open(mpath, "w", encoding="utf-8") as f:
                json.dump(manifest, f, indent=1)
        into = os.path.join(d, "unpacked")
        if not os.path.isdir(into):
            print("%-8s unpacking" % name, flush=True)
            if not unpack(path, into, sz):
                print("%-8s could not unpack" % name, file=sys.stderr)
                continue
        print("%-8s %d bytes, unpacked in %s" % (name, manifest[name]["bytes"], into), flush=True)


if __name__ == "__main__":
    main()
