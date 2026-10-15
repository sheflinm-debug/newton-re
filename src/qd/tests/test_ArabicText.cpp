/*
	test_ArabicText: the joining and the reordering of Arabic text
	(qd/ArabicText.h), without a font.

	- "باب": ba initial, alif final, ba isolated (alif never joins the next).
	- "كتاب": kaf initial, ta medial, alif final, ba isolated.
	- "محمّد": mim initial, ha medial, mim medial (the shadda passed over),
	  the shadda itself, dal final.
	- reordering "باب حصان" (two words, a space) reverses the whole run with
	  its advances; "ab باب cd" reverses only the Arabic; "بٌ" keeps the
	  mark after its letter.
*/

#include "ArabicText.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
Shape(const UniChar* text, long n, const UniChar* expected)
{
	UniChar out[32];
	ArabicShape(text, n, out);
	for (long i = 0; i < n; i++)
	{
		if (out[i] != expected[i])
			fprintf(stderr, "  at %ld: %04x, wanted %04x\n", i, out[i], expected[i]);
		EXPECT(out[i] == expected[i]);
	}
}


int
main(void)
{
	{
		static const UniChar text[] = { 0x0628, 0x0627, 0x0628 };
		static const UniChar want[] = { 0xFE91, 0xFE8E, 0xFE8F };
		Shape(text, 3, want);
	}
	{
		static const UniChar text[] = { 0x0643, 0x062A, 0x0627, 0x0628 };
		static const UniChar want[] = { 0xFEDB, 0xFE98, 0xFE8E, 0xFE8F };
		Shape(text, 4, want);
	}
	{
		static const UniChar text[] = { 0x0645, 0x062D, 0x0645, 0x0651, 0x062F };
		static const UniChar want[] = { 0xFEE3, 0xFEA4, 0xFEE4, 0x0651, 0xFEAA };
		Shape(text, 5, want);
	}
	{
		// not Arabic: as it is
		static const UniChar text[] = { 'a', 'b', ' ' };
		Shape(text, 3, text);
	}
	{
		UniChar text[] = { 0x0628, 0x0627, 0x0628, ' ', 0x062D, 0x0635, 0x0627, 0x0646 };
		Fixed adv[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		ArabicReorder(text, adv, 8);
		static const UniChar want[] = { 0x0646, 0x0627, 0x0635, 0x062D, ' ', 0x0628, 0x0627, 0x0628 };
		for (long i = 0; i < 8; i++)
		{
			EXPECT(text[i] == want[i]);
			EXPECT(adv[i] == 8 - i);
		}
	}
	{
		UniChar text[] = { 'a', 'b', ' ', 0x0628, 0x0627, 0x0628, ' ', 'c', 'd' };
		ArabicReorder(text, nil, 9);
		static const UniChar want[] = { 'a', 'b', ' ', 0x0628, 0x0627, 0x0628, ' ', 'c', 'd' };
		for (long i = 0; i < 9; i++)
			EXPECT(text[i] == want[i]);		// (a palindrome: the same either way, the Latin untouched)
		UniChar text2[] = { 'a', ' ', 0x0643, 0x062A, 0x0627, 0x0628, ' ', 'b' };
		ArabicReorder(text2, nil, 8);
		static const UniChar want2[] = { 'a', ' ', 0x0628, 0x0627, 0x062A, 0x0643, ' ', 'b' };
		for (long i = 0; i < 8; i++)
			EXPECT(text2[i] == want2[i]);
	}
	{
		// a mark stays after its letter: "بٌد" reversed is dal, then ba with its mark
		UniChar text[] = { 0x0628, 0x064C, 0x062F };
		ArabicReorder(text, nil, 3);
		EXPECT(text[0] == 0x062F && text[1] == 0x0628 && text[2] == 0x064C);
	}
	EXPECT(IsArabicChar(0x0628) && IsArabicChar(0xFE91) && !IsArabicChar('b'));
	fprintf(stderr, failures == 0 ? "test_ArabicText: ok\n" : "test_ArabicText: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
