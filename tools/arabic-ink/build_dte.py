#!/usr/bin/env python3
"""Build an Arabic letter table (DTEMain) for ParaGraph's cursive reader
from harvested samples.

Purpose:  the reader matches a word's xrs against prototypes of each letter
          (docs/recognition/arabic.md, "The letter table's format").  Given
          samples whose xrs harvest.py kept and whose words are known, this
          works out what xrs each Arabic letter produces in each of its
          positional forms and writes them as a table the reader takes
          (NEWTON_DTE_MAIN=<file>, recognition/ParaGraph.cpp).
Inputs:   harvest directories (harvest.py's output).  The samples' words are
          Arabic; arabic.py folds them to the 29 letters and gives each its
          Latin code (the table's slot).
Outputs:  -o <table>.bin (the DTEMain binary, dte.py's encoding) and beside
          it <table>.json (dte.py's JSON, for reading) and <table>.stats.txt
          (how many samples each letter form had, its variants and their
          lengths).  Progress on stderr.
Usage:    python tools/arabic-ink/build_dte.py build/arabic-ink/xrs/set_1
              build/arabic-ink/xrs/set_2 -o build/arabic-ink/arabic-dte.bin
              [--iterations 3] [--variants 3] [--limit N] [--from-reader]
          --from-reader starts from the reader's own layouts of the words
          (a harvest.py --forced harvest, where each word was read with
          itself as the vocabulary): the reader's segmentation of the
          word, with its dots where it put them, in place of the rough
          layout by x.

How it works.  Each sample is a word and the string of xrs the reader cut
it into (mirrored: the string runs in reading order).  The word's letters
in their forms (isolated, initial, medial, final) are laid along the
string first by x - each letter given a slice of the word's width by a
rough width per letter - which gives each letter form a first set of xr
subsequences.  A letter form's model is a variant: a sequence of
prototypes, one per position of its typical (medoid) subsequence, each a
count of the xr types, heights, shifts, links and orientations seen at that
position.  Then, as the reader would, every sample's letter models are
aligned to its xrs by dynamic programming with the reader's own scoring
(a match earns the prototype's five nibbles less 50; skipping an xr costs
its penalty; skipping a prototype its cost), the letters' boundaries and
the positions' xrs read off the alignment, and the counts made again:
a few rounds of that.  Last, a letter form with enough samples has its
subsequences clustered by their type strings into up to --variants
variants, and a final round counts each.

Counts become nibbles on the ROM's scale (a perfect match about 60): the
likeliest value of a feature at a position gets 12, one half as likely 9,
a quarter 6; a type never seen there is 0 (the prototype cannot read it);
the other features floor at 1.  A prototype matched in nearly every sample
costs 18 to skip, one matched in few costs nothing, as the ROM's do.
"""

import argparse
import collections
import json
import os
import random
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import arabic   # noqa: E402
import dte      # noqa: E402
import xrs as X # noqa: E402

NEG = -10 ** 9
MATCH_COST = 50
MAX_PROTOS = 12
MIN_PROTOS = 2
FEATURES = (("type", X.TYPE, 64), ("height", X.HEIGHT, 16), ("shift", X.SHIFT, 16), ("link", X.LINK, 16), ("orient", X.ORIENT, 32))

# a rough width per letter, for the first layout by x (forms adjust it)
WIDTH = {"ا": 0.5, "ل": 0.6, "ر": 0.6, "ز": 0.6, "د": 0.7, "ذ": 0.7, "و": 0.7, "ء": 0.5,
         "ب": 0.7, "ت": 0.7, "ث": 0.7, "ن": 0.7, "ي": 0.8,
         "س": 1.5, "ش": 1.5, "ص": 1.6, "ض": 1.6, "ك": 1.2,
         "ج": 1.1, "ح": 1.1, "خ": 1.1, "ع": 1.0, "غ": 1.0, "م": 0.9, "ه": 0.9, "ف": 1.0, "ق": 1.0, "ط": 1.2, "ظ": 1.2}
