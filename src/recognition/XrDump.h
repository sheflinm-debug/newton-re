/*
	File:		recognition/XrDump.h

	Contains:	A record of the xrs the cursive reader reads, for building
				letter tables from samples (docs/recognition/arabic.md).

				With NEWTON_XR_DUMP=<file> in the environment, every word
				`GCTryToRecognize` reads is appended to the file as one line
				of JSON - the xrs exactly as the matcher is given them
				(after the digit reader has taken its own out), each as
				[type, attrib, penalty, height, shift, orient, link,
				begpoint, endpoint, left, top, right, bottom] - and a script
				can put a line of its own between them
				(`HostXrDumpMark(tag)`, host/newton.cpp) to say which sample
				the words that follow came from:

				  {"mark": "<tag>"}
				  {"points": <the word's trace's points>, "xrs": [[...], ...]}

				tools/arabic-ink/xrs.py reads it.  The environment is read
				once; the file is opened on the first line written and
				flushed after every line.

	DEVIATION: all of it.  The ROM has no such record.
*/

#ifndef __XRDUMP_H
#define __XRDUMP_H

struct xrdata_type;

// whether the record is being kept (NEWTON_XR_DUMP is set)
bool	XrDumping(void);
// a line of the script's own, {"mark": tag}
void	XrDumpMark(const char* tag);
// a word's xrs
void	XrDumpWord(const xrdata_type* xr, long nPoints);
// the word graph the reader made of them: each reading's symbols as
// [code, variant, first xr (from nought), how many xrs, weight] -
// {"graph": [[[...], ...], ...]} - the reader's own layout of the word
void	XrDumpGraph(const struct RWG_type* rwg);

#endif
