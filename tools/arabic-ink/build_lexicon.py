#!/usr/bin/env python3
"""build_lexicon.py - merge the fetched Arabic word sources into the one
composite lexicon the Arabic engine reads by.

Purpose:  Arabic is diglossic, so every word in the lexicon carries the
          varieties it was found in (the letters below), and the engine can
          be told which to read by (NEWTON_ARABIC_VARIETIES, the standard
          and all the dialects by default).  A word is kept as it is
          written by hand: no vowel marks, no tatweel, the look-alikes
          folded as arabic.fold folds them, only the letters the network
          has (the 36 Arabic ones and the Persian pe, che, zhe, gaf and ve
          the dialects borrow); anything else rejects the word.

Varieties: m  Modern Standard Arabic   c  Classical Arabic
           e  Egyptian                 l  Levantine (South and North)
           g  Gulf                     h  Hijazi
           n  North African (Maghrebi: Moroccan, Tunisian, the corpus's NOR)
           d  dialect, not told which (CAMeL's mixed dialect list)

Score:    the natural log of a word's frequency, the largest over the
          corpora that count it (each count over its corpus's own total, so
          a small dialect corpus weighs as much as the standard's); a word
          only a dictionary knows (Wiktionary) gets the floor, the score of
          a word seen --floor-count times in a billion.  The corpora keep a
          word only from --min-msa / --min-da / --min-ca / --min-dialect
          counts up (a word seen only a few times is mostly a typo or a
          run-together pair).

Inputs:   build/arabic-lex/sources/ (fetch_lexicons.py).
Outputs:  build/arabic-lex/arabic.lex - UTF-8, one word per line:
          "word<TAB>score<TAB>varieties", the best first; a header line of
          "#" comments says how it was made.  build/arabic-lex/stats.txt -
          the words each source gave and kept, and the overlap of the
          varieties.
Usage:    python tools/arabic-ink/build_lexicon.py [--sources DIR] [-o FILE]
              [--min-msa N] [--min-da N] [--min-ca N] [--min-dialect N] [--min-letters N] [--max-letters N]
"""

import argparse
import glob
import json
import math
import os
import sys
import unicodedata
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arabic  # noqa: E402

LETTERS = set(arabic.REGISTRY[:36]) | set("پچژگڤ")
TATWEEL = "ـ"
VARIETIES = "mcelghnd"

WIKTIONARY = {
    "wiktionary-msa": ("m", "Arabic"),
    "wiktionary-egyptian": ("e", "Egyptian Arabic"),
    "wiktionary-levantine-south": ("l", "South Levantine Arabic"),
    "wiktionary-levantine-north": ("l", "North Levantine Arabic"),
    "wiktionary-hijazi": ("h", "Hijazi Arabic"),
    "wiktionary-gulf": ("g", "Gulf Arabic"),
    "wiktionary-moroccan": ("n", "Moroccan Arabic"),
    "wiktionary-tunisian": ("n", "Tunisian Arabic"),
}
# forms that are not written words
SKIP_TAGS = {"romanization", "transliteration", "table-tags", "inflection-template",
             "class", "canonical-romanization"}
CAMEL = {"camel-msa": ("m", "min_msa"), "camel-da": ("d", "min_da"), "camel-ca": ("c", "min_ca")}
ELHAJ = {"dialects-egy": "e", "dialects-glf": "g", "dialects-lav": "l", "dialects-nor": "n",
         "dialects-msa": "m"}


def clean(text):
    """The word as written by hand, or None if it has anything else."""
    out = []
    for ch in unicodedata.normalize("NFC", text.strip()):
        ch = arabic.FOLD.get(ch, ch)
        if ch in arabic.MARKS or ch == TATWEEL:
            continue
        if ch not in LETTERS:
            return None
        out.append(ch)
    return "".join(out) or None


def wiktionary(path, lang):
    """Each entry's headword and its written forms (inflections, plurals,
    the dialect spellings Wiktionary gives)."""
    with open(path, encoding="utf-8") as f:
        for line in f:
            e = json.loads(line)
            if e.get("lang") != lang:
                continue
            yield e.get("word", "")
            for form in e.get("forms", ()):
                if SKIP_TAGS & set(form.get("tags", ())):
                    continue
                yield form.get("form", "")


