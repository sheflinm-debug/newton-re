/*
	File:		CaretWriting.h

	Contains:	DEVIATION (host, host/KeepInk.h; docs/modern/ink-and-text.md):
				not in the ROM.  With ink kept after recognition, the menu a
				tap on the caret pops up (the punctuation grid and the space
				bar, TRootView::DoCaretClick) gains a strip of two cells under
				the space bar: "Abc" shows writing as text, the scribble shows
				text as the writing it came from.  What is shown the other
				way is the selection when there is one - the selected words
				of every paragraph of the note - and the whole note when
				there is none.
*/

#ifndef __CARETWRITING_H
#define __CARETWRITING_H

#include "View.h"

// The caret view's _caretPopup with the strip added: a frame whose popup
// array is the ROM's and one grid item more, and whose pickActionScript
// hands every other pick on to the ROM's.  The popup itself when ink is
// not kept, or it is not a frame with a popup array.
Ref		CaretPopupWithWriting(RefArg popup);

// The words around the key view shown as writing or as text: the
// selection of the paragraphs it is among (its own, and its siblings' in
// an edit view), or with no selection all of them.
// ==> how many paragraphs were gone through.
long	ShowWritingAround(TView* keyView, Boolean asInk);

#endif	/* __CARETWRITING_H */
