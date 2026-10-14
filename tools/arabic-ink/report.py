#!/usr/bin/env python3
"""Summarise a replay of Arabic ink samples through the cursive recogniser.

Purpose:  compare runs of the scripts to_ns.py writes (for instance as
          written against mirrored): how far the reader got with each
          sample and what it read.
Inputs:   one or more logs of newton running a replay script with
          NEWTON_TRACE_CURSIVE=1 (stdout and stderr together), each beside
          the .tsv index to_ns.py wrote for its script (replay.log goes
          with replay.tsv).
Outputs:  on stdout, per log: the reader's result codes counted (from its
          "[cursive] word of strokes ...: <code>" lines - 0 read, -6 no
          xrs, -7 too few xrs, -8 the low level failed, -9 the word graph
          found no letters), then a table of sample, word, readings.
          With --markdown the same as Markdown tables.
Usage:    python tools/arabic-ink/report.py build/arabic-ink/replay.log
              build/arabic-ink/replay-mirrored.log [--markdown]

The host's stdout (the script's Print lines) and stderr (the trace) are
buffered separately, so their lines do not interleave in order: the codes
are counted over the whole log, and each sample's readings come from the
script's own lines.
"""

import argparse
import collections
import os
import re
import sys

CODES = {"0": "read", "-6": "no xrs", "-7": "too few xrs", "-8": "low level failed", "-9": "no letter path"}
LINE = re.compile(r'"arabic: (\d+) (\S+) (?:info (\d+): (.*)|nothing)"')
WORD = re.compile(r"\[cursive\] word of strokes .*: (-?\d+)$")


def read_log(path):
    tsv = os.path.splitext(path)[0] + ".tsv"
    words = {}
    with open(tsv, encoding="utf-8") as f:
        for line in f:
            n, sid, word = line.rstrip("\n").split("\t")
            words[int(n)] = (sid, word)
    codes = collections.Counter()
    readings = collections.defaultdict(list)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            m = WORD.search(line)
            if m:
                codes[m.group(1)] += 1
                continue
            m = LINE.search(line)
            if m:
                n = int(m.group(1))
                readings[n].append(m.group(4).strip() if m.group(4) is not None else None)
    return words, codes, readings


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    for path in args.logs:
        words, codes, readings = read_log(path)
        read = sum(1 for n in words if any(r for r in readings.get(n, [])))
        print("%s %s: %d of %d samples read" % ("##" if args.markdown else "==", os.path.basename(path), read, len(words)))
        print()
        print(", ".join("%s %d (%s)" % (c, k, CODES.get(c, "?")) for c, k in sorted(codes.items(), key=lambda x: -x[1])))
        print()
        if args.markdown:
            print("| # | word | words found | readings |")
            print("|---|---|---|---|")
        for n in sorted(words):
            sid, word = words[n]
            rs = readings.get(n, [])
            got = [r for r in rs if r]
            text = " / ".join(got) if got else "nothing"
            if args.markdown:
                print("| %d | %s | %d | %s |" % (n, word, len(got), text.replace("|", "\\|")))
            else:
                print("%3d  %-8s  %s" % (n, word, text))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
