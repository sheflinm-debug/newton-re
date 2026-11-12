/*
	File:		InkText.h

	Contains:	DEVIATION (host; docs/modern/ink-and-text.md "Find"): not
				in the ROM.  Writing kept as ink is found by Find.

				A soup query looks for words and text in the strings of an
				entry (TCursor::WordsValidTest / TextValidTest, through the
				store object's text section and its word hints).  A word
				shown as its writing is one character, 0xF701, in the
				paragraph's text, and its reading is inside the 'inkWord
				binary of its style run (ink/InkReading.h), where no query
				looks.  So, as an entry is written to a store, each
				paragraph frame - a frame with a `text` string and a
				`styles` array - that holds ink words with readings is
				given a string slot `inkText`, the readings one after
				another with a space between; one that no longer holds any
				loses the slot.  The string is written as every string is,
				so the store's format is unchanged and a MessagePad finds
				the note by its handwriting too; a paragraph ignores a slot
				it does not know.
*/

#ifndef __INKTEXT_H
#define __INKTEXT_H

#include "Newton.h"
#include "objects.h"

// The slot set or taken away on every paragraph frame within obj (a few
// levels down: an entry's slots, the arrays in them and the frames in
// those).  Read-only objects are left as they are.
void	HostIndexInkReadings(RefArg obj);

// The readings of the ink words in a styles array, space-separated; nil
// when there are none.
Ref		HostInkReadingsOf(RefArg styles);

#endif	/* __INKTEXT_H */
