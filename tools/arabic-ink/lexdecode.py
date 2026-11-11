#!/usr/bin/env python3
"""lexdecode.py - read the Arabic network's output by the composite lexicon:
the decoder the host engine (recognition/ArabicLexicon.cpp) mirrors, and
the measure of it.

Purpose:  The network alone spells a word letter by letter (the greedy
          reading); with a lexicon it may only spell words that exist, and
          prefers the frequent ones.  This is a CTC prefix beam search over
          the lexicon:

          - the lexicon is a sorted list of words; a prefix is a range of
            it (bisection), so the "trie" costs no memory, and a range's
            best word score (a sparse-table range maximum) is the
            look-ahead that ranks a word not yet finished;
          - the vowel marks are optional: their probability is added to
            the blank's (the lexicon is unvowelled, a writer may vowel);
          - a letter the lexicon wants may be read from its look-alike at
            a cost (ALTERNATES: an alif written without its hamza, ta
            marbuta as ha, alif maqsura as ya ...), as writers do;
          - several words: after a whole word, the space class starts the
            next, or a letter starts it with no space read, at a cost
            (Arabic's gaps between a word's pieces and between words are
            not always told apart by size);
          - the score: the network's log-probability + WEIGHT x the words'
            log frequencies + BONUS per word;
          - out of the lexicon (a name, a place): if the free reading's
            CTC score beats the lexicon's best by more than OOV_MARGIN, the
            free reading stands.

          The constants are the ones `--tune` chose on the user's samples
          and held-out ADAB words; the C++ decoder has the same values.

Inputs:   a model (.onnx, its .json beside it), the lexicon
          (build_lexicon.py), sample directories.
Outputs:  each sample's free and lexicon readings, and the scores.
Usage:    build/venv/Scripts/python tools/arabic-ink/lexdecode.py MODEL.onnx
              build/arabic-lex/arabic.lex SAMPLES... [--varieties mel...]
              [--phrases N] [--limit N] [--quiet] [--beam B]
"""

import argparse
import bisect
import json
import math
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arabic  # noqa: E402

BEAM = 48
WEIGHT = 0.5          # x ln frequency
BONUS = 2.0           # per word: against reading one long word as two short ones
NO_SPACE = -2.0       # a new word begun without a space read
ALT_COST = -2.5       # a letter read from its look-alike
OOV_MARGIN = 4.0      # the free reading's lead that overrules the lexicon
CANDIDATE = -9.0      # a class below this log-probability in a frame is not tried

ALTERNATES = {
    "أ": "ا", "إ": "ا", "آ": "ا", "ا": "أإ",
    "ة": "ه", "ه": "ة",
    "ى": "ي", "ي": "ى",
    "ؤ": "و", "ئ": "يى",
}
HIGH = "\U0010ffff"


class Lexicon:
    """A sorted word list with each word's score; prefixes are ranges."""

    def __init__(self, path, varieties=None):
        rows = []
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 3:
                    continue
                if varieties and not (set(parts[2]) & set(varieties)):
                    continue
                rows.append((parts[0], float(parts[1])))
        rows.sort()
        self.words = [w for w, _ in rows]
        self.scores = np.array([s for _, s in rows], dtype=np.float32)
        # sparse table for range maxima
        self.table = [self.scores]
        k = 1
        while 2 * k <= len(rows):
            prev = self.table[-1]
            self.table.append(np.maximum(prev[:-k], prev[k:]))
            k *= 2
        self.best = float(self.scores.max()) if rows else 0.0

    def __len__(self):
        return len(self.words)

    def range_max(self, lo, hi):
        k = (hi - lo).bit_length() - 1
        t = self.table[k]
        return float(max(t[lo], t[hi - (1 << k)]))

    def child(self, lo, hi, prefix):
        """The range of words starting with prefix, within [lo, hi)."""
        a = bisect.bisect_left(self.words, prefix, lo, hi)
        b = bisect.bisect_left(self.words, prefix + HIGH, a, hi)
        return a, b

    def score(self, lo, hi, prefix):
        """The word's own score if prefix is a word (it sorts first)."""
        if lo < hi and self.words[lo] == prefix:
            return float(self.scores[lo])
        return None


