#!/usr/bin/env python3
"""The Arabic-script characters as the cursive reader's eight-bit codes,
and a word's characters in their positional forms.

Purpose:  shared by build_dte.py (which builds a letter table under these
          codes) and report.py (which turns the reader's readings back into
          Arabic).  The reader is an eight-bit engine: with a host letter
          table loaded (NEWTON_DTE_MAIN, recognition/ParaGraph.cpp) its
          alphabet is whatever codes the table has descriptors for, each
          treated as a lower-case letter, so every character in the
          registry below has a code of its own.  A vowel mark (fatha,
          kasra, damma, sukun, shadda, the three tanwin) is a character in
          its own right, written after the letter it sits on, and a reading
          comes out with the marks in that order.  Tatweel and anything
          not registered is dropped; lam-alif is two letters.

The registry.  Codes are allocated in a fixed order from a fixed pool, so
a character's code never changes once given and a new character (another
language's letter) takes the next free code: the pool is a-z, then the
punctuation codes, then 0x7f-0xa2 (the engine reads descriptors for codes
0x20-0xa2; the space, the apostrophe - which the Viterbi treats as its
own - and the digits - the digit reader's - are left out): 119 codes.
Registered in order: the 28 Arabic letters and the hamza; the hamza seats,
ta marbuta and alif maqsura; the eight vowel marks; then, with codes of
their own but no shapes yet, the extra letters of Persian, Urdu, Pashto,
Kurdish and Jawi (Malay).  Add a language's letters at the END.  A code
above 0x7e comes through the Newton's text as the Mac Roman character of
that byte; decode() takes both.
Usage:    import arabic; arabic.letters(word) -> [(char, form), ...];
          arabic.encode(word) -> "codes"; arabic.decode("codes") -> word.
          python tools/arabic-ink/arabic.py "كتابٌ" prints both.
"""

import sys
import unicodedata

POOL = ([chr(c) for c in range(ord("a"), ord("z") + 1)]
        + [chr(c) for c in range(0x21, 0x7f) if not (ord("a") <= c <= ord("z")) and c != 0x27 and not (0x30 <= c <= 0x39)]
        + [chr(c) for c in range(0x7f, 0xa3)])
assert len(POOL) == 119

REGISTRY = [
    # Arabic: the 28 letters and the lone hamza
    "ا", "ب", "ت", "ث", "ج", "ح", "خ", "د", "ذ", "ر", "ز", "س", "ش", "ص", "ض", "ط", "ظ", "ع", "غ", "ف",
    "ق", "ك", "ل", "م", "ن", "ه", "و", "ي", "ء",
    # the hamza seats, ta marbuta, alif maqsura
    "أ", "إ", "آ", "ؤ", "ئ", "ة", "ى",
    # the vowel marks: fatha, kasra, damma, sukun, shadda, fathatan, kasratan, dammatan
    "َ", "ِ", "ُ", "ْ", "ّ", "ً", "ٍ", "ٌ",
    # Persian: pe, che, zhe, gaf
    "پ", "چ", "ژ", "گ",
    # Urdu: tte, ddal, rre, noon ghunna, bari ye, do chashmi he, ta marbuta goal
    "ٹ", "ڈ", "ڑ", "ں", "ے", "ھ", "ۃ",
    # Pashto: tte, ddal, rre, ge, xin, dze, tse, nur, ye with hamza, e
    "ټ", "ډ", "ړ", "ږ", "ښ", "ځ", "څ", "ڼ", "ۍ", "ې",
    # Kurdish (Sorani): rr, ll, oe, e, ve
    "ڕ", "ڵ", "ۆ", "ێ", "ڤ",
    # Jawi (Malay): nga, pa (as Jawi writes it), nya, ga, va
    "ڠ", "ڽ", "ݢ", "ۏ",
    # more marks: the dagger alif, maddah above, hamza above/below as marks
    "ٰ", "ٓ", "ٔ", "ٕ",
]
assert len(REGISTRY) == len(set(REGISTRY)) <= len(POOL)

