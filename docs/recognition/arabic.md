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

## Next

1. A way to turn the switch on from the Newton itself (an "Arabic" choice
   in the Handwriting Recognition slip), once there is something Arabic to
   read.
2. Arabic letter prototypes: how the DTE tables are laid out
   (`ParaGraph.h`, `XrMatrix.h`), and a tool that builds them from samples
   run through the low level (needs several samples of each letter in each
   of its forms).
3. An Arabic character set: the reader is eight-bit; an Arabic code page
   inside, Unicode at the edges.
