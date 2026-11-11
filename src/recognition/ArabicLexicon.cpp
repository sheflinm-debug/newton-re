/*
	File:		recognition/ArabicLexicon.cpp

	Contains:	The Arabic lexicon and its decoder (ArabicLexicon.h).
				Step for step tools/arabic-ink/lexdecode.py: the order in
				which beams are made, merged and ranked is the script's, so
				that the two read alike (ctest recognition.ArabicLexicon).

	DEVIATION: all of it.  The ROM has no such engine.
*/

#include "ArabicLexicon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// lexdecode.py's constants, as its --tune chose them
static const long	kBeam = 48;
static double		gWeight = 0.5;		// x ln frequency
static double		gBonus = 2.0;		// per word
static double		gNoSpace = -2.0;	// a word begun with no space read
static double		gAltCost = -2.5;	// a letter read from its look-alike
static double		gOOVMargin = 4.0;	// the free reading's lead that overrules the lexicon
static const double	kCandidate = -9.0;	// a class below this in a frame is not tried

static const double kNegInf = -HUGE_VAL;

void
ArabicLexiconSetConstants(double weight, double bonus, double noSpace, double altCost, double oovMargin)
{
	gWeight = weight;
	gBonus = bonus;
	gNoSpace = noSpace;
	gAltCost = altCost;
	gOOVMargin = oovMargin;
}
enum { kMaxText = 128 };


/*------------------------------------------------------------------------------
	The lexicon: the words sorted, a prefix a range of them, the best score
	in a range by a sparse table.
------------------------------------------------------------------------------*/

struct ArabicLexicon
{
	long		count;
	UniChar*	pool;			// the words' characters, one after another
	long*		start;			// count + 1: word i is pool[start[i] .. start[i + 1])
	float*		score;			// count
	float**		table;			// levels of the sparse table; table[0] == score
	long		levels;
	double		best;
};

struct Row { UniChar* text; long len; float score; };

static int
CompareText(const UniChar* a, long na, const UniChar* b, long nb)
{
	long n = na < nb ? na : nb;
	for (long i = 0; i < n; i++)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return na < nb ? -1 : na > nb ? 1 : 0;
}

static int
CompareRows(const void* x, const void* y)
{
	const Row* a = (const Row*) x;
	const Row* b = (const Row*) y;
	return CompareText(a->text, a->len, b->text, b->len);
}

// UTF-8 to UTF-16 (the BMP is all the lexicon has); ==> characters, -1 for
// anything else
static long
FromUTF8(const unsigned char* s, long n, UniChar* out, long room)
{
	long k = 0;
	for (long i = 0; i < n; )
	{
		unsigned c = s[i];
		unsigned v;
		if (c < 0x80) { v = c; i += 1; }
		else if ((c & 0xE0) == 0xC0 && i + 1 < n) { v = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); i += 2; }
		else if ((c & 0xF0) == 0xE0 && i + 2 < n) { v = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); i += 3; }
		else return -1;
		if (k >= room)
			return -1;
		out[k++] = (UniChar) v;
	}
	return k;
}


