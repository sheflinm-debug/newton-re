/*
	File:		recognition/ArabicLexicon.h

	Contains:	The Arabic engine's lexicon and the decoder that reads the
				network's output by it.

				The lexicon is tools/arabic-ink/build_lexicon.py's
				composite of Arabic word lists (Wiktionary's standard and
				dialect entries, the CAMeL Lab and drelhaj/Arabic-Dialects
				word counts): one word a line, "word<TAB>ln
				frequency<TAB>varieties", the varieties letters - m Modern
				Standard, c Classical, e Egyptian, l Levantine, g Gulf,
				h Hijazi, n North African, d dialect unspecified.  Arabic
				is diglossic, so the engine can be told which varieties to
				read by (NEWTON_ARABIC_VARIETIES).

				The decoder is a CTC prefix beam search over the lexicon -
				the same, step for step, as tools/arabic-ink/lexdecode.py,
				whose header explains it and whose --tune chose the
				constants (ctest recognition.ArabicLexicon holds the two
				together): the vowel marks optional, a letter readable
				from its look-alike at a cost, several words with or
				without a space read between them, the words' frequencies
				weighed in; and the free (greedy) reading kept when it
				beats the lexicon's best by a margin, for a name or a word
				the lexicon has not.

	DEVIATION: all of it.  The ROM has no such engine.
*/

#ifndef __ARABICLEXICON_H
#define __ARABICLEXICON_H

#include "Newton.h"

struct ArabicLexicon;

// The lexicon read from its file, keeping the words of any of `varieties`
// (letters as above; nil or "" for all); nil when it cannot be read (the
// reason on stderr).
ArabicLexicon*	ArabicLexiconLoad(const char* path, const char* varieties);
void			ArabicLexiconFree(ArabicLexicon* lex);
long			ArabicLexiconCount(const ArabicLexicon* lex);

// The decoder's constants (lexdecode.py's WEIGHT, BONUS, NO_SPACE,
// ALT_COST, OOV_MARGIN); the tuned ones by default.
void			ArabicLexiconSetConstants(double weight, double bonus, double noSpace, double altCost, double oovMargin);

// One reading of the network's output.
struct ArabicReading
{
	UniChar		text[128];		// nought-ended
	double		score;			// the decoder's: log-probability + weighed frequencies
	Boolean		fromLexicon;	// false: the free reading
};

// The network's output for one word or phrase - `logits`, steps x classes,
// `alphabet` each class's character (0 the blank) - read by the lexicon:
// `out[0]` the reading the engine takes, then up to `room - 1`
// alternatives (the lexicon's next best, and the free reading if it is not
// among them).  ==> how many readings.  `lex` nil: the free reading alone.
long			ArabicLexiconRead(const ArabicLexicon* lex, const float* logits, long steps,
								  const UniChar* alphabet, long classes,
								  ArabicReading* out, long room);

#endif