TO_CODE = {ch: POOL[i] for i, ch in enumerate(REGISTRY)}
TO_LETTER = {code: ch for ch, code in TO_CODE.items()}
# the table's slot for each code: the reader's own code, the byte itself
SLOT = {code: ord(code) for code in TO_LETTER}

MARKS = set("ًٌٍَُِّْٰٕٓٔ")
NAMES = {"َ": "fatha", "ِ": "kasra", "ُ": "damma", "ْ": "sukun", "ّ": "shadda",
         "ً": "fathatan", "ٍ": "kasratan", "ٌ": "dammatan", "ٰ": "dagger-alif",
         "ٓ": "maddah", "ٔ": "hamza-above", "ٕ": "hamza-below"}

# Folded onto what they are: look-alikes and presentation forms.  (Persian
# kaf and ye are the same shapes as Arabic kaf and ye: folded.)
FOLD = {"ٱ": "ا", "ک": "ك", "ی": "ي", "ۀ": "ه", "ە": "ه", "ك": "ك", "ي": "ي"}

# The letters that never join the one after them.
NON_JOINERS = set("اأإآدذرزوؤءةىژڈڑڈۆۏڕډړږ")

ISOLATED, INITIAL, MEDIAL, FINAL, MARK = range(5)
FORM_NAMES = ("isolated", "initial", "medial", "final", "mark")


def fold(word):
    """The word as its registered characters and spaces: look-alikes
    folded, the rest dropped."""
    out = []
    for ch in unicodedata.normalize("NFC", word):
        ch = FOLD.get(ch, ch)
        if ch in TO_CODE:
            out.append(ch)
        elif ch.isspace():
            if out and out[-1] != " ":
                out.append(" ")
    while out and out[-1] == " ":
        out.pop()
    return "".join(out).lstrip()


def letters(word):
    """[(character, form), ...]: each letter in its positional form, each
    vowel mark as (mark, MARK) after its letter, a space as (" ", None)."""
    text = fold(word)
    base = [ch for ch in text if ch not in MARKS]
    out = []
    k = -1                              # the index in base of the last letter seen
    for ch in text:
        if ch == " ":
            out.append((" ", None))
            k += 1
            continue
        if ch in MARKS:
            out.append((ch, MARK))
            continue
        k += 1
        prev = base[k - 1] if k > 0 else " "
        nxt = base[k + 1] if k + 1 < len(base) else " "
        joined_before = prev != " " and prev not in NON_JOINERS
        joined_after = nxt != " " and ch not in NON_JOINERS
        form = (MEDIAL if joined_before else INITIAL) if joined_after else (FINAL if joined_before else ISOLATED)
        out.append((ch, form))
    return out


def encode(word):
    """The folded word as the reader's codes (a space kept)."""
    return "".join(TO_CODE.get(ch, " ") for ch in fold(word))


def decode(text):
    """The reader's codes back to the script.  A code above 0x7e may arrive
    as the Mac Roman character of its byte (the Newton's text); a code that
    is no character's is kept as it is."""
    out = []
    for ch in text:
        if ch not in TO_LETTER and ord(ch) > 0x7e:
            try:
                ch = chr(ch.encode("mac_roman")[0])
            except (UnicodeEncodeError, IndexError):
                pass
        out.append(TO_LETTER.get(ch, ch))
    return "".join(out)


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    if len(sys.argv) == 1:
        for ch, code in TO_CODE.items():
            print("%-14s %s  %#04x" % (NAMES.get(ch, unicodedata.name(ch, ch).lower()), code, ord(code)))
    for w in sys.argv[1:]:
        print(w, "->", fold(w), "->", encode(w), "->", decode(encode(w)))
        print("  ", " ".join("%s:%s" % (NAMES.get(l, l), FORM_NAMES[f][:3]) if f is not None else "_" for l, f in letters(w)))
