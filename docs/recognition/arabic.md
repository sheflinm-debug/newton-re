# Arabic handwriting

Status: branch `arabic-recognition`, a fork's experiment.  Nothing here is
ROM behaviour: everything that changes the reconstruction is a `DEVIATION`.

The aim is Arabic written on the Newton and read: first through
ParaGraph's cursive reader (`docs/recognition/README.md`), whose design -
letter prototypes in a table, up to 16 variants a letter, delayed strokes
placed after their letter - suits a connected script; later, if that
falls short, as a host engine of its own (`docs/recognition/engines.md`).

## Tools

`tools/arabic-ink/` (its README gives every step): a capture page for a
stylus phone or tablet, a server that keeps the samples, a converter that
replays them through `Recognize()` with the cursive letter set, and a
report of what the reader made of them.

## Finding 1: the reader needs Arabic mirrored (2026-10-14)

**Established by** replaying 42 samples (the 40 words of the capture
page's list, two of them twice, one writer, a phone's stylus) through
`Recognize()` with letter set 0, as written and mirrored left to right
(`to_ns.py --mirror`), with `NEWTON_TRACE_CURSIVE=1`, and counting the
reader's result codes (`report.py`).

| | read (0) | no letter path (-9) |
|---|---|---|
| as written | 13 | 29 |
| mirrored | 42 | 0 |

(Of those read, 11 and 39 had readings left once `Recognize()` was done;
the rest were read and then had nothing to offer.)

`-9` is `xrw_algs` finding no path through the word graph
(`CursiveReader.cpp`): the low level cuts the strokes into xrs either way
(every sample reached it), but written right to left most of them match
no sequence of the US letter set's prototypes at all.  Mirrored, every
sample comes through every stage - low level, word graph, answers - and
reads as Latin letters ("to..", "ado", "isle."), which is what prototypes
for Latin letters can give.

So the pipeline itself (segmentation into xrs, the word graph, the
answers) carries Arabic once it runs left to right, and what is missing is
what the reader matches against: Arabic letter prototypes (the DTE
tables, `ReadDteResource`) and an Arabic character set.

Seen on the way:

- The dots of باب come out of the low level as the last xrs (`34/1 01/0
  34/1 01/0` at the end of the string), as written and mirrored: they were
  not placed after their letters.  `FindDelayedStroke` and
  `placement_XT_ST` (`LowXtSt.cpp`) look for i-dots and t-bars, marks
  above or across a letter; dots *below* the line may need their own
  placement.
- With the Print lines on stdout and the trace on stderr, the host's two
  streams do not interleave in order; `report.py` counts the trace's codes
  over the whole log rather than per sample.

## The mirror switch (2026-10-14)

`NEWTON_INK_MIRROR=1` (`recognition/InkMirror.h`) mirrors the writing for
the live system, not only for replays.  Every trace made of strokes is
made in `NewGetTraceFromStrokes` (`InkGroups.cpp`) - the cursive reader's
(`GCAllocRecTrace`, `GetTraceFromStrXrUnit`), the ink grouping's, the base
line finder's (`FindBaseline`, which uses only y) and the learning's
(`TWordRecognizer::DoLearning`) - so turning x there turns it for all of
them alike.  The axis is the screen's width in the trace's units, so the
mirrored writing keeps the range the reader expects: the first try, a
fixed axis at 0x4000, put the writing past the word segmenter's widest
line (`kWSLineEnd`, 0x1f60 - about 1000 pixels) and overran its
`f8d8_lineY`.

