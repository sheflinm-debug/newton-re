# Ink kept after recognition: searchable, shown as ink or as text

**Status: a proposal.**

## State (2026-11-11)

Built, behind the Host preferences panel's "Keep handwriting" (off by
default; `NEWTON_KEEP_INK=1` or `=0` overrides it - `host/KeepInk.h`,
`docs/host-settings.md`):
- a reading inside an ink word (`ink/InkReading.h`);
- a word read into text keeping its ink, whether it starts a paragraph
  (`TEditView::AddNewParagraph`), goes into one, or is read later out of
  an ink word;
- the words of a range shown as their writing or as their text
  (`ShowRangeAs` in `views/ParagraphView.cpp`), and for scripts
  `KeepsInk()`, `InkReading(inkWord)` and
  `ShowWritingAs(view, start, stop, 'ink | 'text)` (nil, nil: the whole
  paragraph), which answers where the range ends now;
- the menu: the pop-up a tap on the caret brings up (the punctuation
  grid and the space bar) has a strip of two cells under the space bar,
  "Abc" and a scribble (`views/CaretWriting.h`; the owner's choice of
  menu). "Abc" shows writing as text, the scribble shows text as its
  writing: the selected words of every paragraph of the note when
  something is selected (a word partly selected counts whole), the whole
  note when nothing is. With ink not kept the pop-up is the ROM's;
- reading in the background: a paragraph with an ink word nobody has read
  is an idler (`ReadInkInBackground`); once the pen has been still 1.5 s
  it reads one word at a time and keeps only the reading - the page does
  not change. A word the recogniser makes nothing of gets an empty
  reading and is not tried again;
- Find: as an entry is written to a store, each paragraph holding ink
  words with readings gets a string slot `inkText`, the readings with
  spaces between (`stores/InkText.h`). Every query already searches an
  entry's strings, so the store format is unchanged and a MessagePad
  finds the note by its handwriting too;
- an edited word loses its ink: a kept run whose characters no longer
  spell the reading is given its plain font, and runs that now agree are
  merged (`DropStaleInk`, after every text change);
- a change of font over a kept word keeps the ink (`ChangeStylesOfRange`).

Demos and tests: `keep-ink.ns` (ShowWritingAs, ctests `host.NewtonKeepInk`
and `.off`), `keep-ink-menu.ns` (the pop-up with the pen,
`host.NewtonKeepInkMenu` and `.off`, picture `build/keep-ink-menu.pgm`),
`keep-ink-find.ns` (ink written as ink, read in the background, found by a
words query, `host.NewtonKeepInkFind`), `keep-ink-pref.ns` (the panel,
`host.NewtonKeepInkPref`); `test_Views`' `TestInsertItems` (insert, show,
selection, font change, edit), `test_Soups`' `TestTextSearch` (`inkText`).

Not built: a hit inside an ink word highlighting the ink when Find shows
the note (the note is found and opened; nothing is selected in it);
"keep handwriting for a month" (kept ink is kept until the word is
edited). Not checked with the pen: the selection case of the pop-up (the
unit test selects with `HiliteText`); what the original ROM (`--rom`) makes
of a font frame with an `ink` slot and of a paragraph's `inkText`.

## What Newton has

A paragraph holds text and *ink words* side by side. An ink word reflows
with the text around it. Deferred recognition turns ink into text later: a
double tap on an ink word recognises it, or every ink word in the
selection (`docs/recognition/README.md`, "Deferred recognition"; the
paragraph's commands 0x19 `RerecognizeWord` and 0x1a `RecognizePara`).
Recognition replaces the ink: afterwards only the text exists, and Find
does not see writing left as ink.

## The aim

1. Writing left as ink is found by Find.
2. Recognised writing keeps its ink, so the page can show either form.
3. One menu item switches the selection, or the whole note, between
   "Show as handwriting" and "Show as text".

## Design

**Both forms in one word.** Either way the word has strokes, a reading,
and one of them showing.

*Shown as ink* (built: `ink/InkReading.h`, ctest `ink.Ink`). The word is an
ordinary ink word - one 0xF701 character whose style run is the `'inkWord`
binary - and its reading is carried **inside that binary**, between the
strokes and the eight bytes of information that end it:

    strokes | reading, UniChars, big-endian | count, a halfword | 'IRd1' | the eight bytes

A MessagePad never sees it. The ROM finds the information as the last
eight bytes (`GetPackedInkWordInfo` 0x0014022c) and its decoder stops at
the end of the strokes' group (`DecoderRun`); the test expands a word with
and without a reading and gets the same points. So the reading goes
wherever the word goes - cut, pasted, beamed, saved - with nothing beside
it to keep in step. Ink the ROM's code re-encodes (two words joined, one
split) comes out without a reading and is read again.

*Shown as text* (built for a word as it goes in: `KeepInk()` in
`host/KeepInk.h`, `NEWTON_KEEP_INK=1`; `StyleKeepsInk`, `PlainFontFrame`
and the insert path in `views/ParagraphView.cpp`, the deferred reading in
`views/Rerecognize.cpp`; `test_Views`' `TestInsertItems`). The word is
its characters, in a style run of their own whose font frame carries the
ink: `{family, face, size, ink: <the ink word, its reading the text>}`. A
MessagePad reads the frame as a font and ignores the slot. Three things
must hold on the host for the ink to stay with its word:
- two styles with different `ink` are different styles (the ROM's
  `EqualStyles` compares only family, face and size, and would merge the
  runs of neighbouring words);
- text typed next to the word gets the font without the `ink`
  (`GetStyleForInsertion`);
- a run whose characters no longer spell the ink's reading has been
  edited, and its ink is dropped.
On a MessagePad the first does not hold, so editing such a note there can
lose kept ink (never text).

The first two are built. The third - noticing an edited word and dropping
its ink - is not, and nothing yet shows a word the other way or looks for
a reading; what a MessagePad makes of the frame's extra slot is still to
be tried with `--rom`. Not yet handled either: a style change over a kept
word (the new font frame has no `ink`).

**Reading ink in the background.** An ink word with no reading is
recognised when the machine is idle, a few words at a time, and only the
reading is stored: what the page shows does not change. The correction
slip's alternatives come from the same stored list.

**Find.** The paragraph's text for searching is the text runs plus each ink
word's reading. A hit inside an ink word highlights the ink.

**The menu.** The pop-up that selecting text brings up gains one item:
- with a selection: "Show as text" or "Show as handwriting" for the words
  selected, whichever applies (both when the selection is mixed);
- with no selection: the same for the whole note.

Switching only flips the flag and reflows. Text typed from the keyboard has
no ink and stays text. Editing a word's text with the keyboard drops its
ink, since the two no longer agree.

**A preference** in the recognition preferences: new writing is shown as
text (the ROM's behaviour) or as handwriting (ink kept visible, read in
the background).

## Cost

Ink is larger than text: tens to hundreds of bytes a word against a few.
Kept ink uses the store's existing compression for stroke data
(`docs/ink/README.md`). A preference "Keep handwriting: always / a month /
never" bounds it; "never" is the ROM's behaviour.

## Tests

A pen demo: write three words as ink; Find one of them; convert the
selection to text and back, the strokes identical to those written;
convert the whole note; edit a word's text and see its ink gone; the note
saved, the machine restarted, the note the same. The saved note opened
with `--rom` to see what the original system makes of the extra slots.

## Owner decisions

1. The default for new writing: text or handwriting.
2. How long ink is kept by default.