ArabicLexicon*
ArabicLexiconLoad(const char* path, const char* varieties)
{
	FILE* f = fopen(path, "rb");
	if (f == nil)
	{
		fprintf(stderr, "[arabic] cannot open the lexicon %s\n", path);
		return nil;
	}
	long cap = 1 << 16, n = 0;
	Row* rows = (Row*) malloc(cap * sizeof(Row));
	char line[1024];
	UniChar buf[kMaxText];
	while (fgets(line, sizeof(line), f) != nil)
	{
		if (line[0] == '#')
			continue;
		char* tab1 = strchr(line, '\t');
		if (tab1 == nil)
			continue;
		char* tab2 = strchr(tab1 + 1, '\t');
		if (tab2 == nil)
			continue;
		char* end = tab2 + 1 + strcspn(tab2 + 1, "\r\n");
		*end = 0;
		if (varieties != nil && *varieties != 0)
		{
			Boolean any = false;
			for (const char* v = tab2 + 1; *v && !any; v++)
				any = strchr(varieties, *v) != nil;
			if (!any)
				continue;
		}
		long len = FromUTF8((const unsigned char*) line, tab1 - line, buf, kMaxText);
		if (len <= 0)
			continue;
		if (n == cap)
		{
			cap *= 2;
			rows = (Row*) realloc(rows, cap * sizeof(Row));
		}
		rows[n].text = (UniChar*) malloc(len * sizeof(UniChar));
		memcpy(rows[n].text, buf, len * sizeof(UniChar));
		rows[n].len = len;
		rows[n].score = (float) strtod(tab1 + 1, nil);
		n++;
	}
	fclose(f);
	qsort(rows, n, sizeof(Row), CompareRows);

	ArabicLexicon* lex = (ArabicLexicon*) calloc(1, sizeof(ArabicLexicon));
	lex->count = n;
	long chars = 0;
	for (long i = 0; i < n; i++)
		chars += rows[i].len;
	lex->pool = (UniChar*) malloc((chars > 0 ? chars : 1) * sizeof(UniChar));
	lex->start = (long*) malloc((n + 1) * sizeof(long));
	lex->score = (float*) malloc((n > 0 ? n : 1) * sizeof(float));
	long at = 0;
	lex->best = 0;
	for (long i = 0; i < n; i++)
	{
		lex->start[i] = at;
		memcpy(lex->pool + at, rows[i].text, rows[i].len * sizeof(UniChar));
		at += rows[i].len;
		lex->score[i] = rows[i].score;
		if (i == 0 || rows[i].score > lex->best)
			lex->best = rows[i].score;
		free(rows[i].text);
	}
	lex->start[n] = at;
	free(rows);

	long levels = 1;
	while ((1L << levels) <= n)
		levels++;
	lex->levels = levels;
	lex->table = (float**) calloc(levels, sizeof(float*));
	lex->table[0] = lex->score;
	for (long k = 1, half = 1; k < levels; k++, half *= 2)
	{
		long m = n - 2 * half + 1;
		const float* prev = lex->table[k - 1];
		float* t = (float*) malloc((m > 0 ? m : 1) * sizeof(float));
		for (long i = 0; i < m; i++)
			t[i] = prev[i] > prev[i + half] ? prev[i] : prev[i + half];
		lex->table[k] = t;
	}
	fprintf(stderr, "[arabic] the lexicon: %ld words%s%s\n", n,
			varieties != nil && *varieties ? " of the varieties " : "", varieties != nil ? varieties : "");
	return lex;
}


void
ArabicLexiconFree(ArabicLexicon* lex)
{
	if (lex == nil)
		return;
	for (long k = 1; k < lex->levels; k++)
		free(lex->table[k]);
	free(lex->table);
	free(lex->score);
	free(lex->start);
	free(lex->pool);
	free(lex);
}


long
ArabicLexiconCount(const ArabicLexicon* lex)
{
	return lex != nil ? lex->count : 0;
}


static inline const UniChar*	Word(const ArabicLexicon* lex, long i)		{ return lex->pool + lex->start[i]; }
static inline long				WordLen(const ArabicLexicon* lex, long i)	{ return lex->start[i + 1] - lex->start[i]; }

static double
RangeMax(const ArabicLexicon* lex, long lo, long hi)
{
	long k = 0;
	while ((2L << k) <= hi - lo)
		k++;
	const float* t = lex->table[k];
	float a = t[lo], b = t[hi - (1L << k)];
	return a > b ? a : b;
}

// the words in [lo, hi) that start with prefix (which all of them do but
// for its last character): lexdecode.py's Lexicon.child
static void
Child(const ArabicLexicon* lex, long lo, long hi, const UniChar* prefix, long n, long* outLo, long* outHi)
{
	// first word >= prefix
	long a = lo, b = hi;
	while (a < b)
	{
		long m = (a + b) / 2;
		if (CompareText(Word(lex, m), WordLen(lex, m), prefix, n) < 0)
			a = m + 1;
		else
			b = m;
	}
	long first = a;
	// first word, from there, that does not start with prefix
	b = hi;
	while (a < b)
	{
		long m = (a + b) / 2;
		long len = WordLen(lex, m);
		if (len >= n && memcmp(Word(lex, m), prefix, n * sizeof(UniChar)) == 0)
			a = m + 1;
		else
			b = m;
	}
	*outLo = first;
	*outHi = a;
}

