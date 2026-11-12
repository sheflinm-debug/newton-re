/*
	KeepInk.h - the switch for ink kept after recognition.

	DEVIATION (host), docs/modern/ink-and-text.md: with it on, a word the
	recogniser turned into text keeps the writing it came from, in the
	style run of its characters, and an ink word carries its reading
	(ink/InkReading.h) - so writing left as ink can be found, and a word
	shown either way.  Off (the default), every path is the ROM's.

	The Host preferences panel's "Keep handwriting" turns it on or off
	(host/HostSettings.h, kept in the System soup); NEWTON_KEEP_INK in the
	environment (1 on, 0 off) overrides the panel.  Header-only, as
	RomBugs.h is, so every library can ask without a link dependency; the
	environment is read once (the Windows CRT's getenv locks).
*/

#ifndef __KEEPINK_H
#define __KEEPINK_H

#include <stdlib.h>

inline int&
KeepInkState(void)
{
	static int sKeep = -1;		// -1: not yet read from the environment
	return sKeep;
}

// ink is kept after recognition (off unless asked for)
inline bool
KeepInk(void)
{
	int& keep = KeepInkState();
	if (keep < 0)
	{
		const char* asked = getenv("NEWTON_KEEP_INK");
		keep = asked != NULL && asked[0] != 0 && asked[0] != '0';
	}
	return keep != 0;
}

// whether NEWTON_KEEP_INK says, one way or the other (the panel then
// cannot change it)
inline bool
KeepInkFromEnvironment(void)
{
	static int sSaid = -1;
	if (sSaid < 0)
	{
		const char* asked = getenv("NEWTON_KEEP_INK");
		sSaid = asked != NULL && asked[0] != 0;
	}
	return sSaid != 0;
}

// tests: on or off, whatever NEWTON_KEEP_INK says
inline void
SetKeepInk(bool keep)
{
	KeepInkState() = keep ? 1 : 0;
}

#endif	/* __KEEPINK_H */
