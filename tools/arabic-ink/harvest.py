#!/usr/bin/env python3
"""Run many Arabic ink samples through the cursive reader and keep the
xrs it reads.

Purpose:  the letter-table builder needs, for every sample, the xrs the
          live reader cuts it into.  This writes replay scripts in batches
          (to_ns.py, mirrored by default), runs newton on each with
          NEWTON_XR_DUMP set (recognition/XrDump.h), and leaves one record
          per batch for xrs.py to read.
Inputs:   a samples directory (serve.py's or adab.py's), the built newton
          and the ROM image (build/host/host/newton, build/MP2x00US/rom.bin).
Outputs:  in the output directory (default build/arabic-ink/xrs/<name>/):
          batch-NNN.ns, batch-NNN.tsv (sample number, id, word), batch-NNN.xrs
          (the record), batch-NNN.log (the run's output).  A batch whose
          .xrs already ends in its last sample's words is not run again, so
          an interrupted harvest picks up where it stopped.
Usage:    python tools/arabic-ink/harvest.py SAMPLES_DIR [--name NAME]
              [--batch 50] [--no-mirror] [--jobs 4] [--forced]
          --forced reads each sample with a vocabulary of its own word
          (to_ns.py --dictionary-each) and the free letters taken away
          (NEWTON_XR_VOCAB_ONLY=1), so the records hold the reader's own
          layout of the right word (its graph) beside the xrs.  It wants
          NEWTON_DTE_MAIN set to the table to lay the words out with.
          e.g. python tools/arabic-ink/harvest.py build/arabic-ink/adab/set_1
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
NEWTON = os.path.join(REPO, "build", "host", "host", "newton.exe" if os.name == "nt" else "newton")
ROM = os.path.join(REPO, "build", "MP2x00US", "rom.bin")


def count_samples(samples):
    return len([n for n in os.listdir(samples) if n.endswith(".json")])


def done(xrs, tsv):
    """Whether a batch's record has a mark for its last sample."""
    if not (os.path.exists(xrs) and os.path.exists(tsv)):
        return False
    with open(tsv, encoding="utf-8") as f:
        last = f.read().strip().split("\n")[-1].split("\t")[1]
    with open(xrs, encoding="utf-8") as f:
        return ('"mark": "%s"' % last) in f.read()


def run_batch(args, k, start):
    base = os.path.join(args.out, "batch-%03d" % k)
    ns, tsv, xrs, log = base + ".ns", base + ".tsv", base + ".xrs", base + ".log"
    if done(xrs, tsv):
        return k, "kept"
    cmd = [sys.executable, os.path.join(HERE, "to_ns.py"), "--samples", args.samples, "-o", ns,
           "--start", str(start), "--count", str(args.batch)]
    if not args.no_mirror:
        cmd.append("--mirror")
    if args.forced:
        cmd.append("--dictionary-each")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    if os.path.exists(xrs):
        os.remove(xrs)
    env = dict(os.environ, NEWTON_XR_DUMP=xrs)
    if args.forced:
        env["NEWTON_XR_VOCAB_ONLY"] = "1"     # (recognition/Xrlv.cpp: only the vocabulary's words may be read)
    env.pop("NEWTON_TRACE_CURSIVE", None)
    # about a tenth of a second a sample, and the boot
    timeout = 60 + args.batch * 2
    with open(log, "w", encoding="utf-8") as f:
        r = subprocess.run([NEWTON, "--rom", ROM, "--display", "320x480", "--script", ns, "--headless", str(timeout)],
                           cwd=REPO, env=env, stdout=f, stderr=subprocess.STDOUT)
    return k, "ran (exit %d)%s" % (r.returncode, "" if done(xrs, tsv) else " - INCOMPLETE")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("samples")
    ap.add_argument("--name", help="the output's name (default: the samples directory's)")
    ap.add_argument("--out", help="output directory (default build/arabic-ink/xrs/<name>)")
    ap.add_argument("--batch", type=int, default=50, help="samples a run (500 ran the Newton out of memory loading the script)")
    ap.add_argument("--jobs", type=int, default=4, help="batches run at once")
    ap.add_argument("--no-mirror", action="store_true")
    ap.add_argument("--forced", action="store_true", help="each sample read with its own word as the vocabulary")
    args = ap.parse_args()
    name = args.name or os.path.basename(os.path.normpath(args.samples))
    args.out = args.out or os.path.join(REPO, "build", "arabic-ink", "xrs", name)
    os.makedirs(args.out, exist_ok=True)
    total = count_samples(args.samples)
    starts = list(range(0, total, args.batch))
    print("%d samples in %d batches -> %s" % (total, len(starts), args.out), flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for k, status in pool.map(lambda ks: run_batch(args, *ks), enumerate(starts)):
            print("batch %03d: %s" % (k, status), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
