# Structure and meaning recognised from what is written

**Status: a proposal.**

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