// the word's own score if prefix is a word, else NAN
static double
WordScore(const ArabicLexicon* lex, long lo, long hi, const UniChar* prefix, long n)
{
	if (n > 0 && lo < hi && WordLen(lex, lo) == n && memcmp(Word(lex, lo), prefix, n * sizeof(UniChar)) == 0)
		return lex->score[lo];
	return NAN;
}


/*------------------------------------------------------------------------------
	The frames: the vowel marks' probability merged into the blank's.
------------------------------------------------------------------------------*/

static Boolean
IsMark(UniChar c)
{
	return (c >= 0x064B && c <= 0x0652) || c == 0x0670 || c == 0x0653 || c == 0x0654 || c == 0x0655;
}

static inline double
LogSumExp(double a, double b)
{
	if (a == kNegInf)
		return b;
	if (b == kNegInf)
		return a;
	double m = a > b ? a : b;
	return m + log(exp(a - m) + exp(b - m));
}

struct Frames
{
	long		steps, classes;
	double*		lp;			// steps x classes, log-softmax
	double*		blank;		// steps
	long		space;		// the space's class, or -1
	const UniChar* alphabet;
};

static long
ClassOf(const UniChar* alphabet, long classes, UniChar ch)
{
	for (long j = 1; j < classes; j++)
		if (alphabet[j] == ch)
			return j;
	return -1;
}

// the look-alikes a letter may be read from: lexdecode.py's ALTERNATES
static const UniChar*
Alternates(UniChar ch)
{
	static const UniChar alif[] = { 0x0627, 0 };			// ا
	static const UniChar alifHamzas[] = { 0x0623, 0x0625, 0 };	// أ إ
	static const UniChar ha[] = { 0x0647, 0 };
	static const UniChar taMarbuta[] = { 0x0629, 0 };
	static const UniChar ya[] = { 0x064A, 0 };
	static const UniChar alifMaqsura[] = { 0x0649, 0 };
	static const UniChar waw[] = { 0x0648, 0 };
	static const UniChar yaAlifMaqsura[] = { 0x064A, 0x0649, 0 };
	switch (ch)
	{
	case 0x0623: case 0x0625: case 0x0622:	return alif;
	case 0x0627:	return alifHamzas;
	case 0x0629:	return ha;
	case 0x0647:	return taMarbuta;
	case 0x0649:	return ya;
	case 0x064A:	return alifMaqsura;
	case 0x0624:	return waw;
	case 0x0626:	return yaAlifMaqsura;
	}
	return nil;
}

// ch's log-probability at t, or its look-alike's at a cost
static double
Emission(const Frames* fr, long cls, UniChar ch, long t)
{
	double e = cls >= 0 ? fr->lp[t * fr->classes + cls] : kNegInf;
	const UniChar* alt = Alternates(ch);
	for (; alt != nil && *alt; alt++)
	{
		long c = ClassOf(fr->alphabet, fr->classes, *alt);
		if (c >= 0 && !IsMark(*alt))
		{
			double v = fr->lp[t * fr->classes + c] + gAltCost;
			if (v > e)
				e = v;
		}
	}
	return e;
}


/*------------------------------------------------------------------------------
	The beams.
------------------------------------------------------------------------------*/

struct Beam
{
	UniChar		text[kMaxText];
	long		len;
	long		lo, hi, start;
	double		pb, pnb, lm;
	UniChar		last;		// 0: none
	Boolean		hasLast;
};

struct BeamSet
{
	Beam*		beams;
	long		count, cap;
	long*		hash;		// open addressing: index + 1, 0 empty
	long		hashSize;
};

