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
