# Ink kept after recognition: searchable, shown as ink or as text

**Status: a proposal.**

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

**Both forms in one word.** An ink word gains a reading: the recogniser's
best text and its alternatives, held with the strokes. A recognised word
gains its ink: the strokes it came from, held with the text. Either way the
item in the paragraph has strokes, a reading, and a flag for which is
shown. In the saved form these are extra slots on the paragraph's existing
ink-word and style data, so a note written on this system still opens on a
MessagePad (which shows what the flag says and ignores the rest: to be
checked against the ROM's readers before the format is fixed).

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
