/*
	File:		recognition/InkMirror.h

	Contains:	The switch that hands ParaGraph's reader the writing
				mirrored left to right, for scripts written right to left.

				ParaGraph's cursive reader assumes writing runs left to
				right: written right to left, most Arabic words match no
				path through its word graph at all (`xrw_algs`, -9), while
				the same words mirrored come through every stage
				(docs/recognition/arabic.md, finding 1).  With the switch on,
				every trace made of strokes (`NewGetTraceFromStrokes`, which
				the reader, the ink grouping, the base line finder and the
				learning all take their points from) has each x turned
				about a fixed axis, and the one place where a word's box in
				the trace goes back to the page (`TStrXrDomain`'s word units,
				StrXrDomain.cpp) turns it back.

				The axis is the screen's, not the writing's own, so that the
				traces made of a growing list of strokes - the word segmenter
				is fed one stroke at a time - all agree.  It is the screen's
				width in the trace's units (eighths of a pixel), so x in
				[0, width] goes to [width, 0]: the mirrored writing keeps the
				range the reader expects.  (A fixed axis far to the right
				does not do: the word segmenter's histogram ends at 0x1f60,
				about 1000 pixels - WordSegment.cpp's kWSLineEnd - and a
				line past it overran an array, f8d8_lineY.)

				The switch is NEWTON_INK_MIRROR=1 in the environment, read
				once (the Windows CRT's getenv locks); SetInkMirrored sets it
				from code.

	DEVIATION: all of it.  The ROM has no such switch.
*/

#ifndef __INKMIRROR_H
#define __INKMIRROR_H

#include <stdlib.h>
#include <string.h>

extern long	screenWidth;			// qd/Screen.h: ROM 0x0c104c58 screenWidth

// the axis x is turned about, in the trace's units (eighths of a pixel):
// the screen's width (the MP2x00's 320 before the screen is set up)
inline long
InkMirrorAxis(void)
{
	return (screenWidth > 0 ? screenWidth : 320) * 8;
}

inline int&
InkMirroredState(void)
{
	static int sMirrored = -1;		// -1: not yet read from the environment
	return sMirrored;
}

// whether the reader is handed the writing mirrored
inline bool
InkMirrored(void)
{
	int& state = InkMirroredState();
	if (state < 0)
	{
		const char* value = getenv("NEWTON_INK_MIRROR");
		state = value != NULL && strcmp(value, "1") == 0;
	}
	return state != 0;
}

inline void
SetInkMirrored(bool mirrored)
{
	InkMirroredState() = mirrored ? 1 : 0;
}

// an x in the trace's units, turned about the axis (its own inverse)
inline short
MirrorInkX(long x)
{
	return (short) (InkMirrorAxis() - x);
}

#endif