static unsigned long
HashText(const UniChar* s, long n)
{
	unsigned long h = 2166136261u;
	for (long i = 0; i < n; i++)
		h = (h ^ s[i]) * 16777619u;
	return h;
}

static void
SetInit(BeamSet* s, long cap)
{
	s->cap = cap;
	s->count = 0;
	s->beams = (Beam*) malloc(cap * sizeof(Beam));
	s->hashSize = 1;
	while (s->hashSize < cap * 2)
		s->hashSize *= 2;
	s->hash = (long*) calloc(s->hashSize, sizeof(long));
}

static void
SetClear(BeamSet* s)
{
	s->count = 0;
	memset(s->hash, 0, s->hashSize * sizeof(long));
}

static void
SetFree(BeamSet* s)
{
	free(s->beams);
	free(s->hash);
}

// lexdecode.py's put: a new text appended; a known one has its two
// probabilities summed in, the rest of it kept
static void
Put(BeamSet* s, const UniChar* text, long len, long lo, long hi, long start, double pb, double pnb, double lm, UniChar last, Boolean hasLast)
{
	if (len > kMaxText)
		return;
	unsigned long h = HashText(text, len) & (s->hashSize - 1);
	while (s->hash[h] != 0)
	{
		Beam* b = &s->beams[s->hash[h] - 1];
		if (b->len == len && memcmp(b->text, text, len * sizeof(UniChar)) == 0)
		{
			b->pb = LogSumExp(b->pb, pb);
			b->pnb = LogSumExp(b->pnb, pnb);
			return;
		}
		h = (h + 1) & (s->hashSize - 1);
	}
	if (s->count == s->cap)
	{
		// grow: rehash
		long cap = s->cap * 2;
		BeamSet t;
		SetInit(&t, cap);
		memcpy(t.beams, s->beams, s->count * sizeof(Beam));
		t.count = s->count;
		for (long i = 0; i < t.count; i++)
		{
			unsigned long g = HashText(t.beams[i].text, t.beams[i].len) & (t.hashSize - 1);
			while (t.hash[g] != 0)
				g = (g + 1) & (t.hashSize - 1);
			t.hash[g] = i + 1;
		}
		SetFree(s);
		*s = t;
		h = HashText(text, len) & (s->hashSize - 1);
		while (s->hash[h] != 0)
			h = (h + 1) & (s->hashSize - 1);
	}
	Beam* b = &s->beams[s->count];
	memcpy(b->text, text, len * sizeof(UniChar));
	b->len = len;
	b->lo = lo;
	b->hi = hi;
	b->start = start;
	b->pb = pb;
	b->pnb = pnb;
	b->lm = lm;
	b->last = last;
	b->hasLast = hasLast;
	s->hash[h] = ++s->count;
}

struct Ranked { double total; long index; };

static int
CompareRanked(const void* x, const void* y)
{
	const Ranked* a = (const Ranked*) x;
	const Ranked* b = (const Ranked*) y;
	if (a->total != b->total)
		return a->total > b->total ? -1 : 1;
	return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;	// stable, as Python's sort
}

static double
Ahead(const ArabicLexicon* lex, const Beam* b)
{
	if (b->len - b->start <= 0)
		return lex->best;
	return RangeMax(lex, b->lo, b->hi);
}


struct Found { UniChar text[kMaxText]; long len; double score; };

