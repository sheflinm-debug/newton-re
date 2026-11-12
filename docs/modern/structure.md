# Structure and meaning recognised from what is written

**Status: a first piece built (2026-11-12): checkboxes and lists in a plain note,
behind the Host panel's "Checkboxes and lists" (`NEWTON_STRUCTURE=1`/`=0`
overrides; `host/Features.h`). The rest is a proposal.**

## State

- **A to-do item inside a plain note**, not a checklist note: a paragraph
  whose data frame has a slot `check` ('open or 'done) has a checkbox drawn
  in the margin to its left (`views/MarginMarks.h`); a tap on the box ticks
  it or unticks it, and the note is saved (`HostMarks.ns`'s `ToggleDone`).
  A MessagePad ignores the slot.
- **Made from the caret's pop-up:** a row "[] To do" under the space bar
  makes the paragraphs selected, or the caret's, items - or, when all are
  items already, plain paragraphs again (`ToggleChecks`).
- **Offered from what is written:** a paragraph whose text begins with a
  checkbox written as text - "[ ]", "[]", "[x]" (ticked), or U+2610-2612 -
  is shown a dotted box in the margin: the offer. It is only drawn, never
  stored, so writing on or leaving it changes nothing. A tap on it accepts:
  the box written is taken out of the text and the paragraph made an item
  (ticked for "[x]"). This is the page's "offers, not conversions".
- **Lists** the same way: a paragraph's `list` slot, 'bullet or 'number,
  draws a bullet or its number in the margin (a number is one more than
  the numbered items straight above it); a text beginning "- ", "* ", a
  bullet, or "1. "/"12) " is offered with a dotted bullet, and a tap takes
  the mark written out and makes the item.
- Demo `src/host/demo/checkboxes.ns`, ctests `host.NewtonCheckboxes` and
  `.off`; pictures `build/checkboxes-offer.pgm`, `build/checkboxes.pgm`,
  `build/checkboxes-list.pgm`.

Not built yet: a square or a dash *drawn* before a line (it needs the
stroke positions: the structure recogniser proper, below); a list item's
level (a line begun further right); a new line after an item continuing
the list; live details; headings, sums, tables, diagrams; a
tap accepting being undone by a scrub (Undo does undo the text change).

## The proposal

## What Newton has

- Recognisers for words, shapes and gestures arbitrate over the same
  strokes, and the reconstruction keeps a seam for adding recognisers
  (`docs/recognition/engines.md`).
- The Notepad's stationery: a plain note (`'paperroll`), an outline
  (`'list`), a checklist (`'checkList`) and text. The user chooses one from
  the New menu before writing; a note does not change kind by being written
  in.
- A line drawn across the page starts a new note; a scrub deletes; a caret
  inserts.
- The Assistant recognises intent in a sentence, when asked
  (`docs/assist/README.md`).

## The aim

The kind of thing being written is recognised from the writing, one level
above words and shapes, and offered. The user never has to choose a kind of
note first.

## How it fits

A **structure recogniser** runs after the word and shape recognisers have
settled, over their results and the positions of the marks on the page. It
is not in the arbitration for single strokes, so it cannot make ordinary
handwriting worse. Each structure it knows is a registered frame: what to
look for, and the stationery or paragraph attribute that represents it.
Third parties register more, as they register stationery (`RegDataDef`).

**Offers, not conversions.** A recognised structure is shown by a small
marker in the margin. A tap accepts it; writing on ignores it; a scrub over
an accepted structure returns the ink as written. A preference lets each
structure be accepted without asking, once the user trusts it.

## Structures

| Written | Becomes | Represented by |
|---|---|---|
| a dash, dot or number, then words; the next line likewise | a list; a line begun further right is a sub-item | the outline stationery's items and levels |
| a small square before a line | a to-do item; a tick in the square marks it done | the checklist stationery's items |
| a grid of lines | a table whose cells take writing | new stationery (a table) |
| a column of numbers with a line under it | the total written below; it follows changes | a table column with a computed cell |
| an expression ending in `=` | the result after it | text, marked as computed |
| closed shapes joined by lines or arrows | a diagram: connectors stay attached when a shape is moved | shapes with a `connects` slot naming the two shapes |
| a line of writing underlined | the note's title, and a heading in a note's list of contents | a paragraph attribute |

A note may hold several structures. Where the outline and checklist
stationery today take a whole note, they must become items that can sit
inside a plain note: the main change to the Notepad this page needs.

## Live details

Recognised in text and in ink readings (`ink-and-text.md`), shown by a
dotted underline, acted on by a tap:

| Detail | Offer |
|---|---|
| a date or time | a meeting or a to-do, the note linked to it |
| a telephone number | call, send a text, or add to a Names card |
| an address | add to a card; a map where the host has one |
| an e-mail address or URL | mail to it; open it |
| a name that matches a Names card | a link to the card; the card lists the notes that mention it |
| a sentence the ROM's Assistant can match ("call Bob Friday") | that task's slip, filled in |

The patterns are the ROM's own where it has them (the Assistant's lexicon
and Names heuristics, the date and number parsing of the international
frame); nothing here needs a language model or a network.

Links are stored once, in a soup `Links`: the two entries' ids and the
range of text. Both ends query it, so "notes that mention this person"
needs no change to Names beyond a view of the results.

## Tests

Pen demos with a paced pen (strokes given at a writer's speed, so the
recognisers' timing is real): each structure
written, offered and accepted; written and ignored (the ink untouched);
accepted and scrubbed (the ink back). Ordinary paragraphs from the existing
recognition tests give no offers at all - the false-positive check, and the
one that matters most. Live details: each kind in text and in ink.

## Order

Lists and checkboxes; live details; headings; sums; tables; diagrams.

## Owner decisions

1. Offers by margin marker (above), or accepted at once and undone by a
   scrub.
2. Whether right-to-left writing is in the first version (lists and
   checkboxes mirrored).
