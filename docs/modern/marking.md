# Marking: a mark beside any line, and one list of everything marked

**Status: a proposal.**

## What it is for

Filing puts a whole note in a folder. Marking is finer: one line or
paragraph inside any note, flagged "come back to this", without moving or
copying anything. A single overview then lists every marked line from
every note, newest first, and a tap goes to the line in its note.

Uses: the one action item in a page of meeting notes; a quotation to find
again; a question to ask someone; a line in a long note being drafted.

## What Newton has

Folders for whole entries (`labels`), Find across applications, and
paragraph gestures (scrub, caret, the line that starts a new note). Nothing
marks a part of a note.

## Design

**Making a mark.** Either:
- write a star (or a tick, by preference) in the left margin beside a
  line - recognised by the structure recogniser (`structure.md`) because of
  where it is, so a star written in the body of a note stays writing;
- or select text and choose "Mark" from the pop-up menu.

The mark shows as a small star in the margin. A tap on it offers "Remove
mark" and a choice of kind.

**Kinds.** A short list the user can edit, each with its own symbol: To do,
Important, Question, and one unnamed default. One kind is enough for a
first version.

**Where marks are kept.** In a soup `Marks`, one entry a mark: the note's
soup and entry id, the range of the paragraph, the kind, the time, and a
copy of the marked text so the overview can be drawn without opening every
note. The note itself gains nothing but a marker in the paragraph's style
data, so a marked note still opens on a MessagePad. When a note changes,
its marks' ranges and text copies are brought up to date from the
soup-change notification; a mark whose text is deleted goes with it.

**The overview.** An application in Extras, "Marked": a list by date or by
kind, each row the marked text, its note's title and date. A tap opens the
note at the line. The Action button routes the list like any other. A
check box on a row removes the mark (for "To do", marks it done first).

**Beyond the Notepad.** The same soup serves any application whose views
are paragraphs: a line in a Names card's notes, a meeting's notes. The
first version is the Notepad only.

**With sync.** `Marks` is an ordinary soup and syncs like the others
(`sync.md`), referring to notes by their `syncID`.

## Tests

A pen demo: a star written in the margin becomes a mark; one written in
the body does not; text selected and marked from the menu; the overview
lists both, and a tap opens the note at the line; the note edited above
the mark, the mark still on its line; the marked line scrubbed, the mark
gone; a restart, all as before.

## Owner decisions

1. The symbol: a star, or the user's choice.
2. One kind or several in the first version.
