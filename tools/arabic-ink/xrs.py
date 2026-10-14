#!/usr/bin/env python3
"""Read the xrs harvest.py kept, joined to the samples' words.

Purpose:  the library the letter-table builder reads its training data
          through, and a summary of a harvest: for each sample, its word
          and the xrs the cursive reader cut it into.
Inputs:   one or more harvest directories (harvest.py's output: batch-NNN.tsv
          beside batch-NNN.xrs), or single .xrs files with their .tsv.
Outputs:  `summary`: samples, words read per sample, xrs per sample, and
          how often each xr type, height, shift, orientation and link
          occurs.  As a library: `load(paths)` -> a list of
          {"id", "word", "words": [{"points", "xrs": [Xr, ...], "graph"?}]}
          (a "graph", from a --forced harvest, is the reader's readings of
          the word: each a list of [code, variant, first xr, how many,
          weight]).
Usage:    python tools/arabic-ink/xrs.py summary build/arabic-ink/xrs/set_1 [...]

Each xr is [type, attrib, penalty, height, shift, orient, link, begpoint,
endpoint, left, top, right, bottom] (recognition/XrDump.h); the names
below index it.
"""

import argparse
import collections
import glob
import json
import os
import sys

TYPE, ATTRIB, PENALTY, HEIGHT, SHIFT, ORIENT, LINK, BEG, END, LEFT, TOP, RIGHT, BOTTOM = range(13)


def record_files(paths):
    for p in paths:
        if os.path.isdir(p):
            for x in sorted(glob.glob(os.path.join(p, "*.xrs"))):
                yield x
        else:
            yield p


def load(paths):
    samples = []
    for xrs in record_files(paths):
        words = {}
        with open(os.path.splitext(xrs)[0] + ".tsv", encoding="utf-8") as f:
            for line in f:
                n, sid, word = line.rstrip("\n").split("\t")
                words[sid] = word
        current = None
        with open(xrs, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                except ValueError:
                    continue                      # a line cut short by an interrupted run
                if "mark" in rec:
                    current = {"id": rec["mark"], "word": words.get(rec["mark"]), "words": []}
                    samples.append(current)
                elif "graph" in rec:
                    if current is not None and current["words"]:
                        current["words"][-1]["graph"] = rec["graph"]
                elif current is not None:
                    current["words"].append(rec)
    return samples


def summary(samples):
    n = len(samples)
    per = collections.Counter(len(s["words"]) for s in samples)
    xr_counts = [sum(len(w["xrs"]) for w in s["words"]) for s in samples]
    feats = {k: collections.Counter() for k in ("type", "height", "shift", "orient", "link")}
    for s in samples:
        for w in s["words"]:
            for x in w["xrs"]:
                feats["type"][x[TYPE]] += 1
                feats["height"][x[HEIGHT]] += 1
                feats["shift"][x[SHIFT]] += 1
                feats["orient"][x[ORIENT]] += 1
                feats["link"][x[LINK]] += 1
    print("%d samples, %d distinct words" % (n, len(set(s["word"] for s in samples))))
    print("words the reader made of a sample: " + ", ".join("%d: %d" % kv for kv in sorted(per.items())))
    if xr_counts:
        xs = sorted(xr_counts)
        print("xrs a sample: median %d, 90th percentile %d, most %d" % (xs[len(xs) // 2], xs[int(len(xs) * 0.9)], xs[-1]))
    for name, c in feats.items():
        total = sum(c.values()) or 1
        print("%-6s %d values: %s" % (name, len(c), " ".join("%#x:%.1f%%" % (k, 100.0 * v / total) for k, v in c.most_common(12))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("summary"); s.add_argument("paths", nargs="+")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    if args.cmd == "summary":
        summary(load(args.paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())