FINAL_EXTRA = {"ب": 0.5, "ت": 0.5, "ث": 0.5, "ن": 0.5, "ي": 0.6, "س": 0.6, "ش": 0.6, "ل": 0.4, "ك": 0.4, "ق": 0.4, "ف": 0.3}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


# ---------------------------------------------------------------- samples

class Sample:
    __slots__ = ("id", "word", "letters", "xrs", "cx", "pen", "assign", "reader")

    def __init__(self, sid, word, letters, xrs, reader=None):
        self.id = sid
        self.word = word
        self.letters = letters              # [(letter, form)]
        self.xrs = xrs                      # list of 13-int lists
        self.cx = [(x[X.LEFT] + x[X.RIGHT]) / 2.0 for x in xrs]
        self.pen = np.array([x[X.PENALTY] for x in xrs], dtype=np.int64)
        self.assign = None                  # per letter: list of (xr index, prototype index) or None
        self.reader = reader                # the reader's own layout: per letter the xr indexes, or None


def reader_layout(word_rec, letters):
    """The reader's layout of the word (a --forced harvest's graph, its
    first reading), as the xr indexes of each letter - when that reading
    is the word itself, letter for letter."""
    graph = word_rec.get("graph")
    if not graph or not graph[0]:
        return None
    reading = graph[0]
    codes = [arabic.TO_CODE[ch] for ch, form in letters]
    if [chr(s[0]) for s in reading] != codes:
        return None
    n = len(word_rec["xrs"])
    out = []
    for sym in reading:
        start, count = sym[2], sym[3]
        out.append([i for i in range(start, start + count) if 0 <= i < n])
    return out


def load_samples(paths, limit):
    out = []
    for s in X.load(paths):
        if len(s["words"]) != 1 or not s["word"]:
            continue
        seq = s["words"][0]["xrs"]
        letters = [lf for lf in arabic.letters(s["word"]) if lf[1] is not None]
        if not letters or len(seq) < len(letters):
            continue
        out.append(Sample(s["id"], s["word"], letters, seq, reader_layout(s["words"][0], letters)))
    random.Random(7).shuffle(out)
    return out[:limit] if limit else out


def layout_by_x(sample):
    """A first assignment of the xrs to the letters by x: each letter a
    slice of the word's width."""
    widths = []
    for letter, form in sample.letters:
        if form == arabic.MARK:
            w = 0.2                 # (a mark sits over its letter: a sliver after it)
        else:
            w = WIDTH.get(letter, 1.0)
            if form in (arabic.FINAL, arabic.ISOLATED):
                w += FINAL_EXTRA.get(letter, 0.0)
        widths.append(w)
    total = sum(widths)
    lo = min(sample.cx)
    hi = max(sample.cx)
    span = max(hi - lo, 1.0)
    edges = []
    acc = 0.0
    for w in widths:
        acc += w
        edges.append(lo + span * acc / total)
    per_letter = [[] for _ in sample.letters]
    for i, cx in enumerate(sample.cx):
        j = 0
        while j < len(edges) - 1 and cx > edges[j]:
            j += 1
        per_letter[j].append(i)
    return per_letter


# ---------------------------------------------------------------- models