def logsumexp(a, b):
    if a == -math.inf:
        return b
    if b == -math.inf:
        return a
    m = max(a, b)
    return m + math.log(math.exp(a - m) + math.exp(b - m))


def prepare(logprobs, alphabet):
    """(blank, letters): the per-frame log-probabilities with the vowel
    marks' merged into the blank's; letters maps a character to its
    column (the space included)."""
    lp = np.asarray(logprobs, dtype=np.float64)
    marks = [i for i, ch in enumerate(alphabet) if ch and ch in arabic.MARKS]
    blank = np.logaddexp.reduce(lp[:, [0] + marks], axis=1)
    letters = {ch: lp[:, i] for i, ch in enumerate(alphabet) if ch and ch not in arabic.MARKS}
    return blank, letters


def emission(letters, ch, t):
    """ch's log-probability at t, or its look-alike's at a cost."""
    col = letters.get(ch)
    e = col[t] if col is not None else -math.inf
    for alt in ALTERNATES.get(ch, ""):
        c = letters.get(alt)
        if c is not None:
            e = max(e, c[t] + ALT_COST)
    return e


def ctc_score(blank, letters, text):
    """The CTC forward log-probability of text (marks merged in the blank)."""
    labels = [ch for ch in text]
    T = len(blank)
    if not labels:
        return float(np.sum(blank))
    ext = [None]
    for l in labels:
        ext += [l, None]
    S = len(ext)
    col = [blank if c is None else letters.get(c, np.full(T, -1e9)) for c in ext]
    alpha = [-math.inf] * S
    alpha[0] = col[0][0]
    alpha[1] = col[1][0]
    for t in range(1, T):
        new = [-math.inf] * S
        for s in range(S):
            a = alpha[s]
            if s > 0:
                a = logsumexp(a, alpha[s - 1])
            if s > 1 and ext[s] is not None and ext[s] != ext[s - 2]:
                a = logsumexp(a, alpha[s - 2])
            new[s] = a + col[s][t]
        alpha = new
    return logsumexp(alpha[S - 1], alpha[S - 2]) if S > 1 else alpha[S - 1]


def greedy(blank, letters, alphabet, logprobs):
    best = np.asarray(logprobs).argmax(axis=1)
    out, prev = [], 0
    for c in best:
        if c != prev and c != 0 and alphabet[c] not in arabic.MARKS:
            out.append(alphabet[c])
        prev = c
    return "".join(out).strip()


class Beam:
    __slots__ = ("text", "lo", "hi", "start", "pb", "pnb", "lm", "last")

    def __init__(self, text, lo, hi, start, pb, pnb, lm, last):
        self.text, self.lo, self.hi, self.start = text, lo, hi, start
        self.pb, self.pnb, self.lm, self.last = pb, pnb, lm, last

    def word(self):
        return self.text[self.start:]


