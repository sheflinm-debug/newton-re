#!/usr/bin/env python3
"""ink_sets.py - turn the online handwriting sets fetch_ink.py downloads into
samples (the capture page's format, README "Sample format").

Purpose:  AHWDB (Mutah University, 2019; free for research and teaching):
          200 writers, each writing "محمد" (AHWDB1) and "محمد عبدالله"
          (AHWDB2) ten times with a finger on a tablet held in portrait and
          written across, so the points are turned a quarter turn back
          (the writing runs down the screen: x, y -> -y, x).  The files
          keep no pen lifts: a stroke ends where the clock jumps by more
          than --gap milliseconds (a tablet sample is about 17 ms).
          Many hands, two texts: --per-writer limits what one writer gives
          (the first trials), so that two words do not swamp the training.

          writers: other people's handwriting from the capture page
          published as a claude.ai artifact (capture.html; its store's
          "samples" collection, each sample carrying the artifact's
          anonymous id for its writer), pulled by Claude with the
          ArtifactData tool (list, out_dir build/arabic-ink/pulled).

Inputs:   build/arabic-ink/downloads/ahwdb{1,2}/unpacked/ (fetch_ink.py);
          build/arabic-ink/pulled/ (writers).
Outputs:  build/arabic-ink/ahwdb/<part>/<writer>_<trial>.json, with
          "set": "ahwdb", "writer"; writers past --train-writers go to
          build/arabic-ink/ahwdb-held/ instead, never trained on.
          writers: build/arabic-ink/writers/<writer>/<sample>.json.
Usage:    python tools/arabic-ink/ink_sets.py ahwdb [--per-writer N] [--gap MS] [--train-writers N]
          python tools/arabic-ink/ink_sets.py writers [--pulled DIR]
"""

import argparse
import glob
import json
import os
import re
import sys

TEXTS = {"ahwdb1": "محمد", "ahwdb2": "محمد عبدالله"}


def read_ahwdb(path, gap):
    """[[x, y, t, p, ...], ...] - the strokes, turned upright, t in ms
    from the first point."""
    pts = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) < 4:
                continue
            try:
                stamp = parts[0]
                # yyyymmddhhmmssfff: the time of day in milliseconds
                ms = ((int(stamp[8:10]) * 60 + int(stamp[10:12])) * 60 + int(stamp[12:14])) * 1000 + int(stamp[14:17])
                pts.append((ms, float(parts[1]), float(parts[2]), float(parts[3])))
            except ValueError:
                continue
    if len(pts) < 2:
        return None
    t0 = pts[0][0]
    strokes, cur, prev = [], [], None
    for ms, x, y, p in pts:
        if prev is not None and ms - prev > gap:
            strokes.append(cur)
            cur = []
        cur += [-y, x, ms - t0, p]
        prev = ms
    strokes.append(cur)
    strokes = [s for s in strokes if len(s) >= 8]
    if not strokes:
        return None
    xs = [s[i] for s in strokes for i in range(0, len(s), 4)]
    ys = [s[i + 1] for s in strokes for i in range(0, len(s), 4)]
    lo, top = min(xs), min(ys)
    for s in strokes:
        for i in range(0, len(s), 4):
            s[i] = round(s[i] - lo, 2)
            s[i + 1] = round(s[i + 1] - top, 2)
    return strokes


def ahwdb(args):
    made = 0
    for part, text in TEXTS.items():
        root = os.path.join(args.downloads, part, "unpacked")
        held = os.path.join(args.out, "ahwdb-held", part)
        out = os.path.join(args.out, "ahwdb", part)
        os.makedirs(out, exist_ok=True)
        os.makedirs(held, exist_ok=True)
        files = glob.glob(os.path.join(root, "**", "*.txt"), recursive=True)
        by_writer = {}
        for f in files:
            m = re.match(r"(\d+)_(\d+)\.txt$", os.path.basename(f))
            if m:
                by_writer.setdefault(int(m.group(1)), []).append((int(m.group(2)), f))
        for writer, trials in sorted(by_writer.items()):
            for trial, f in sorted(trials)[:args.per_writer]:
                strokes = read_ahwdb(f, args.gap)
                if not strokes:
                    continue
                xs = [s[i] for s in strokes for i in range(0, len(s), 4)]
                ys = [s[i + 1] for s in strokes for i in range(0, len(s), 4)]
                sample = {"word": text, "set": "ahwdb", "ahwdbPart": part, "writer": writer,
                          "strokes": strokes,
                          "slate": {"width": round(max(xs)), "height": round(max(ys))},
                          "pointerTypes": ["touch"]}
                into = held if writer > args.train_writers else out
                with open(os.path.join(into, "%d_%d.json" % (writer, trial)), "w", encoding="utf-8") as h:
                    json.dump(sample, h, ensure_ascii=False)
                made += 1
        print("%s: %d writers" % (part, len(by_writer)))
    print("%d samples written to %s" % (made, os.path.join(args.out, "ahwdb")))


def writers(args):
    """The capture page's artifact store, pulled to --pulled (the
    ArtifactData tool's list with out_dir writes one JSON file a sample,
    the page's fields perhaps inside a "data" member): each writer's
    samples under writers/<writer>/, the writer an opaque id the page
    took from the artifact, never a name."""
    made = 0
    seen = set()
    for f in glob.glob(os.path.join(args.pulled, "**", "*.json"), recursive=True):
        with open(f, encoding="utf-8") as h:
            d = json.load(h)
        d = d.get("data", d) if isinstance(d, dict) else None
        if not isinstance(d, dict) or not d.get("word") or not d.get("strokes"):
            continue
        who = re.sub(r"[^A-Za-z0-9_-]", "_", str(d.get("writer") or "unknown"))
        seen.add(who)
        out = os.path.join(args.out, "writers", who)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, os.path.basename(f)), "w", encoding="utf-8") as h:
            json.dump(dict(d, set=d.get("set", "words"), source="capture-artifact"), h, ensure_ascii=False)
        made += 1
    print("%d samples from %d writers written to %s" % (made, len(seen), os.path.join(args.out, "writers")))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("set", choices=["ahwdb", "writers"])
    ap.add_argument("--pulled", default="build/arabic-ink/pulled",
                    help="writers: where the capture artifact's store was pulled to")
    ap.add_argument("--downloads", default="build/arabic-ink/downloads")
    ap.add_argument("--out", default="build/arabic-ink")
    ap.add_argument("--per-writer", type=int, default=10)
    ap.add_argument("--gap", type=float, default=50.0)
    ap.add_argument("--train-writers", type=int, default=160,
                    help="writers 1..N for training (ahwdb/), the rest held out (ahwdb-held/)")
    args = ap.parse_args()
    if args.set == "ahwdb":
        ahwdb(args)
    else:
        writers(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