// lexdecode.py's decode: the lexicon readings, best first, at most `room`
static long
Decode(const ArabicLexicon* lex, const Frames* fr, Found* out, long room)
{
	long T = fr->steps;
	long C = fr->classes;
	const UniChar* alphabet = fr->alphabet;
	// the candidate letters: the alphabet's, in its order, but the blank, the marks and the space
	long* cands = (long*) malloc(C * sizeof(long));
	long nCands = 0;
	for (long j = 1; j < C; j++)
		if (alphabet[j] != 0 && !IsMark(alphabet[j]) && alphabet[j] != ' ')
			cands[nCands++] = j;
	double* frame = (double*) malloc(C * sizeof(double));	// by class
	long* tries = (long*) malloc(C * sizeof(long));

	BeamSet cur, nxt;
	SetInit(&cur, kBeam * 4);
	SetInit(&nxt, kBeam * 8);
	Put(&cur, nil, 0, 0, lex->count, 0, 0.0, kNegInf, 0.0, 0, false);
	Ranked* ranked = (Ranked*) malloc(sizeof(Ranked) * 64);
	long rankedCap = 64;
	UniChar w[kMaxText + 2];

	for (long t = 0; t < T; t++)
	{
		long nTries = 0;
		for (long k = 0; k < nCands; k++)
		{
			long j = cands[k];
			frame[j] = Emission(fr, j, alphabet[j], t);
			if (frame[j] > kCandidate)
				tries[nTries++] = j;
		}
		double sp = fr->space >= 0 ? fr->lp[t * C + fr->space] : kNegInf;
		double blank = fr->blank[t];
		SetClear(&nxt);
		for (long i = 0; i < cur.count; i++)
		{
			Beam b = cur.beams[i];		// a copy: nxt may grow
			double p = LogSumExp(b.pb, b.pnb);
			Put(&nxt, b.text, b.len, b.lo, b.hi, b.start, p + blank, kNegInf, b.lm, b.last, b.hasLast);
			if (b.hasLast && b.last != ' ')
			{
				long j = ClassOf(alphabet, C, b.last);
				double e = j >= 0 ? frame[j] : kNegInf;
				Put(&nxt, b.text, b.len, b.lo, b.hi, b.start, kNegInf, b.pnb + e, b.lm, b.last, b.hasLast);
			}
			else if (b.hasLast && b.last == ' ')
				Put(&nxt, b.text, b.len, b.lo, b.hi, b.start, kNegInf, b.pnb + sp, b.lm, b.last, b.hasLast);
			long wn = b.len - b.start;
			memcpy(w, b.text + b.start, wn * sizeof(UniChar));
			double here = wn > 0 ? WordScore(lex, b.lo, b.hi, w, wn) : NAN;
			for (long k = 0; k < nTries; k++)
			{
				long j = tries[k];
				UniChar ch = alphabet[j];
				double e = frame[j];
				double prev = (b.hasLast && ch == b.last) ? b.pb : p;
				if (b.len + 1 > kMaxText)
					continue;
				w[wn] = ch;
				long lo, hi;
				Child(lex, b.lo, b.hi, w, wn + 1, &lo, &hi);
				if (lo < hi)
				{
					UniChar text[kMaxText];
					memcpy(text, b.text, b.len * sizeof(UniChar));
					text[b.len] = ch;
					Put(&nxt, text, b.len + 1, lo, hi, b.start, kNegInf, prev + e, b.lm, ch, true);
				}
				if (!isnan(here) && b.len + 2 <= kMaxText)
				{
					Child(lex, 0, lex->count, &ch, 1, &lo, &hi);
					if (lo < hi)
					{
						UniChar text[kMaxText];
						memcpy(text, b.text, b.len * sizeof(UniChar));
						text[b.len] = ' ';
						text[b.len + 1] = ch;
						double lm = b.lm + gWeight * here + gBonus + gNoSpace;
						Put(&nxt, text, b.len + 2, lo, hi, b.len + 1, kNegInf, prev + e, lm, ch, true);
					}
				}
			}
			if (!isnan(here) && sp > kCandidate && b.len + 1 <= kMaxText)
			{
				UniChar text[kMaxText];
				memcpy(text, b.text, b.len * sizeof(UniChar));
				text[b.len] = ' ';
				double lm = b.lm + gWeight * here + gBonus;
				Put(&nxt, text, b.len + 1, 0, lex->count, b.len + 1, kNegInf, p + sp, lm, ' ', true);
			}
		}
		// keep the best kBeam
		if (nxt.count > rankedCap)
		{
			rankedCap = nxt.count * 2;
			ranked = (Ranked*) realloc(ranked, rankedCap * sizeof(Ranked));
		}
		for (long i = 0; i < nxt.count; i++)
		{
			const Beam* b = &nxt.beams[i];
			ranked[i].total = LogSumExp(b->pb, b->pnb) + b->lm + gWeight * Ahead(lex, b);
			ranked[i].index = i;
		}
		qsort(ranked, nxt.count, sizeof(Ranked), CompareRanked);
		long keep = nxt.count < kBeam ? nxt.count : kBeam;
		SetClear(&cur);
		for (long i = 0; i < keep; i++)
		{
			const Beam* b = &nxt.beams[ranked[i].index];
			Put(&cur, b->text, b->len, b->lo, b->hi, b->start, b->pb, b->pnb, b->lm, b->last, b->hasLast);
		}
	}

	// the finished readings
	long nDone = 0;
	Found* done = (Found*) malloc((cur.count > 0 ? cur.count : 1) * sizeof(Found));
	Ranked* order = (Ranked*) malloc((cur.count > 0 ? cur.count : 1) * sizeof(Ranked));
	for (long i = 0; i < cur.count; i++)
	{
		const Beam* b = &cur.beams[i];
		long len = b->len;
		double final;
		if (len > 0 && b->text[len - 1] == ' ')
		{
			final = LogSumExp(b->pb, b->pnb) + b->lm;
			while (len > 0 && b->text[len - 1] == ' ')
				len--;
		}
		else
		{
			double here = WordScore(lex, b->lo, b->hi, b->text + b->start, b->len - b->start);
			if (isnan(here))
				continue;
			final = LogSumExp(b->pb, b->pnb) + b->lm + gWeight * here + gBonus;
		}
		memcpy(done[nDone].text, b->text, len * sizeof(UniChar));
		done[nDone].len = len;
		done[nDone].score = final;
		order[nDone].total = final;
		order[nDone].index = nDone;
		nDone++;
	}
	qsort(order, nDone, sizeof(Ranked), CompareRanked);
	long n = 0;
	for (long i = 0; i < nDone && n < room; i++)
	{
		const Found* d = &done[order[i].index];
		Boolean seen = false;
		for (long k = 0; k < n && !seen; k++)
			seen = out[k].len == d->len && memcmp(out[k].text, d->text, d->len * sizeof(UniChar)) == 0;
		if (!seen)
			out[n++] = *d;
	}
	free(done);
	free(order);
	free(ranked);
	SetFree(&cur);
	SetFree(&nxt);
	free(tries);
	free(frame);
	free(cands);
	return n;
}