def decode(logprobs, alphabet, lex, beam=BEAM, nbest=1):
    """[(text, score)] best first; text is the lexicon reading."""
    blank, letters = prepare(logprobs, alphabet)
    T = len(blank)
    space = letters.get(" ")
    root = (0, len(lex))
    cands_all = [ch for ch in letters if ch != " "]

    def ahead(b):
        w = b.word()
        if not w:
            return lex.best
        return lex.range_max(b.lo, b.hi)

    def total(b):
        return logsumexp(b.pb, b.pnb) + b.lm + WEIGHT * ahead(b)

    beams = {"": Beam("", root[0], root[1], 0, 0.0, -math.inf, 0.0, None)}
    for t in range(T):
        frame = {ch: emission(letters, ch, t) for ch in cands_all}
        tries = [ch for ch, e in frame.items() if e > CANDIDATE]
        sp = space[t] if space is not None else -math.inf
        nxt = {}

        def put(text, lo, hi, start, pb, pnb, lm, last):
            b = nxt.get(text)
            if b is None:
                nxt[text] = Beam(text, lo, hi, start, pb, pnb, lm, last)
            else:
                b.pb = logsumexp(b.pb, pb)
                b.pnb = logsumexp(b.pnb, pnb)

        for b in beams.values():
            p = logsumexp(b.pb, b.pnb)
            # blank (and the marks): stay
            put(b.text, b.lo, b.hi, b.start, p + blank[t], -math.inf, b.lm, b.last)
            # the last letter repeated without a blank: stay
            if b.last is not None and b.last != " ":
                put(b.text, b.lo, b.hi, b.start, -math.inf, b.pnb + frame.get(b.last, -math.inf), b.lm, b.last)
            elif b.last == " ":
                put(b.text, b.lo, b.hi, b.start, -math.inf, b.pnb + sp, b.lm, b.last)
            w = b.word()
            here = lex.score(b.lo, b.hi, w) if w else None
            # a letter: extend the word
            for ch in tries:
                e = frame[ch]
                prev = b.pb if ch == b.last else p
                lo, hi = lex.child(b.lo, b.hi, w + ch)
                if lo < hi:
                    put(b.text + ch, lo, hi, b.start, -math.inf, prev + e, b.lm, ch)
                # or begin a new word with no space read
                if here is not None:
                    lo, hi = lex.child(0, len(lex), ch)
                    if lo < hi:
                        lm = b.lm + WEIGHT * here + BONUS + NO_SPACE
                        put(b.text + " " + ch, lo, hi, len(b.text) + 1, -math.inf, prev + e, lm, ch)
            # the space: close the word
            if here is not None and sp > CANDIDATE:
                lm = b.lm + WEIGHT * here + BONUS
                put(b.text + " ", root[0], root[1], len(b.text) + 1, -math.inf, p + sp, lm, " ")
        beams = dict(sorted(nxt.items(), key=lambda kv: -total(kv[1]))[:beam])

    done = []
    for b in beams.values():
        text = b.text.rstrip(" ")
        if b.text.endswith(" "):
            final = logsumexp(b.pb, b.pnb) + b.lm
        else:
            here = lex.score(b.lo, b.hi, b.word())
            if here is None:
                continue
            final = logsumexp(b.pb, b.pnb) + b.lm + WEIGHT * here + BONUS
        done.append((text, final))
    done.sort(key=lambda x: -x[1])
    seen, out = set(), []
    for text, s in done:
        if text not in seen:
            seen.add(text)
            out.append((text, s))
    return out[:nbest]


def read(logprobs, alphabet, lex, beam=BEAM):
    """(reading, free reading, lexicon reading or None, chose)"""
    blank, letters = prepare(logprobs, alphabet)
    free = greedy(blank, letters, alphabet, logprobs)
    found = decode(logprobs, alphabet, lex, beam) if len(lex) else []
    if not found:
        return free, free, None, "free"
    best = found[0][0]
    if best == free:
        return best, free, best, "lexicon"
    fs = ctc_score(blank, letters, free)
    ls = ctc_score(blank, letters, best)
    if fs - ls > OOV_MARGIN:
        return free, free, best, "free"
    return best, free, best, "lexicon"


def readings(logprobs, alphabet, lex, beam=BEAM, room=4):
    """The engine's list (ArabicLexiconRead): the reading taken, then the
    lexicon's others best first, then the free reading - each once, at
    most room."""
    blank, letters = prepare(logprobs, alphabet)
    free = greedy(blank, letters, alphabet, logprobs)
    found = [t for t, _ in decode(logprobs, alphabet, lex, beam, nbest=room)] if len(lex) else []
    take_free = True
    if found:
        if found[0] == free:
            take_free = False
        else:
            take_free = ctc_score(blank, letters, free) - ctc_score(blank, letters, found[0]) > OOV_MARGIN
    out = []
    for t in ([free] if take_free else []) + found + [free]:
        if t not in out and len(out) < room:
            out.append(t)
    return out


