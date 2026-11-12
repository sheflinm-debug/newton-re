/*
	File:		MarginMarks.h

	Contains:	DEVIATION (host; docs/modern/marking.md, docs/modern/
				structure.md): not in the ROM.  What a page of paragraphs
				(a TEditView: the Notepad's note) shows in the margin to
				the left of a paragraph, and what a tap there does.

				- a star: the paragraph is marked (a `marked` slot in its
				  data frame; host/HostMarks.ns).  A tap takes the mark off.
				- a checkbox: the paragraph is a to-do item (a `check` slot,
				  'open or 'done).  A tap ticks it, or unticks it.
				- a dotted checkbox: an offer.  The paragraph's text begins
				  with a box written as text - "[ ]", "[]", "[x]", or one of
				  the characters U+2610-2612 - and it is not an item yet.  A
				  tap accepts it: the box written is taken out of the text
				  and the paragraph becomes an item (ticked, for "[x]").
				  Writing on, or leaving it, changes nothing: the offer is
				  only drawn, never stored.

				Stars are shown with marking on, checkboxes with structure
				on (host/Features.h).  The slots go wherever the note goes;
				a MessagePad ignores them.  Saving the note after a change
				is the NewtonScript side's (HostMarks.ns: SaveNoteOf).
*/

#ifndef __MARGINMARKS_H
#define __MARGINMARKS_H

#include "View.h"

// the stars and checkboxes of the editor's paragraphs drawn (its PostDraw)
void	DrawMargin(TView* editor);

// a tap on the page: true when it was on a star or a checkbox, and was
// done here (the caret is then not moved)
Boolean	TapMargin(TView* editor, Point& pt);

// How many characters at the start of the text are a checkbox written as
// text (with the one space after it), 0 for none; whether it is ticked.
long	CheckboxWrittenLength(RefArg text, Boolean* ticked);

#endif	/* __MARGINMARKS_H */