class Prototype:
    def __init__(self):
        self.counts = {name: np.zeros(size, dtype=np.int64) for name, _, size in FEATURES}
        self.n = 0
        self.breaks = 0
        self.nibbles = None
        self.skip = 18
        self.break_only = False

    def add(self, xr):
        for name, idx, _ in FEATURES:
            self.counts[name][xr[idx]] += 1
        self.n += 1
        if xr[X.ATTRIB] & 0x80:
            self.breaks += 1

    def finish(self, letter_samples):
        # The ROM's scale (docs/recognition/arabic.md): the type carries the
        # discrimination (its nibbles spread from 12 down to 0), and the
        # other four are permissive - mostly 11 and 12, so that a typical
        # match nets a few points over the 50 it costs, which the Viterbi
        # needs (a letter whose net gain is not positive is dropped).
        # Height, shift and orientation are smoothed over their neighbours
        # (orientation around the circle) before the nibbles are read off.
        self.nibbles = {}
        for name, _, size in FEATURES:
            c = self.counts[name].astype(np.float64)
            n = c.sum()
            if n == 0:
                self.nibbles[name] = np.full(size, 0 if name == "type" else 11, dtype=np.int64)
                continue
            if name in ("height", "shift"):
                c = c + 0.5 * (np.roll(c, 1) + np.roll(c, -1))
                c[0] -= 0.5 * self.counts[name][-1]
                c[-1] -= 0.5 * self.counts[name][0]
            elif name == "orient":
                c = c + 0.6 * (np.roll(c, 1) + np.roll(c, -1)) + 0.3 * (np.roll(c, 2) + np.roll(c, -2))
            r = c / c.max()
            if name == "type":
                nib = np.rint(12 + 2.5 * np.log2(np.maximum(r, 1e-9))).astype(np.int64)
                nib = np.clip(nib, 1, 12)
                nib[self.counts[name] == 0] = 0 if n >= 8 else 1
                nib[0] = 4          # type 0, the end of the list: a little, as the ROM's prototypes give it
            else:
                # plausible values (after smoothing) score 10-12, as the ROM's
                # do; implausible ones fall away fast, so that a dot above
                # the line is not read as one below it
                nib = np.full(size, 2, dtype=np.int64)
                for floor, value in ((0.01, 6), (0.03, 8), (0.07, 10), (0.15, 11), (0.35, 12)):
                    nib[r >= floor] = value
                nib[c == 0] = np.minimum(nib[c == 0], 4)
            self.nibbles[name] = nib
        # (a position nearly always read is required; one read in most
        # samples costs a little to skip; the rest are free - gentler than
        # the ROM's 18s, so that a letter of many prototypes is not undercut
        # by chains of letters of one)
        f = self.n / float(letter_samples) if letter_samples else 0.0
        self.skip = 18 if f >= 0.92 else (8 if f >= 0.65 else 0)
        self.break_only = self.n >= 8 and self.breaks >= 0.9 * self.n


class Variant:
    def __init__(self, length):
        self.protos = [Prototype() for _ in range(length)]
        self.samples = 0
        self.match = None   # per-feature nibble matrices [K, size], set by prepare()

    def prepare(self):
        for p in self.protos:
            p.finish(self.samples)
        self.match = {name: np.array([p.nibbles[name] for p in self.protos]) for name, _, _ in FEATURES}
        self.skips = np.array([p.skip for p in self.protos], dtype=np.int64)
        self.break_only = np.array([p.break_only for p in self.protos])


def medoid_length(subseqs, rng):
    """How many prototypes a letter form's model gets: enough for three
    quarters of its subsequences (the dots a letter carries, placed after
    it, must have positions of their own, or the rounds of alignment can
    never take them in), at least MIN_PROTOS, at most the prototype limit."""
    lengths = sorted(len(s) for s in subseqs)
    want = lengths[min(len(lengths) - 1, int(len(lengths) * 0.75))]
    return max(MIN_PROTOS, min(MAX_PROTOS, want))


def levenshtein(a, b):
    if len(a) < len(b):
        a, b = b, a
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


# ---------------------------------------------------------------- alignment

def match_matrix(variant, sample):
    """[K, N] the score of each prototype read against each xr, less the
    reading cost; NEG where the prototype cannot read it."""
    xs = np.array(sample.xrs, dtype=np.int64)
    m = np.zeros((len(variant.protos), len(sample.xrs)), dtype=np.int64)
    for name, idx, _ in FEATURES:
        m += variant.match[name][:, xs[:, idx]]
    cannot = variant.match["type"][:, xs[:, X.TYPE]] == 0
    if variant.break_only.any():
        near = (xs[:, X.ATTRIB] & 0x80) != 0
        cannot |= variant.break_only[:, None] & ~near[None, :]
    m -= MATCH_COST
    m[cannot] = NEG
    return m