// lexdecode.py's ctc_score: the CTC forward log-probability of text
static double
CTCScore(const Frames* fr, const UniChar* text, long n)
{
	long T = fr->steps;
	long C = fr->classes;
	if (n == 0)
	{
		double s = 0;
		for (long t = 0; t < T; t++)
			s += fr->blank[t];
		return s;
	}
	long S = 2 * n + 1;
	long* cls = (long*) malloc(S * sizeof(long));		// -1 the blank, -2 a character with no class
	UniChar* ext = (UniChar*) malloc(S * sizeof(UniChar));
	for (long s = 0; s < S; s++)
	{
		if (s % 2 == 0)
		{
			cls[s] = -1;
			ext[s] = 0;
		}
		else
		{
			ext[s] = text[s / 2];
			long j = ClassOf(fr->alphabet, C, ext[s]);
			cls[s] = j >= 0 && !IsMark(ext[s]) ? j : -2;
		}
	}
	double* alpha = (double*) malloc(S * sizeof(double));
	double* next = (double*) malloc(S * sizeof(double));
	#define COL(s, t) (cls[s] == -1 ? fr->blank[t] : cls[s] == -2 ? -1e9 : fr->lp[(t) * C + cls[s]])
	for (long s = 0; s < S; s++)
		alpha[s] = kNegInf;
	alpha[0] = COL(0, 0);
	alpha[1] = COL(1, 0);
	for (long t = 1; t < T; t++)
	{
		for (long s = 0; s < S; s++)
		{
			double a = alpha[s];
			if (s > 0)
				a = LogSumExp(a, alpha[s - 1]);
			if (s > 1 && ext[s] != 0 && ext[s] != ext[s - 2])
				a = LogSumExp(a, alpha[s - 2]);
			next[s] = a + COL(s, t);
		}
		double* swap = alpha;
		alpha = next;
		next = swap;
	}
	#undef COL
	double r = LogSumExp(alpha[S - 1], alpha[S - 2]);
	free(alpha);
	free(next);
	free(ext);
	free(cls);
	return r;
}