What comes back with positions in it is the word unit's box
(`TStrXrDomain`, StrXrDomain.cpp: `fLeft`/`fRight` from the word
descriptor's `fInkBox`), turned back there.  A word split into parts takes
its box from the strokes themselves, which are never mirrored.
NOT YET: a word continued after a dash at a line's end (`fJoinX`) assumes
the line ends on the right.

**Established by** `src/host/demo/cursive-mirrored.ns` (ctest
`host.NewtonCursiveMirrored`): cursive.ns's "ton" written as its own
mirror image, right to left from x = 200 to 156, reads as cursive.ns's
does (the same 13 xrs, "ton"/85), and the word box comes back as x 156 to
200, where the ink was; the same strokes unmirrored at x 160 to 204, with
the switch off, give a box of 160 to 204.  (The Notepad puts a fresh
page's first paragraph at its margin wherever the ink was, so the
paragraph's own bounds do not show the box; the trace line "[cursive]
word box" does.)

## The letter table's format (2026-10-14)

What an Arabic letter table has to be, read from the code that reads the
ROM's (`XrMatrix.cpp`'s `CountSym` and `XrMatch`, `ParaGraph.cpp`'s
`GetSymDescriptor`) and **established by** `tools/arabic-ink/dte.py
check`: the US ROM's DTEMain decoded into JSON and encoded again comes
back byte for byte (91 letters, 3499 prototypes, 274596 bytes).  The
layout is in the tool's header.  In short: a descriptor per character
code, each up to 16 variants of up to 12 prototypes; a prototype is 0x4c
bytes, chiefly five tables of nibbles - what each of the 64 xr types,
16 heights, 16 shifts, 16 links and 32 orientations is worth there - and
an xr read against it scores the five nibbles its own values pick,
summed, against a cost of 50 for reading it.  That is an additive model of
independent features, which is what makes building a table from samples
straightforward: each nibble is in effect a scaled log-likelihood of that
feature at that point of the letter.

Not yet understood, and kept as they are: a descriptor's bytes 1 and 3,
its byte 2 (the letter's own code for 83 of 91, otherwise another
letter's - space has ?, _ has -, six codes above 0x7e have v), two bytes
per variant at +0x34/+0x44 (by their values the height zones it reaches:
a 0x78/0x57, t 0xbd/0xce), and a prototype's first two bytes.

The RAM replacement (`SetRamParaData`, `gParaRamData.fDTEMain`) does not
replace a letter: `GetSymDescriptor` counts the RAM table's variants after
the ROM's, sixteen in all.  An Arabic letter set will swap the whole
table instead (a DEVIATION to come).  The other resources the reader
reads - PPDMain (the post-processing rules, `XrPost.h`) and the trigrams -
are not yet decoded.

The ADAB database (El Abed et al., IfN/REGIM; IEEE DataPort, open access
for non-commercial research) is to be the main source of samples: 937
Tunisian place names written by many writers, the pen's trajectory in
InkML with UPX labels.  It is not redistributed: a script will convert a
copy the user downloads into `build/adab/`.

## Finding 2: the reader reads Arabic from a built table (2026-10-14)

The pipeline runs end to end: samples (the phone's, ADAB's) -> the xrs
the reader cuts them into (`harvest.py`, `NEWTON_XR_DUMP`) -> a letter
table (`build_dte.py`, `arabic.py`'s codes) -> the reader with that table
(`NEWTON_DTE_MAIN`) -> Arabic text (`report.py --arabic`).

**Established by** replaying 42 of the user's words and 50 of ADAB's set 3
(writers not trained on) against a table built from ADAB's sets 1 and 2
(10,048 words aligned; 35 characters, 210 variants), each with and without
a vocabulary (`to_ns.py --dictionary`):

| | read | the word first | among the first five | letters right |
|---|---|---|---|---|
| user's 42, no vocabulary | 21 | 0 | 0 | 9% |
| user's 42, their 40 words | 21 | 0 | 0 | 10% |
| ADAB 50, no vocabulary | 35 | 0 | 0 | 16% |
| ADAB 50, ADAB's 952 words | 35 | 0 | 2 | 16% |

The mechanism is right: with a table holding only alif and ba, "باب"
reads as "باب".  With the whole alphabet the readings are Arabic
letters of about the right number but seldom the right ones
("السواسي" -> "اسلانيّي", with "السواسي" second).  What was learnt on the
way, each a DEVIATION under the host's table:

- **The Viterbi keeps only lower-case letters freely** (`XrlvDevelopCell`:
  a capital only at a link), and a letter's net gain must be positive to
  survive (threshold 0, `outLine` clamped at 0).  So the table's codes
  are all marked lower case in a character-class table of the host's,
  and the alphabet is the table's own codes (`ParaGraph.cpp`,
  `HostDTEMain`, `SetOsRecTableAndCharSet`): no borrowing of Latin
  letters, 119 codes to allocate (`arabic.py`'s registry, with the extra
  letters of Persian, Urdu, Pashto, Kurdish and Jawi reserved).
- **The ROM's scale**: the type nibbles discriminate (12 down to 0), the
  other four features are permissive (mostly 11-12), so a typical match
  nets a few points over the 50 it costs.  Peaky tables in all five
  features killed every path (-9 everywhere); steeper tables in the four
  made no difference once the scale was right.
- The English trigrams are left out (`Xrlv.cpp`), and every xr may end
  a letter (the low level's "last in letter" marks are dense enough -
  42% of xrs - that this changed little).
- A `Recognize()` configuration of a script's own needs the ROM's
  re-recognition configuration under it (`HostRerecognizeConfig()`,
  host/HostViews.cpp), or nothing is recognised at all.
- ADAB's labels carry shadda, ta marbuta and the hamza seats, but no
  short vowels or tanwin: the marks' shapes will need the user's own
  vowelled samples.

Where the accuracy is lost, as far as is known: the medial forms of
ب ت ث ن ي are one tooth each, told apart by dots that the low level
places after the letter but not always next to it, so the models of the
tooth letters are nearly alike; a one-prototype variant matches almost
any xr for a small gain, and chains of them outscore the right word; and
the reader's own rules and penalties (`XrPost`, the class and break
penalties) were tuned for Latin letters.

## Finding 3: the models must be long enough (2026-10-14)

An oracle test - 50 of the words the table was trained on, with the
vocabulary - read 35, one of them right, 21% of letters: so the loss is
in how the models meet the reader, not in generalisation.  Two causes
tried:

- **Model lengths.**  A variant was as long as the medoid of its first
  rough slices, so a dotted letter's model was one prototype - the tooth -
  and the alignment rounds could never take the dots in (a model cannot
  grow).  Sized instead for three quarters of the slices, two prototypes
  at least, with skip costs 18/8/0 at 92%/65% matched (`build_dte.py`):
  initial ب went from one prototype to four, and the oracle test read 43,
  3 right, 5 among the first five, 23% of letters; held out, 42 of 50
  read, 1 right, 23% of letters; the user's 42: 29 read, 2 right, 13%.
- **The free letters' cost** (`NEWTON_CHARSET_PEN`, Xrlv.cpp, the ROM's 4
  a position): 8 read fewer words and changed nothing else.  Left at 4.