def counts(path):
    """(word, count) from a word<TAB or space>count list."""
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                n = int(parts[-1])
            except ValueError:
                try:
                    n = int(parts[0])
                    parts = parts[1:] + parts[:1]
                except ValueError:
                    continue
            yield parts[0], n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sources", default="build/arabic-lex/sources")
    ap.add_argument("-o", "--out", default="build/arabic-lex/arabic.lex")
    ap.add_argument("--min-msa", type=int, default=2000)
    ap.add_argument("--min-da", type=int, default=1000)
    ap.add_argument("--min-ca", type=int, default=300)
    ap.add_argument("--min-dialect", type=int, default=3)
    ap.add_argument("--floor-count", type=float, default=1.0)
    ap.add_argument("--max-letters", type=int, default=24,
                    help="longer words are left out: run-together lists in the sources (the longest real"
                         " Arabic words have about 15 letters)")
    ap.add_argument("--min-letters", type=int, default=2,
                    help="shorter words are left out: a lone letter in the corpora is a clitic split off"
                         " (wa-, bi-, li-) or a typo, and as a word it lets the decoder cut a word in two")
    args = ap.parse_args()

    score = {}                       # word -> best log frequency
    varieties = defaultdict(set)     # word -> letters
    stats = []

    def add(word, v, logf):
        varieties[word].add(v)
        if logf is not None and (word not in score or logf > score[word]):
            score[word] = logf

    for name, (v, lang) in WIKTIONARY.items():
        files = glob.glob(os.path.join(args.sources, name, "*.jsonl"))
        if not files:
            stats.append("%-28s missing" % name)
            continue
        seen = rejected = 0
        kept = set()
        for raw in wiktionary(files[0], lang):
            seen += 1
            w = clean(raw)
            if not w:
                rejected += 1
                continue
            kept.add(w)
        for w in kept:
            add(w, v, None)
        stats.append("%-28s %8d forms, %8d words kept (%d not in Arabic letters)" % (name, seen, len(kept), rejected))

    for name, (v, key) in CAMEL.items():
        files = [f for f in glob.glob(os.path.join(args.sources, name, "**", "*.tsv"), recursive=True)
                 if "__MACOSX" not in f]     # the zips' macOS resource forks
        if not files:
            stats.append("%-28s missing" % name)
            continue
        least = getattr(args, key)
        total = 0
        table = Counter()
        for path in files:
            for raw, n in counts(path):
                total += n
                if n >= least:
                    w = clean(raw)
                    if w:
                        table[w] += n
        for w, n in table.items():
            add(w, v, math.log(n / total))
        stats.append("%-28s %8d words of %d and up, of %d tokens" % (name, len(table), least, total))

    for name, v in ELHAJ.items():
        files = glob.glob(os.path.join(args.sources, name, "*.txt"))
        if not files:
            stats.append("%-28s missing" % name)
            continue
        total = 0
        table = Counter()
        for raw, n in counts(files[0]):
            total += n
            w = clean(raw)
            if w:
                table[w] += n
        table = {w: n for w, n in table.items() if n >= args.min_dialect}
        for w, n in table.items():
            add(w, v, math.log(n / total))
        stats.append("%-28s %8d words of %d and up, of %d tokens" % (name, len(table), args.min_dialect, total))

    floor = math.log(args.floor_count / 1e9)
    for w in [w for w in varieties if not args.min_letters <= len(w) <= args.max_letters]:
        del varieties[w]
        score.pop(w, None)
    words = sorted(varieties, key=lambda w: (-score.get(w, floor), w))
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Arabic composite lexicon, made by tools/arabic-ink/build_lexicon.py\n")
        f.write("# from build/arabic-lex/sources (fetch_lexicons.py; see its manifest.json for\n")
        f.write("# each source's URL and licence: Wiktionary CC BY-SA 4.0, CAMeL Lab CC BY-SA 4.0,\n")
        f.write("# drelhaj/Arabic-Dialects CC BY 4.0).  word<TAB>ln frequency<TAB>varieties (%s)\n" % VARIETIES)
        f.write("# min counts: msa %d, da %d, ca %d, dialect %d; floor %.3f; letters %d to %d\n"
                % (args.min_msa, args.min_da, args.min_ca, args.min_dialect, floor, args.min_letters, args.max_letters))
        for w in words:
            vs = "".join(c for c in VARIETIES if c in varieties[w])
            f.write("%s\t%.3f\t%s\n" % (w, score.get(w, floor), vs))

    by = Counter()
    only = Counter()
    for w, vs in varieties.items():
        for c in vs:
            by[c] += 1
        if len(vs) == 1:
            only[next(iter(vs))] += 1
    stats.append("")
    stats.append("%d words in all" % len(words))
    for c in VARIETIES:
        stats.append("  %s  %8d words, %8d only there" % (c, by[c], only[c]))
    text = "\n".join(stats)
    print(text)
    with open(os.path.join(os.path.dirname(args.out) or ".", "stats.txt"), "w", encoding="utf-8") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