static long
AddReading(ArabicReading* out, long n, long room, const UniChar* text, long len, double score, Boolean fromLexicon)
{
	if (n >= room || len >= kMaxText)
		return n;
	for (long k = 0; k < n; k++)
	{
		long m = 0;
		while (out[k].text[m] != 0)
			m++;
		if (m == len && memcmp(out[k].text, text, len * sizeof(UniChar)) == 0)
			return n;
	}
	memcpy(out[n].text, text, len * sizeof(UniChar));
	out[n].text[len] = 0;
	out[n].score = score;
	out[n].fromLexicon = fromLexicon;
	return n + 1;
}


long
ArabicLexiconRead(const ArabicLexicon* lex, const float* logits, long steps,
				  const UniChar* alphabet, long classes, ArabicReading* out, long room)
{
	if (room < 1 || steps < 1)
		return 0;
	Frames fr;
	fr.steps = steps;
	fr.classes = classes;
	fr.alphabet = alphabet;
	fr.space = ClassOf(alphabet, classes, ' ');
	fr.lp = (double*) malloc(steps * classes * sizeof(double));
	fr.blank = (double*) malloc(steps * sizeof(double));
	for (long t = 0; t < steps; t++)
	{
		const float* row = logits + t * classes;
		double z = kNegInf;
		for (long j = 0; j < classes; j++)
			z = LogSumExp(z, row[j]);
		for (long j = 0; j < classes; j++)
			fr.lp[t * classes + j] = row[j] - z;
		double b = fr.lp[t * classes];
		for (long j = 1; j < classes; j++)
			if (IsMark(alphabet[j]))
				b = LogSumExp(b, fr.lp[t * classes + j]);
		fr.blank[t] = b;
	}

	// the free reading: greedy, the marks dropped, the ends' spaces trimmed
	UniChar free_[kMaxText];
	long freeLen = 0;
	long prev = 0;
	for (long t = 0; t < steps; t++)
	{
		const double* row = fr.lp + t * classes;
		long best = 0;
		for (long j = 1; j < classes; j++)
			if (row[j] > row[best])
				best = j;
		if (best != prev && best != 0 && !IsMark(alphabet[best]) && freeLen < kMaxText)
			free_[freeLen++] = alphabet[best];
		prev = best;
	}
	long freeStart = 0;
	while (freeStart < freeLen && free_[freeStart] == ' ')
		freeStart++;
	while (freeLen > freeStart && free_[freeLen - 1] == ' ')
		freeLen--;
	const UniChar* freeText = free_ + freeStart;
	freeLen -= freeStart;

	long n = 0;
	Found* found = (Found*) malloc(room * sizeof(Found));
	long nFound = lex != nil && lex->count > 0 ? Decode(lex, &fr, found, room) : 0;

	// the reading taken
	Boolean takeFree = true;
	if (nFound > 0)
	{
		if (found[0].len == freeLen && memcmp(found[0].text, freeText, freeLen * sizeof(UniChar)) == 0)
			takeFree = false;
		else
		{
			double fs = CTCScore(&fr, freeText, freeLen);
			double ls = CTCScore(&fr, found[0].text, found[0].len);
			takeFree = fs - ls > gOOVMargin;
		}
	}
	// the reading taken, then the lexicon's others, then the free one: each once
	if (takeFree)
		n = AddReading(out, n, room, freeText, freeLen, 0, false);
	for (long i = 0; i < nFound; i++)
		n = AddReading(out, n, room, found[i].text, found[i].len, found[i].score, true);
	n = AddReading(out, n, room, freeText, freeLen, 0, false);
	free(found);
	free(fr.blank);
	free(fr.lp);
	return n;
}