Longer training (6 rounds, 5 variants a form) made the old models worse
(13% of letters on trained words): the alignment drifts without the
dots to anchor it.

## Finding 4: the English lexicons, and the reader's own layouts (2026-10-14)

A trace of where the Viterbi's symbols come from (`[cursive] xrlv
symbols by kind`) showed the ROM's own lexicons in the vocabulary chain
beside a script's dictionary, their English words spelt in codes that
are now Arabic letters - "busboy", "hexagon", "areaway" read as Arabic
with a dictionary's cheap penalties - and the ROM's number and
punctuation sets offering most of the table's non-Latin codes as free
symbols.  Both are left out under the host's table now (XrLex.cpp:
dictionaries with ids below 200; Xrlv.cpp: only the alpha set).  It
made no difference to the scores: the free-letter path (kind 8, tens of
thousands of offers a word) is where the soup comes from.

The reader's own layouts: with the free letters taken away
(`NEWTON_XR_VOCAB_ONLY=1`) and each sample's word as its only vocabulary
(`to_ns.py --dictionary-each`, `harvest.py --forced`), the reader lays
the word out itself where a path exists - 3,350 of 10,126 ADAB samples
(59% of the one-word labels, almost none of the two-word ones: the word
gap is not found) - and `build_dte.py --from-reader` trains on those
layouts.  The tables so built: trained words 45 of 50 read, 1 right,
24% of letters (all samples) or 26 read, 4 right, 20% (the laid-out
samples alone); held out 37 read, 0 right, 20%; the user's 42: 22 read,
3 right, 12%.  A plateau: the models' discrimination, not their
alignment, is the limit.

Where the loss is: the right word is almost never in the reader's graph
of alternatives (3 of 48 trained words), so it is lost in the search,
not demoted after it.  Yet a wider beam (`NEWTON_XR_BEAM`, four times)
and more readings a location (`NEWTON_XR_ENTRIES`, 100) change nothing,
and nor does a dearer free letter (`NEWTON_CHARSET_PEN` 12 or 20) now
that the lexicons are out.  **Closed-vocabulary reading**
(`NEWTON_XR_VOCAB_ONLY=1` as the normal mode, only the dictionary's words
allowed) is the best so far: held out, 6 of 50 right (7 among the first
five) with the 952 place names; the user's 42, 6 right (9 among the
first five) with their 40 words - but half the words find no path at
all, which with no free letters to blame means the right letters' own
matches net nothing: the models.

## Finding 5: a host network reads Arabic (2026-10-14)

The user's decision: a host engine of the Newton's own kind - a third
word recogniser behind the author's seam (`docs/recognition/engines.md`),
which adds to the architecture rather than changes it - for a working
prototype, with ParaGraph's Arabic table kept as the long-term authentic
route.  `tools/arabic-ink/train_net.py` trains a two-layer bidirectional
GRU (128 wide) with CTC over the pen's trajectory (per point: dx, dy, pen
down, a stroke's first and last, the height above the base line; the
word scaled to its height and mirrored, resampled to a step of a tenth
of the height), on ADAB's samples, and exports it to ONNX.

**Established by** training on 3,000 words of sets 1 and 2 for 12 epochs
(about 2 minutes an epoch on 16 cores) and reading 300 held-out words of
set 3, other writers: letters wrong 27.1%; words right 7.3% read freely,
**89.3% with the 952 place names as the vocabulary** (a CTC score per
word, over the 40 nearest the free reading).  ParaGraph's table, with
the same vocabulary, read 12%.

The full run (10,126 words, 20 epochs, about 8 minutes an epoch, the
vocabulary scored every fifth): letters wrong 7.9%, words right 54.0%
read freely, **98.3% with the vocabulary** (99.0% at epoch 15).  On the
user's 42 words - another hand, another device, their 40 words as the
vocabulary - letters wrong 41%, words right 21% freely, 81% with the
vocabulary (the 3,000-word model: 54%, 7%, 71%).  The gap between
ADAB's writers and the user's hand is what a few dozen of the user's
own words in the training set are for.

## The engine (2026-10-14)

`recognition/ArabicNet.h` runs the network in the host's own C++ - the
.anet file `tools/arabic-ink/export_weights.py` writes (the alphabet and
the arrays, little-endian float32), the features made as the training
made them, the two bidirectional GRU layers as ONNX defines them, the
greedy decoding; ctest `recognition.ArabicNet` holds it to the Python
model on a sample (features within 1e-4, logits within 1e-3, the same
reading - 5e-7 and 3e-6 in practice).  `recognition/ArabicRecognizer.h`
is the word engine, 'ARAB', the "Arabic (network)" button (9) of the
Handwriting Recognition slip (`WordEngines.cpp`'s table): strokes
grouped into words by the clock as the ink-only engine groups them, a
closed word read by the network, its reading the unit's one
interpretation, a word, scored by the network's mean confidence;
`NEWTON_ARABIC_NET=<file>.anet` names the network, `NEWTON_TRACE_ARABIC=1`
prints each reading.  ctest `host.NewtonArabic`
(`src/host/demo/arabic.ns`): the user's own "باب", written on the
Notepad at 40 pixels high, is typed into the note as "باب".

NOT YET: alternatives for the corrector (the greedy reading alone); a
vocabulary in the engine (the Newton's dictionaries are eight-bit and
Latin-shaped; the engine will carry its own word list first); the word
gap judged by the writing rather than the clock; Arabic glyphs in the
ROM's fonts (the text is right in the note; the screen cannot yet draw
it); the marks and the letters the training had no samples of; a way to
choose the network from the Host panel rather than the environment.

## The screen (2026-10-14)

The ROM's fonts have no Arabic, and its text engine measures and draws
one character at a time, left to right (`Text.cpp`'s
`MeasureGlyphWidths`, `DrText.cpp`'s `DrTextChunk`).  `qd/ArabicText.h`
(a DEVIATION) stands in front of both: the letters joined into their
contextual forms (Unicode's Presentation Forms-B, by the joining rules),
each run of Arabic reversed for drawing with its advances - a letter and
its marks moving as one - and the glyphs taken from a host face with
Arabic in it (Arial, Segoe UI or Tahoma on Windows), opened at the
strike's size and kept, since the ROM's strikes have none.  ctest
`qd.ArabicText` holds the joining and the reordering; the demo's "باب"
now shows as باب on the Notepad.  Not a bidirectional layout: the caret
and the selection still count in logical order (mirrored inside an
Arabic word), and a line of mixed scripts is runs, not Unicode's
algorithm.  NOT YET: the lam-alif ligature, the caret in visual order,
Arabic in the ROM's own bitmap fonts (the hosts without a rasteriser).

## Next

1. Better models: the dots bound to their letters (a position-aware
   alignment, or the low level's placement extended to marks below the
   line), more alignment rounds and variants, and a floor on a variant's
   length; then the reader's own layouts (which xrs read as which letter)
   as the alignment for a second round of training.
2. The vocabulary made to count: why a dictionary word scores below a
   free reading (the dictionary letters' penalties against the charset's).
3. A way to turn the switch on from the Newton itself (an "Arabic" choice
   in the Handwriting Recognition slip).
4. Arabic letter prototypes: how the DTE tables are laid out
   (`ParaGraph.h`, `XrMatrix.h`), and a tool that builds them from samples
   run through the low level (needs several samples of each letter in each
   of its forms).
3. An Arabic character set: the reader is eight-bit; an Arabic code page
   inside, Unicode at the edges.