def align_letter(variant, sample, inp, P):
    """The reader's matrix for one variant: from the line `inp` (the best
    score ending before each xr position) to the line out, with the rows
    kept for the traceback."""
    m = match_matrix(variant, sample)
    rows = [inp]
    for k in range(len(variant.protos)):
        prev = rows[-1]
        row = prev - variant.skips[k]
        diag = np.concatenate(([NEG], prev[:-1] + m[k]))
        row = np.maximum(row, diag)
        # skipping xrs: the best earlier position less the penalties between
        row = np.maximum.accumulate(row + P) - P
        row[row < NEG // 2] = NEG
        rows.append(row)
    return rows, m


def traceback(variant, sample, rows, m, end):
    """From the end position back to the start: the (xr, prototype) pairs
    read on the diagonal, and the start position."""
    pairs = []
    k = len(variant.protos)
    i = end
    while k > 0:
        v = rows[k][i]
        if i > 0 and rows[k - 1][i - 1] > NEG // 2 and v == rows[k - 1][i - 1] + m[k - 1][i - 1]:
            pairs.append((i - 1, k - 1))
            k -= 1
            i -= 1
        elif rows[k - 1][i] > NEG // 2 and v == rows[k - 1][i] - variant.skips[k - 1]:
            k -= 1
        elif i > 0 and rows[k][i - 1] > NEG // 2 and v == rows[k][i - 1] - sample.pen[i - 1]:
            i -= 1
        else:
            # (an earlier skip in the row, through the scan)
            j = i - 1
            while j >= 0 and not (rows[k][j] > NEG // 2 and v == rows[k][j] - (int(sample.pen[j:i].sum()))):
                j -= 1
            if j < 0:
                return None, None
            i = j
    return pairs, i


def align_sample(models, sample):
    """Every letter's models aligned to the sample's xrs: per letter the
    (xr, prototype) pairs of the variant that won, and the whole score."""
    N = len(sample.xrs)
    P = np.concatenate(([0], np.cumsum(sample.pen)))
    inp = np.full(N + 1, NEG, dtype=np.int64)
    inp[0] = 0
    kept = []
    for letter, form in sample.letters:
        variants = models.get((letter, form))
        if not variants:
            return None, None
        outs = []
        for v in variants:
            rows, m = align_letter(v, sample, inp, P)
            outs.append((rows, m))
        out = np.max(np.stack([r[-1] for r, _ in outs]), axis=0)
        kept.append((variants, outs, out))
        inp = out
    if inp[N] <= NEG // 2:
        return None, None
    # back through the letters
    assign = [None] * len(sample.letters)
    end = N
    for j in range(len(sample.letters) - 1, -1, -1):
        variants, outs, out = kept[j]
        best = None
        for vi, (rows, m) in enumerate(outs):
            if rows[-1][end] == out[end]:
                best = vi
                break
        rows, m = outs[best]
        pairs, start = traceback(variants[best], sample, rows, m, end)
        if pairs is None:
            return None, None
        assign[j] = (best, pairs)
        end = start
    return assign, int(inp[N])


# ---------------------------------------------------------------- training

def count_models(samples, models, assignments):
    """Fresh counts from the assignments (each sample's per-letter winning
    variant and its (xr, prototype) pairs)."""
    for variants in models.values():
        for v in variants:
            v.samples = 0
            for p in v.protos:
                p.counts = {name: np.zeros(size, dtype=np.int64) for name, _, size in FEATURES}
                p.n = 0
                p.breaks = 0
    for sample, assign in zip(samples, assignments):
        if assign is None:
            continue
        for (letter, form), (vi, pairs) in zip(sample.letters, assign):
            v = models[(letter, form)][vi]
            v.samples += 1
            for i, k in pairs:
                v.protos[k].add(sample.xrs[i])
    for variants in models.values():
        for v in variants:
            v.prepare()


def initial_models(samples, rng, min_samples, from_reader=False):
    """From the layout by x - or the reader's own layout of the word, where
    a --forced harvest has one (--from-reader) - one variant per letter
    form, sized for its subsequences."""
    subseqs = collections.defaultdict(list)
    layouts = []
    used_reader = 0
    for s in samples:
        if from_reader and s.reader is not None:
            per = s.reader
            used_reader += 1
        else:
            per = layout_by_x(s)
        layouts.append(per)
        for (letter, form), idxs in zip(s.letters, per):
            if idxs:
                subseqs[(letter, form)].append([s.xrs[i] for i in idxs])
    models = {}
    for key, seqs in subseqs.items():
        if len(seqs) < min_samples:
            continue
        models[key] = [Variant(medoid_length(seqs, rng))]
    # the first counts: each subsequence laid over the medoid's length by position
    assignments = []
    for s, per in zip(samples, layouts):
        assign = []
        ok = True
        for (letter, form), idxs in zip(s.letters, per):
            variants = models.get((letter, form))
            if not variants:
                ok = False
                break
            K = len(variants[0].protos)
            pairs = []
            if idxs:
                for n, i in enumerate(idxs):
                    k = min(K - 1, int(n * K / len(idxs)))
                    pairs.append((i, k))
            assign.append((0, pairs))
        assignments.append(assign if ok else None)
    count_models(samples, models, assignments)
    if from_reader:
        log("%d of %d samples laid out by the reader itself" % (used_reader, len(samples)))
    return models


def split_variants(samples, models, assignments, max_variants, rng, min_cluster):
    """Each letter form's aligned subsequences clustered by their type
    strings into up to max_variants variants."""
    subseqs = collections.defaultdict(list)
    for s, assign in zip(samples, assignments):
        if assign is None:
            continue
        for (letter, form), (vi, pairs) in zip(s.letters, assign):
            subseqs[(letter, form)].append(tuple(s.xrs[i][X.TYPE] for i, k in pairs))
    new = {}
    for key, variants in models.items():
        strings = subseqs.get(key, [])
        if len(strings) < 2 * min_cluster or max_variants < 2:
            new[key] = variants
            continue
        pool = strings if len(strings) <= 200 else rng.sample(strings, 200)
        # k-medoids, a few sweeps
        k = min(max_variants, max(1, len(pool) // min_cluster))
        medoids = rng.sample(pool, k)
        for _ in range(4):
            clusters = [[] for _ in medoids]
            for st in pool:
                d = [levenshtein(st, md) for md in medoids]
                clusters[d.index(min(d))].append(st)
            medoids = [min(c, key=lambda a: sum(levenshtein(a, b) for b in c)) if c else md for c, md in zip(clusters, medoids)]
        sizes = [len(c) for c in clusters]
        keep = [md for md, n in zip(medoids, sizes) if n >= min_cluster * len(pool) / float(len(strings))]
        if len(keep) < 2:
            new[key] = variants
            continue
        new[key] = [Variant(max(MIN_PROTOS, min(MAX_PROTOS, len(md) + 1))) for md in keep]
        for v, md in zip(new[key], keep):
            v.medoid = md
    # a first count for the new variants: each subsequence to the nearest medoid
    for s, assign in zip(samples, assignments):
        if assign is None:
            continue
        fresh = []
        for (letter, form), (vi, pairs) in zip(s.letters, assign):
            variants = new[(letter, form)]
            if len(variants) == 1 and not hasattr(variants[0], "medoid"):
                fresh.append((vi, pairs))
                continue
            st = tuple(s.xrs[i][X.TYPE] for i, k in pairs)
            d = [levenshtein(st, v.medoid) for v in variants]
            best = d.index(min(d))
            K = len(variants[best].protos)
            idxs = [i for i, k in pairs]
            fresh.append((best, [(i, min(K - 1, int(n * K / max(1, len(idxs))))) for n, i in enumerate(idxs)]))
        assign[:] = fresh
    count_models(samples, models and new, assignments)
    return new


def realign(samples, models):
    assignments = []
    total = 0
    aligned = 0
    for s in samples:
        assign, score = align_sample(models, s)
        assignments.append(assign)
        if assign is not None:
            aligned += 1
            total += score
    return assignments, aligned, total


# ---------------------------------------------------------------- output

def to_table(models, stats):
    letters = []
    by_code = collections.defaultdict(list)
    for (letter, form), variants in models.items():
        by_code[arabic.TO_CODE[letter]].append((form, variants))
    for code in sorted(by_code, key=lambda c: arabic.SLOT[c]):
        variants_out = []
        for form, variants in sorted(by_code[code]):
            for v in variants:
                if v.samples == 0:
                    continue
                protos = []
                for p in v.protos:
                    protos.append({"head": "0107", "breakOnly": bool(p.break_only), "flags": 0, "skipCost": int(p.skip),
                                   **{name: [int(n) for n in p.nibbles[name]] for name, _, _ in FEATURES}})
                variants_out.append({"vexByte": 0, "groups": 0xF0, "zoneA": 0x78, "zoneB": 0x57, "prototypes": protos,
                                     "_form": arabic.FORM_NAMES[form], "_samples": v.samples})
        variants_out = variants_out[:16]
        if not variants_out:
            continue
        slot = arabic.SLOT[code]
        letters.append({"code": slot, "char": code, "count": len(variants_out), "byte1": 0x2a, "byte2": slot, "byte3": 0,
                        "variants": variants_out})
        stats.append("%s %s: %d variants: %s" % (code, arabic.TO_LETTER[code], len(variants_out),
                     ", ".join("%s x%d (%d protos)" % (v["_form"][:3], v["_samples"], len(v["prototypes"])) for v in variants_out)))
    return {"format": "Arabic DTEMain built by tools/arabic-ink/build_dte.py", "lead": "0109b8d8", "letters": letters}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("harvests", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--iterations", type=int, default=3)
    ap.add_argument("--variants", type=int, default=3)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--min-samples", type=int, default=20)
    ap.add_argument("--min-cluster", type=int, default=30)
    ap.add_argument("--from-reader", action="store_true", help="start from the reader's own layouts (a --forced harvest)")
    args = ap.parse_args()
    rng = random.Random(11)

    samples = load_samples(args.harvests, args.limit)
    log("%d samples" % len(samples))
    models = initial_models(samples, rng, args.min_samples, args.from_reader)
    log("%d letter forms modelled" % len(models))
    for it in range(args.iterations):
        assignments, aligned, total = realign(samples, models)
        log("round %d: %d of %d samples aligned, mean score %.1f" % (it + 1, aligned, len(samples), total / max(1, aligned)))
        count_models(samples, models, assignments)
    if args.variants > 1:
        assignments, aligned, total = realign(samples, models)
        models = split_variants(samples, models, assignments, args.variants, rng, args.min_cluster)
        log("variants split: %d models, %d variants" % (len(models), sum(len(v) for v in models.values())))
        for it in range(2):
            assignments, aligned, total = realign(samples, models)
            log("final round %d: %d of %d aligned, mean score %.1f" % (it + 1, aligned, len(samples), total / max(1, aligned)))
            count_models(samples, models, assignments)
    stats = []
    table = to_table(models, stats)
    binary = dte.encode(table)
    with open(args.out, "wb") as f:
        f.write(binary)
    base = os.path.splitext(args.out)[0]
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(table, f, ensure_ascii=False, indent=1)
    with open(base + ".stats.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(stats) + "\n")
    log("wrote %s: %d letters, %d bytes; %s.stats.txt" % (args.out, len(table["letters"]), len(binary), base))
    return 0


if __name__ == "__main__":
    sys.exit(main())