def write_fixture(path, cases, alphabet, lex, beam, room=4):
    """The check ctest recognition.ArabicLexicon reads: little-endian
    "ALXF", version 1, the class count and each class's character (u16),
    the five constants (f64: WEIGHT BONUS NO_SPACE ALT_COST OOV_MARGIN),
    the case count; a case: steps, the logits (f32, steps x classes),
    room, the readings' count, each reading's length and characters (u16)."""
    import struct
    with open(path, "wb") as f:
        f.write(b"ALXF" + struct.pack("<II", 1, len(alphabet)))
        f.write(struct.pack("<%dH" % len(alphabet), *[ord(c) if c else 0 for c in alphabet]))
        f.write(struct.pack("<5d", WEIGHT, BONUS, NO_SPACE, ALT_COST, OOV_MARGIN))
        f.write(struct.pack("<I", len(cases)))
        for _, _, lp, logits in cases:
            got = readings(lp, alphabet, lex, beam, room)
            f.write(struct.pack("<I", len(logits)))
            f.write(np.asarray(logits, dtype="<f4").tobytes())
            f.write(struct.pack("<II", room, len(got)))
            for t in got:
                f.write(struct.pack("<I", len(t)) + struct.pack("<%dH" % len(t), *[ord(c) for c in t]))


PARAMS =("WEIGHT", "BONUS", "NO_SPACE", "ALT_COST", "OOV_MARGIN")
STEPS = {"WEIGHT": (0.25, 0.5, 0.75, 1.0, 1.5, 2.0), "BONUS": (0.0, 2.0, 4.0, 6.0, 9.0),
         "NO_SPACE": (-1.0, -2.0, -4.0, -6.0, -10.0), "ALT_COST": (-1.0, -1.5, -2.5, -4.0),
         "OOV_MARGIN": (2.0, 4.0, 6.0, 9.0, 14.0)}


def score_all(cases, alphabet, lex, beam, show=False):
    """{group: [cases, free right, right, in the lexicon, of them right]}"""
    tally = {}
    for group, want, lp, _ in cases:
        got, free, best, chose = read(lp, alphabet, lex, beam)
        known = all(lex.score(*lex.child(0, len(lex), w), w) is not None for w in want.split())
        t = tally.setdefault(group, [0, 0, 0, 0, 0])
        t[0] += 1
        t[1] += free == want
        t[2] += got == want
        t[3] += known
        t[4] += known and got == want
        if show:
            print("%-8s %s want %-16s free %-16s lexicon %-16s -> %s%s" % (
                group, "in " if known else "OOV", want, free, best, chose, "  *" if got == want else ""))
    return tally


def objective(tally):
    return sum(t[2] / t[0] for t in tally.values()) / len(tally)


_WORK = {}


def _work_init(lexicon, varieties, alphabet, cases, beam):
    """A tuning worker: the lexicon and the cases, once."""
    _WORK.update(lex=Lexicon(lexicon, varieties), alphabet=alphabet, cases=cases, beam=beam)


def _work_eval(params):
    globals().update(params)
    return objective(score_all(_WORK["cases"], _WORK["alphabet"], _WORK["lex"], _WORK["beam"]))


