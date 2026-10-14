#!/usr/bin/env python3
"""Convert the ADAB database into Arabic ink samples.

Purpose:  ADAB (the Arabic handwriting DataBase of the Institut fuer
          Nachrichtentechnik, TU Braunschweig, and REGIM, University of
          Sfax) holds Tunisian place names written on Tablet PCs by many
          writers, the pen's trajectory recorded.  This turns each of its
          words into a sample in the format capture.html and serve.py
          write, so the replay and table-building tools take both alike.
Inputs:   a copy of ADAB, unpacked (default build/adab/): set_N/inkml/<id>.inkml
          (the traces: "x y" points, comma-separated, one <trace> per
          stroke, 125 points a second) and set_N/upx/<id>.upx (the label,
          the place name in numeric character references, and the
          writer).  ADAB is not redistributed with this repository: it is
          open access on IEEE DataPort (a free IEEE account is needed) for
          non-commercial research, citing El Abed et al. (see
          docs/recognition/arabic.md), and is downloaded by hand.
Outputs:  one JSON sample per word in the output directory (default
          build/arabic-ink/adab/set_N/<id>.json): word, set ("adab"),
          adabSet, writer, strokes (x, y, t, pressure: t from the 125 Hz
          sample rate, pressure 0.5, there being none), and a slate with
          guide lines estimated from the word itself (the 25th and 75th
          percentiles of its points' y: ADAB has no guide lines, and
          to_ns.py scales by them).  A summary on stdout.
Usage:    python tools/arabic-ink/adab.py [--adab build/adab] [--out build/arabic-ink/adab]
              [--set 1]...
"""

import argparse
import glob
import html
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
INK = "{http://www.w3.org/2003/InkML}"
UPX = "{http://unipen.nici.ru.nl/upx}"
RATE_MS = 8          # 125 points a second


def read_upx(path):
    root = ET.parse(path).getroot()
    word = writer = None
    for alt in root.iter(UPX + "alternate"):
        word = html.unescape(alt.get("value", ""))
        break
    for h in root.iter(UPX + "hLevel"):
        writer = h.get("writerRef")
        break
    return word, writer


def read_inkml(path):
    root = ET.parse(path).getroot()
    strokes = []
    for trace in root.iter(INK + "trace"):
        pts = []
        for i, pair in enumerate(p for p in trace.text.strip().split(",") if p.strip()):
            xy = pair.split()
            pts += [float(xy[0]), float(xy[1]), 0, 0.5]
        strokes.append(pts)
    # times run on across strokes; the pen-up time between them is unknown
    t = 0
    for s in strokes:
        for i in range(2, len(s), 4):
            s[i] = t
            t += RATE_MS
        t += RATE_MS * 10
    return [s for s in strokes if s]


def percentile(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, max(0, int(round(q * (len(v) - 1)))))]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--adab", default=os.path.join(REPO, "build", "adab"))
    ap.add_argument("--out", default=os.path.join(REPO, "build", "arabic-ink", "adab"))
    ap.add_argument("--set", type=int, action="append", default=[], help="only this set (repeatable)")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

    sets = sorted(glob.glob(os.path.join(args.adab, "set_*")))
    if args.set:
        sets = [s for s in sets if int(re.sub(r"\D", "", os.path.basename(s))) in args.set]
    if not sets:
        print("no ADAB sets under %s" % args.adab, file=sys.stderr)
        return 1
    total = skipped = 0
    words, writers = set(), set()
    for sdir in sets:
        name = os.path.basename(sdir)
        out = os.path.join(args.out, name)
        os.makedirs(out, exist_ok=True)
        for inkml in sorted(glob.glob(os.path.join(sdir, "inkml", "*.inkml"))):
            sid = os.path.splitext(os.path.basename(inkml))[0]
            upx = os.path.join(sdir, "upx", sid + ".upx")
            try:
                word, writer = read_upx(upx)
                strokes = read_inkml(inkml)
            except (OSError, ET.ParseError, ValueError, IndexError):
                skipped += 1
                continue
            if not word or not strokes:
                skipped += 1
                continue
            ys = [s[i] for s in strokes for i in range(1, len(s), 4)]
            xs = [s[i] for s in strokes for i in range(0, len(s), 4)]
            top, base = percentile(ys, 0.25), percentile(ys, 0.75)
            if base - top < 1:
                base = top + max(1.0, (max(ys) - min(ys)) / 2)
            sample = {"word": word, "set": "adab", "adabSet": name, "writer": writer, "strokes": strokes,
                      "pointFormat": "x,y,t,pressure (ADAB's tablet units; t in ms at 125 Hz, pressure 0.5: none recorded)",
                      "slate": {"width": round(max(xs) - min(xs)), "height": round(max(ys) - min(ys)),
                                "topLine": top, "baseLine": base, "estimated": True},
                      "source": "ADAB " + name + " " + sid}
            with open(os.path.join(out, sid + ".json"), "w", encoding="utf-8") as f:
                json.dump(sample, f, ensure_ascii=False)
            total += 1
            words.add(word)
            writers.add(writer)
    print("converted %d samples (%d skipped): %d distinct words, %d writers -> %s" % (total, skipped, len(words), len(writers), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
