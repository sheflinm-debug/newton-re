/*
	File:		InkReading.h

	Contains:	A reading kept inside an ink word - the text a recogniser
				made of the writing, carried in the same binary as the
				strokes, so that writing left as ink can be found and a
				word can be shown as its ink or as its text without being
				read again (docs/modern/ink-and-text.md).

				DEVIATION (host): not in the ROM.  An ink word is its
				compressed strokes followed by eight bytes of information
				(Ink.h).  The reading goes between the two:

					strokes | reading, UniChars, big-endian | count, a halfword | 'IRd1' | the eight bytes

				The ROM finds the information as the last eight bytes
				(GetPackedInkWordInfo) and its decoder stops at the end of
				the strokes' group (DecoderRun), so a MessagePad draws,
				measures and reads such a word as it does any other and
				never looks at the reading - which is why it is here and
				not in a slot beside the word: it goes wherever the word
				goes (cut, pasted, beamed, saved) with nothing to keep in
				step.  Ink re-encoded by the ROM's code (two words joined,
				one split) comes out without it, and is simply read again.
*/

#if !defined(__INKREADING_H)
#define __INKREADING_H 1

#include "objects.h"

// The reading an ink word carries: a string, or nil when it has none (or
// is not an ink word).
Ref		InkWordReading(RefArg ink);

// The same ink word with this reading (a string), or with none (nil): a
// new binary - the size changes - or the one given when there is nothing
// to change.  nil when `ink` is not an ink word.
Ref		InkWordWithReading(RefArg ink, RefArg reading);

// How many bytes of the binary are the strokes: what is left without the
// reading and the eight bytes.
long	InkWordStrokesSize(RefArg ink);

#endif	/* __INKREADING_H */
