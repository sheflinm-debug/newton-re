# Ink kept after recognition: searchable, shown as ink or as text

**Status: a proposal.**

## State (2026-11-11)

Built, behind `NEWTON_KEEP_INK=1` (off by default):
- a reading inside an ink word (`ink/InkReading.h`);
- a word read into text keeping its ink, whether it starts a paragraph
  (`TEditView::AddNewParagraph`), goes into one, or is read later out of
  an ink word;
- the words of a range shown as their writing or as their text
  (`ShowRangeAs` in `views/ParagraphView.cpp`), and for scripts
  `KeepsInk()` and `ShowWritingAs(view, start, stop, 'ink | 'text)`
  (nil, nil: the whole paragraph), which answers where the range ends now.

`src/host/demo/keep-ink.ns` writes "ton" on the Notepad, shows it as its
writing and as text again, and leaves a picture of each (ctests
`host.NewtonKeepInk` and `.off`; `test_Views`' `TestInsertItems`).

Not built: the menu item (it needs the owner's word on which menu - the
corrector a double tap opens, or the Notepad's Recognize button), Find
over readings, reading ink in the background, an edited word's ink
dropped as it is edited (today it is only refused when shown as writing),
a style change over a kept word, the preferences, a whole note or a
selection across paragraphs in one call.

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
