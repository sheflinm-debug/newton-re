/*
	File:		qd/ArabicText.h

	Contains:	Arabic text drawn: the letters joined into their contextual
				forms, a run laid out right to left, and the glyphs found
				where the ROM's fonts have none.

				The ROM's text engine measures and draws one character at a
				time, left to right, from the strike's own glyphs (Text.cpp's
				MeasureGlyphWidths, DrText.cpp's DrTextChunk).  Arabic needs
				three things in front of that, and this file gives them to
				those two places:

				- `ArabicShape`: each letter made the presentation form
				  (Unicode's Arabic Presentation Forms-B, U+FE70..U+FEFF)
				  its place in the word calls for - isolated, final,
				  initial or medial - by the joining rules (a letter joins
				  the next unless it is one of the six that never do, or a
				  hamza seat, ta marbuta or alif maqsura).  The marks ride
				  along as they are.
				- `ArabicReorder`: each run of Arabic reversed for drawing,
				  with its advances, so that the first letter is on the
				  right; a letter and the marks on it move as one, so that a
				  mark is still drawn over its letter.  Spaces and common
				  punctuation between two Arabic words are part of the run.
				- `ArabicGlyphSource`: the engine to take an Arabic
				  character's glyph from - the font's own when it is a host
				  face, else a host face with Arabic in it (Arial, Segoe UI
				  or Tahoma on Windows), opened at the strike's size and
				  kept; the ROM's strikes have no Arabic at all.

				What this is not: a bidirectional layout.  The caret and the
				selection still count in logical order, so inside an Arabic
				word they stand mirrored; a line that mixes scripts is laid
				out as runs, not reordered as Unicode's algorithm would.
				NOT YET: the lam-alif ligature (lam and alif are drawn as
				their joined forms), the caret and selection in visual
				order, Arabic in the ROM's own fonts (a bitmap font), the
				hosts without a font rasteriser.

	DEVIATION: all of it.  The ROM draws every character as the strike has
	it, left to right (docs/recognition/arabic.md, "The screen").
*/

#ifndef __ARABICTEXT_H
#define __ARABICTEXT_H

#include "Newton.h"
#include "Fonts.h"

// whether the character is Arabic script (the letters and marks, the
// presentation forms)
Boolean	IsArabicChar(UniChar ch);
// whether any character of the text is
Boolean	ArabicInText(const UniChar* chars, long count);

// The text's letters in their contextual forms, into `out` (room for
// `count`); other characters as they are.
void	ArabicShape(const UniChar* chars, long count, UniChar* out);

// Each run of Arabic in `chars` reversed in place, and `advances` with it
// (nil for none); a letter with its marks moves as one.
void	ArabicReorder(UniChar* chars, Fixed* advances, long count);

// The engine to take the character's glyph from: `info` itself for any
// character that is not Arabic, or when `info` is a host face; else the
// Arabic host face at the strike's size, or `info` when the host has no
// such face.
FontEngineInfo*	ArabicGlyphSource(FontEngineInfo* info, UniChar ch);

// The glyph fields of `from` copied into `to` (after a GetGlyph on `from`).
void	ArabicCopyGlyph(FontEngineInfo* to, const FontEngineInfo* from);

#endif