def tune(lexicon, varieties, alphabet, cases, beam, workers):
    """Coordinate ascent: each constant in turn set to each of its STEPS
    (the values tried in parallel), the best kept; twice over, or until
    nothing moves.  ==> the objective."""
    import multiprocessing
    light = [(g, w, lp, None) for g, w, lp, _ in cases]
    current = {k: globals()[k] for k in PARAMS}
    with multiprocessing.Pool(workers, initializer=_work_init,
                              initargs=(lexicon, varieties, alphabet, light, beam)) as pool:
        best = pool.map(_work_eval, [dict(current)])[0]
        print("start %.4f  %s" % (best, " ".join("%s=%g" % kv for kv in current.items())), flush=True)
        for _ in range(2):
            moved = False
            for k in PARAMS:
                tries = [v for v in STEPS[k] if v != current[k]]
                got = pool.map(_work_eval, [dict(current, **{k: v}) for v in tries])
                for v, o in zip(tries, got):
                    print("    %s=%g: %.4f" % (k, v, o), flush=True)
                o, v = max(zip(got, tries))
                if o > best + 1e-9:
                    best, current[k], moved = o, v, True
                    print("  %s=%g -> %.4f" % (k, v, o), flush=True)
            if not moved:
                break
    globals().update(current)
    print("tuned %.4f  %s" % (best, " ".join("%s=%g" % kv for kv in current.items())))
    return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("model")
    ap.add_argument("lexicon")
    ap.add_argument("samples", nargs="+", help="sample directories, each scored as its own group")
    ap.add_argument("--varieties")
    ap.add_argument("--limit", type=int, default=0, help="at most N samples a directory, drawn at random")
    ap.add_argument("--phrases", type=int, default=0, help="also N phrases of 2-3 of the samples")
    ap.add_argument("--beam", type=int, default=BEAM)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--tune", action="store_true",
                    help="choose the constants by coordinate ascent on the mean of the groups' words right")
    ap.add_argument("--workers", type=int, default=6, help="--tune's parallel processes")
    ap.add_argument("--fixture", metavar="FILE",
                    help="write the engine's check (write_fixture) from --fixture-count cases spread over all")
    ap.add_argument("--fixture-count", type=int, default=24)
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help="a constant for this run (WEIGHT, BONUS, NO_SPACE, ALT_COST, OOV_MARGIN)")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    for kv in args.set:
        k, v = kv.split("=")
        globals()[k] = float(v)
    import onnxruntime as ort
    import train_net as T
    with open(os.path.splitext(args.model)[0] + ".json", encoding="utf-8") as f:
        meta = json.load(f)
    alphabet = [c or None for c in meta["alphabet"]]
    T.STEP = meta.get("step", T.STEP)
    sess = ort.InferenceSession(args.model)
    lex = Lexicon(args.lexicon, args.varieties)
    print("lexicon: %d words" % len(lex), flush=True)

    rng = random.Random(args.seed)
    raw = []
    for d in args.samples:
        got = T.load_dir(d)
        if args.limit and len(got) > args.limit:
            got = rng.sample(got, args.limit)
        group = os.path.basename(os.path.normpath(d))
        raw += [(group, w, s) for w, s in got]
    if args.phrases:
        pool = [(w, s) for _, w, s in raw]
        for _ in range(args.phrases):
            w, s = T.compose(rng.sample(pool, rng.choice((2, 3))), rng)
            raw.append(("phrases", w, s))
    cases = []
    for group, want, strokes in raw:
        feats = T.featurize(strokes)
        if feats is None:
            continue
        logits = sess.run(None, {"features": [feats]})[0][0]
        lp = logits.astype(np.float64)              # in doubles, as the engine works
        lp = lp - np.logaddexp.reduce(lp, axis=1, keepdims=True)
        want = " ".join(want.translate({ord(m): None for m in arabic.MARKS}).split())
        cases.append((group, want, lp, logits))

    if args.tune:
        tune(args.lexicon, args.varieties, alphabet, cases, args.beam, args.workers)
    if args.fixture:
        chosen = cases[::max(1, len(cases) // args.fixture_count)][:args.fixture_count]
        write_fixture(args.fixture, chosen, alphabet, lex, args.beam)
        print("wrote %s: %d cases" % (args.fixture, len(chosen)))
    tally = score_all(cases, alphabet, lex, args.beam, show=not args.quiet)
    for group, (n, f, g, k, kg) in tally.items():
        print("%-8s %4d: free %.1f%%, with the lexicon %.1f%%;  in the lexicon %d, of them right %.1f%%" % (
            group, n, 100.0 * f / n, 100.0 * g / n, k, 100.0 * kg / max(1, k)))


if __name__ == "__main__":
    main()
