/*
	test_ArabicLexicon: the engine's lexicon decoder (recognition/
	ArabicLexicon.h) held to tools/arabic-ink/lexdecode.py.

	Wants the composite lexicon (tools/arabic-ink/build_lexicon.py) and a
	check lexdecode.py --fixture wrote with it: for each case, the
	network's logits and the readings Python made of them (the one taken,
	the alternatives).  The C++ must make the same readings in the same
	order, with the constants the check names.

	  test_ArabicLexicon <lexicon> <check>
*/

#include "ArabicLexicon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static unsigned long
U32(FILE* f)
{
	unsigned char b[4];
	if (fread(b, 1, 4, f) != 4)
		return 0;
	return (unsigned long) b[0] | ((unsigned long) b[1] << 8) | ((unsigned long) b[2] << 16) | ((unsigned long) b[3] << 24);
}

static UniChar
U16(FILE* f)
{
	unsigned char b[2];
	if (fread(b, 1, 2, f) != 2)
		return 0;
	return (UniChar) (b[0] | (b[1] << 8));
}

static double
F64(FILE* f)
{
	double v = 0;		// the hosts are little-endian, as the file
	if (fread(&v, 8, 1, f) != 1)
		return 0;
	return v;
}

static void
Print(const char* label, const UniChar* s, long n)
{
	fprintf(stderr, "%s", label);
	for (long i = 0; i < n; i++)
		fprintf(stderr, "%s%04x", i ? " " : "", (unsigned) s[i]);
	fprintf(stderr, "\n");
}

static long
Len(const UniChar* s)
{
	long n = 0;
	while (s[n] != 0)
		n++;
	return n;
}

int
main(int argc, char** argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: test_ArabicLexicon <lexicon> <check>\n");
		return 2;
	}
	ArabicLexicon* lex = ArabicLexiconLoad(argv[1], nil);
	if (lex == nil)
		return 1;
	FILE* f = fopen(argv[2], "rb");
	if (f == nil)
	{
		fprintf(stderr, "cannot open %s\n", argv[2]);
		return 1;
	}
	char magic[4];
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ALXF", 4) != 0 || U32(f) != 1)
	{
		fprintf(stderr, "%s is not a lexicon check\n", argv[2]);
		return 1;
	}
	long classes = (long) U32(f);
	UniChar* alphabet = (UniChar*) malloc(classes * sizeof(UniChar));
	for (long j = 0; j < classes; j++)
		alphabet[j] = U16(f);
	double k[5];
	for (int i = 0; i < 5; i++)
		k[i] = F64(f);
	ArabicLexiconSetConstants(k[0], k[1], k[2], k[3], k[4]);
	long cases = (long) U32(f);
	long same = 0;
	for (long c = 0; c < cases; c++)
	{
		long steps = (long) U32(f);
		float* logits = (float*) malloc(steps * classes * sizeof(float));
		if (fread(logits, sizeof(float), steps * classes, f) != (size_t) (steps * classes))
		{
			fprintf(stderr, "the check is short\n");
			return 1;
		}
		long room = (long) U32(f);
		long want = (long) U32(f);
		ArabicReading* got = (ArabicReading*) malloc(room * sizeof(ArabicReading));
		long n = ArabicLexiconRead(lex, logits, steps, alphabet, classes, got, room);
		Boolean ok = n == want;
		for (long r = 0; r < want; r++)
		{
			long len = (long) U32(f);
			UniChar w[256];
			for (long i = 0; i < len; i++)
				w[i] = U16(f);
			if (r < n && (Len(got[r].text) != len || memcmp(got[r].text, w, len * sizeof(UniChar)) != 0))
			{
				ok = false;
				fprintf(stderr, "case %ld reading %ld differs:\n", c, r);
				Print("  python ", w, len);
				Print("  c++    ", got[r].text, Len(got[r].text));
			}
		}
		if (n != want)
			fprintf(stderr, "case %ld: %ld readings, python %ld\n", c, n, want);
		same += ok;
		if (!ok)
			failures++;
		free(got);
		free(logits);
	}
	fclose(f);
	free(alphabet);
	ArabicLexiconFree(lex);
	printf("%ld of %ld cases read as Python reads them\n", same, cases);
	return failures == 0 ? 0 : 1;
}
