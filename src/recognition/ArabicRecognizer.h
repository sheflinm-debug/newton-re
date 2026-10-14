/*
	File:		recognition/ArabicRecognizer.h

	Contains:	`TArabicRecognizer` ('ARAB'), the host's Arabic word
				engine: the strokes of a word read by the host's network
				(ArabicNet.h) and the word put on the unit as the Newton's
				own recognisers put theirs.

				A host engine (WordEngines.h), chosen with the "Arabic"
				button of the Handwriting Recognition slip.  Strokes are
				grouped into words as the ink-only engine groups them - a
				stroke joins the word when the pen came down again within
				the domain's delay, else it starts the next - and a closed
				word's strokes go to the network, which reads them right to
				left (the mirror is its own: nothing else is told), and the
				reading is the unit's one interpretation, a word, so the
				paragraph takes it as it takes Rosetta's.  The network is
				the .anet file NEWTON_ARABIC_NET names
				(tools/arabic-ink/export_weights.py), read once; without one
				the engine reads nothing and the strokes become ink.

				NOT YET: alternatives for the corrector (one reading only);
				a vocabulary (the greedy reading alone); the marks and the
				letters the training had no samples of; a word gap judged
				by the writing rather than the clock.

	DEVIATION: all of it.  The ROM has no such engine.
*/

#ifndef __ARABICRECOGNIZER_H
#define __ARABICRECOGNIZER_H

#include "WRecDomain.h"

const ULong kArabicType = 'ARAB';

// Registers the engine under kHostWordEngineInterface.  Does nothing
// when there is no protocol registry.
void	RegisterArabicRecognizer(void);

PROTOCOL TArabicRecognizer : public TWRecognizer
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TArabicRecognizer);

	TArabicRecognizer*	New(void);
	void				Delete(void);

	void	Initialize(void);
	void	Group(TStrokeUnit* stroke);
	long	Classify(TWRecUnit* unit);
	long	Reclassify(TWRecUnit* unit);
	long	FindBaseline(TStroke** strokes, Point* out);
	void	GroupInkStroke(TStrokeUnit* stroke, ULong a, ULong b, Boolean flag);
	long	AreaInfoGetSize(void);
	void	AreaInfoFillDefaults(Handle info);
	void	AreaInfoConfigure(Handle info, RefArg config);
	void	AreaInfoFreeDependents(Handle info);
	void	AreaInfoSetParameters(Handle info);
	void	UnitInfoFreePtr(char* info);
	Boolean	VerifyWordSymbols(UniChar* word);
	long	UnitConfidence(TWRecUnit* unit);
	void	Sleep(void);
	void	WakeUp(void);

private:
	void	Read(TWRecUnit* unit);
	long	fConfidence;	// the last reading's, 0..100
};

#endif	/* __ARABICRECOGNIZER_H */
