/*
	File:		recognition/ArabicNet.h

	Contains:	The host's Arabic word reader: a small network over the
				pen's trajectory, run in the host's own code.

				Trained by tools/arabic-ink/train_net.py on ADAB and the
				user's samples (a two-layer bidirectional GRU with CTC) and
				written by tools/arabic-ink/export_weights.py as one
				binary file, the .anet, whose format that script's header
				gives.  Here: the file read, the strokes of a word made into
				the features the training made (the word scaled to its
				height, mirrored so that it runs left to right in reading
				order, resampled to an even step), the network run, and the
				logits decoded greedily into the word's characters.  No
				library: the arithmetic is written out, so that the engine
				runs the same on every host (docs/recognition/arabic.md,
				finding 5).

	DEVIATION: all of it.  The ROM has no such engine.
*/

#ifndef __ARABICNET_H
#define __ARABICNET_H

#include "Newton.h"

struct ArabicNet;

// The network read from an .anet file; nil when it cannot be (the reason
// on stderr).  Freed with ArabicNetFree.
ArabicNet*	ArabicNetLoad(const char* path);
void		ArabicNetFree(ArabicNet* net);

long		ArabicNetFeatureCount(const ArabicNet* net);	// features a step has
long		ArabicNetClassCount(const ArabicNet* net);		// classes (0 the blank)
UniChar		ArabicNetClassChar(const ArabicNet* net, long cls);	// a class's character (0 for the blank)

// A word's strokes - each `counts[s]` points of (x, y) in `xy`, one stroke
// after another, in screen coordinates (y down) - made into features, one
// step after another, `features` per step: the array is the caller's, of
// room for `room` steps.  ==> how many steps (0 when there is nothing to
// read; more than `room` when the room was too small and nothing was
// written).  ArabicNetStepsFor says how many steps at most the strokes
// can make.
long		ArabicNetStepsFor(const float* xy, const long* counts, long nStrokes, float step);
long		ArabicNetFeaturize(const ArabicNet* net, const float* xy, const long* counts, long nStrokes, float* out, long room);

// The network run over `steps` steps of features: `logits` (the caller's,
// steps x classes) filled.  ==> 0, or -1 for no memory.
long		ArabicNetRun(const ArabicNet* net, const float* features, long steps, float* logits);

// The logits decoded greedily (CTC: the best class of each step, repeats
// and blanks dropped) into `out`, at most `room` characters, nought-ended.
// ==> how many characters.
long		ArabicNetGreedy(const ArabicNet* net, const float* logits, long steps, UniChar* out, long room);

#endif
