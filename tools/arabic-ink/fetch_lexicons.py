#!/usr/bin/env python3
"""fetch_lexicons.py - download the free Arabic word sources the composite
lexicon is built from.

Purpose:  The Arabic engine reads a word best when it may choose among real
          words.  Arabic is diglossic: the written standard (fusha - Modern
          Standard and Classical Arabic) and the spoken dialects differ in
          their words, so the sources are kept apart by variety and
          build_lexicon.py merges them into one lexicon whose every word
          says which varieties it belongs to.  This script only downloads;
          nothing it fetches is committed (the licences ask for
          attribution, and the files are large).

Sources (all free; licences as their publishers state them):
          wiktionary-*   Wiktionary entries extracted by kaikki.org
                         (wiktextract; CC BY-SA 4.0 and GFDL, as
                         Wiktionary): Arabic (the standard), and the
                         Egyptian, South and North Levantine, Hijazi, Gulf,
                         Moroccan and Tunisian Arabic dialects.
          camel-*        The CAMeL Arabic Frequency Lists (CAMeL Lab, NYU
                         Abu Dhabi; CC BY-SA 4.0; cite Inoue et al. 2021,
                         "The Interplay of Variant, Size, and Task Type in
                         Arabic Pre-trained Language Models"): word counts
                         over 12.6 billion words of Modern Standard Arabic,
                         5.8 billion of dialect (mixed, untagged) and 847
                         million of Classical Arabic.
          dialects-*     drelhaj/Arabic-Dialects on Hugging Face (CC BY 4.0;
                         M. El-Haj): word counts of Egyptian, Gulf,
                         Levantine and North African (Maghrebi) dialect
                         text, and of the standard as written beside them.

Inputs:   none (the network).
Outputs:  build/arabic-lex/sources/<source>/<file>, and
          build/arabic-lex/sources/manifest.json: each file's URL, licence,
          size and SHA-256.  A file already there with its recorded size is
          not fetched again.
Usage:    python tools/arabic-ink/fetch_lexicons.py [--only NAME...] [--out DIR]
          (about 1.1 GB, most of it Wiktionary's Arabic; a few minutes)
"""

import argparse
import hashlib
import json
import os
import sys
import time
import urllib.parse
import urllib.request
import zipfile

KAIKKI = "https://kaikki.org/dictionary/%s/kaikki.org-dictionary-%s.jsonl"
CAMEL = "https://github.com/CAMeL-Lab/Camel_Arabic_Frequency_Lists/releases/download/v1.0/%s_freq_lists.tsv.zip"
ELHAJ = ("https://huggingface.co/datasets/drelhaj/Arabic-Dialects/resolve/main/"
         "dialects-frequency-lists/Dialects%20Tokens%20WITH%20Frequency%20Count/all-{}_FreqList.txt")

WIKT_LICENCE = "CC BY-SA 4.0 / GFDL (Wiktionary, extracted by kaikki.org wiktextract)"
CAMEL_LICENCE = "CC BY-SA 4.0 (CAMeL Lab; cite Inoue et al. 2021)"
ELHAJ_LICENCE = "CC BY 4.0 (drelhaj/Arabic-Dialects, Hugging Face)"


def kaikki(lang):
    return KAIKKI % (urllib.parse.quote(lang), lang.replace(" ", ""))


# name: (url, file name, licence)
SOURCES = {
    "wiktionary-msa": (kaikki("Arabic"), "arabic.jsonl", WIKT_LICENCE),
    "wiktionary-egyptian": (kaikki("Egyptian Arabic"), "egyptian.jsonl", WIKT_LICENCE),
    "wiktionary-levantine-south": (kaikki("South Levantine Arabic"), "south-levantine.jsonl", WIKT_LICENCE),
    "wiktionary-levantine-north": (kaikki("North Levantine Arabic"), "north-levantine.jsonl", WIKT_LICENCE),
    "wiktionary-hijazi": (kaikki("Hijazi Arabic"), "hijazi.jsonl", WIKT_LICENCE),
    "wiktionary-gulf": (kaikki("Gulf Arabic"), "gulf.jsonl", WIKT_LICENCE),
    "wiktionary-moroccan": (kaikki("Moroccan Arabic"), "moroccan.jsonl", WIKT_LICENCE),
    "wiktionary-tunisian": (kaikki("Tunisian Arabic"), "tunisian.jsonl", WIKT_LICENCE),
    "camel-msa": (CAMEL % "MSA", "MSA_freq_lists.tsv.zip", CAMEL_LICENCE),
    "camel-da": (CAMEL % "DA", "DA_freq_lists.tsv.zip", CAMEL_LICENCE),
    "camel-ca": (CAMEL % "CA", "CA_freq_lists.tsv.zip", CAMEL_LICENCE),
    "dialects-egy": (ELHAJ.format("EGY"), "EGY.txt", ELHAJ_LICENCE),
    "dialects-glf": (ELHAJ.format("GLF"), "GLF.txt", ELHAJ_LICENCE),
    "dialects-lav": (ELHAJ.format("LAV"), "LAV.txt", ELHAJ_LICENCE),
    "dialects-nor": (ELHAJ.format("NOR"), "NOR.txt", ELHAJ_LICENCE),
    "dialects-msa": (ELHAJ.format("MSA"), "MSA.txt", ELHAJ_LICENCE),
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
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while True:
            block = r.read(1 << 20)
            if not block:
                break
            f.write(block)
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default="build/arabic-lex/sources")
    ap.add_argument("--only", nargs="*", help="fetch only these sources")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    mpath = os.path.join(args.out, "manifest.json")
    manifest = json.load(open(mpath, encoding="utf-8")) if os.path.exists(mpath) else {}
    for name, (url, fname, licence) in SOURCES.items():
        if args.only and name not in args.only:
            continue
        d = os.path.join(args.out, name)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, fname)
        known = manifest.get(name)
        if known and os.path.exists(path) and os.path.getsize(path) == known["bytes"]:
            print("%-28s have it (%d bytes)" % (name, known["bytes"]))
            continue
        print("%-28s fetching %s" % (name, url), flush=True)
        for attempt in range(5):
            try:
                fetch(url, path)
                break
            except Exception as e:  # a host that refuses or rate-limits: wait, try again
                print("%-28s failed (%s); again in %d s" % (name, e, 20 * (attempt + 1)), flush=True)
                time.sleep(20 * (attempt + 1))
        else:
            print("%-28s FAILED" % name, file=sys.stderr)
            continue
        if fname.endswith(".zip"):
            with zipfile.ZipFile(path) as z:
                for member in z.namelist():
                    if member.endswith(".tsv") and "__MACOSX" not in member:
                        z.extract(member, d)    # zipfile drops '..' and roots
        manifest[name] = {"url": url, "file": fname, "licence": licence,
                          "bytes": os.path.getsize(path), "sha256": sha256(path)}
        with open(mpath, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=1, ensure_ascii=False)
        print("%-28s %d bytes" % (name, manifest[name]["bytes"]), flush=True)


if __name__ == "__main__":
    main()
