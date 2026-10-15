/*
	File:		qd/ArabicText.cpp

	Contains:	Arabic text drawn (ArabicText.h): the joining, the
				reordering, the fallback face.

	DEVIATION: all of it.
*/

#include "ArabicText.h"
#include "HostFonts.h"		// HostOpenFont, MakeHostFontFamily
#include "FixedMath.h"
#include <string.h>
#include <stdlib.h>

const long kNoFont = 3;		// (Fonts.cpp's answer from OpenFont, as HostFontEngine.cpp has it)

/*------------------------------------------------------------------------------
	T h e   s c r i p t
------------------------------------------------------------------------------*/

Boolean
IsArabicChar(UniChar ch)
{
	return (ch >= 0x0600 && ch <= 0x06FF) || (ch >= 0xFB50 && ch <= 0xFDFF) || (ch >= 0xFE70 && ch <= 0xFEFF);
}


Boolean
ArabicInText(const UniChar* chars, long count)
{
	for (long i = 0; i < count; i++)
		if (IsArabicChar(chars[i]))
			return true;
	return false;
}


// a mark that sits on a letter (the harakat, tanwin, shadda, sukun, the
// superscript alif, maddah and the hamza marks)
static Boolean
IsMark(UniChar ch)
{
	return (ch >= 0x064B && ch <= 0x0652) || (ch >= 0x0653 && ch <= 0x0655) || ch == 0x0670
		|| (ch >= 0x06D6 && ch <= 0x06DC) || (ch >= 0x06DF && ch <= 0x06E4) || ch == 0x06E7 || ch == 0x06E8
		|| (ch >= 0x06EA && ch <= 0x06ED)
		|| (ch >= 0xFE70 && ch <= 0xFE7F);		// (the marks' presentation forms)
}


/*------------------------------------------------------------------------------
	T h e   j o i n i n g

	Each letter's four forms in Presentation Forms-B, in Unicode's order
	there: isolated, final, initial, medial (0 for a form the letter has
	not: a letter that never joins the next has no initial or medial).
------------------------------------------------------------------------------*/

struct ArabicForms
{
	UniChar		letter;
	UniChar		isolated, final_, initial, medial;
};

static const ArabicForms kForms[] =
{
	{ 0x0621, 0xFE80, 0,      0,      0      },		// hamza
	{ 0x0622, 0xFE81, 0xFE82, 0,      0      },		// alif madda
	{ 0x0623, 0xFE83, 0xFE84, 0,      0      },		// alif hamza above
	{ 0x0624, 0xFE85, 0xFE86, 0,      0      },		// waw hamza
	{ 0x0625, 0xFE87, 0xFE88, 0,      0      },		// alif hamza below
	{ 0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C },		// ya hamza
	{ 0x0627, 0xFE8D, 0xFE8E, 0,      0      },		// alif
	{ 0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92 },		// ba
	{ 0x0629, 0xFE93, 0xFE94, 0,      0      },		// ta marbuta
	{ 0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98 },		// ta
	{ 0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C },		// tha
	{ 0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0 },		// jim
	{ 0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4 },		// ha
	{ 0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8 },		// kha
	{ 0x062F, 0xFEA9, 0xFEAA, 0,      0      },		// dal
	{ 0x0630, 0xFEAB, 0xFEAC, 0,      0      },		// thal
	{ 0x0631, 0xFEAD, 0xFEAE, 0,      0      },		// ra
	{ 0x0632, 0xFEAF, 0xFEB0, 0,      0      },		// zain
	{ 0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4 },		// sin
	{ 0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8 },		// shin
	{ 0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC },		// sad
	{ 0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0 },		// dad
	{ 0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4 },		// tah
	{ 0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8 },		// zah
	{ 0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC },		// ain
	{ 0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0 },		// ghain
	{ 0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4 },		// fa
	{ 0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8 },		// qaf
	{ 0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC },		// kaf
	{ 0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0 },		// lam
	{ 0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4 },		// mim
	{ 0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8 },		// nun
	{ 0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC },		// ha
	{ 0x0648, 0xFEED, 0xFEEE, 0,      0      },		// waw
	{ 0x0649, 0xFEEF, 0xFEF0, 0,      0      },		// alif maqsura
	{ 0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4 },		// ya
};


static const ArabicForms*
Forms(UniChar ch)
{
	for (unsigned i = 0; i < sizeof(kForms) / sizeof(kForms[0]); i++)
		if (kForms[i].letter == ch)
			return &kForms[i];
	return nil;
}


// the letter before or after `i`, the marks between passed over; nil for none
static const ArabicForms*
Neighbour(const UniChar* chars, long count, long i, long step)
{
	for (long j = i + step; j >= 0 && j < count; j += step)
	{
		if (IsMark(chars[j]))
			continue;
		return Forms(chars[j]);
	}
	return nil;
}


void
ArabicShape(const UniChar* chars, long count, UniChar* out)
{
	for (long i = 0; i < count; i++)
	{
		UniChar ch = chars[i];
		const ArabicForms* f = Forms(ch);
		if (f == nil)
		{
			out[i] = ch;
			continue;
		}
		const ArabicForms* before = Neighbour(chars, count, i, -1);
		const ArabicForms* after = Neighbour(chars, count, i, 1);
		Boolean joinedBefore = before != nil && before->initial != 0;		// (the one before joins the next)
		Boolean joinedAfter = after != nil && f->initial != 0;				// (this one joins the next, and there is one)
		UniChar form;
		if (joinedAfter)
			form = joinedBefore ? f->medial : f->initial;
		else
			form = joinedBefore ? f->final_ : f->isolated;
		if (form == 0)
			form = joinedBefore && f->final_ != 0 ? f->final_ : f->isolated;
		out[i] = form;
	}
}


/*------------------------------------------------------------------------------
	T h e   o r d e r
------------------------------------------------------------------------------*/

// a character that belongs to a run when it has Arabic on both sides
static Boolean
IsNeutral(UniChar ch)
{
	return ch == ' ' || ch == ',' || ch == '.' || ch == ':' || ch == ';' || ch == '!' || ch == '?'
		|| ch == '(' || ch == ')' || ch == '"' || ch == '\'' || ch == '-' || ch == 0x060C || ch == 0x061B || ch == 0x061F;
}


static void
ReverseRun(UniChar* chars, Fixed* advances, long from, long to)
{
	// the clusters: a letter with the marks on it is one
	long n = to - from;
	UniChar* c = (UniChar*) malloc(n * sizeof(UniChar));
	Fixed* a = advances != nil ? (Fixed*) malloc(n * sizeof(Fixed)) : nil;
	long at = 0;
	long i = to;
	while (i > from)
	{
		// the cluster ending at i - 1: back over its marks to its base
		long end = i;
		long start = end - 1;
		while (start > from && IsMark(chars[start]))
			start--;
		for (long j = start; j < end; j++, at++)
		{
			c[at] = chars[j];
			if (a != nil)
				a[at] = advances[j];
		}
		i = start;
	}
	memcpy(chars + from, c, n * sizeof(UniChar));
	if (a != nil)
		memcpy(advances + from, a, n * sizeof(Fixed));
	free(c);
	free(a);
}


void
ArabicReorder(UniChar* chars, Fixed* advances, long count)
{
	long i = 0;
	while (i < count)
	{
		if (!IsArabicChar(chars[i]))
		{
			i++;
			continue;
		}
		// the run: Arabic, and neutrals with Arabic after them
		long end = i;
		while (end < count)
		{
			if (IsArabicChar(chars[end]))
			{
				end++;
				continue;
			}
			if (!IsNeutral(chars[end]))
				break;
			long k = end;
			while (k < count && IsNeutral(chars[k]))
				k++;
			if (k < count && IsArabicChar(chars[k]))
				end = k;
			else
				break;
		}
		ReverseRun(chars, advances, i, end);
		i = end;
	}
}


/*------------------------------------------------------------------------------
	T h e   f a c e

	The host face with Arabic in it, one per pixel size seen (a few), each
	a FontEngineInfo of its own opened through HostOpenFont.
------------------------------------------------------------------------------*/

struct ArabicFace
{
	long			pixels;
	FontEngineInfo*	info;		// nil: none could be opened at this size
};

static ArabicFace	gFaces[8];
static long			gFaceCount = 0;
static Boolean		gNoFace = false;		// the host has no face with Arabic in it


static FontEngineInfo*
OpenArabicFace(long pixels)
{
	static const char* const kFamilies[] = { "Arial", "Segoe UI", "Tahoma", "Noto Sans Arabic", "DejaVu Sans" };
	for (unsigned k = 0; k < sizeof(kFamilies) / sizeof(kFamilies[0]); k++)
	{
		UniChar name[32];
		long n = 0;
		for (const char* p = kFamilies[k]; *p != 0 && n < 31; p++)
			name[n++] = (UniChar) *p;
		name[n] = 0;
		RefVar family(MakeHostFontFamily(name));
		if (ISNIL(family))
			continue;
		FontEngineInfo* info = new FontEngineInfo;
		memset(info, 0, sizeof(FontEngineInfo));
		info->fFontData = new RefStruct;
		info->fCached = new FontEngineInfo;
		memset(info->fCached, 0, sizeof(FontEngineInfo));
		info->fCached->fFontData = new RefStruct;
		StyleRecord style;				// (as HostFontEngine.cpp's Fallback makes one: no memset, its
		style.fFontFamily = family;		//  family slot is a reference)
		style.fFontSize = ToFixed(pixels);
		style.fFontFace = 0;
		if (HostOpenFont(nil, &style, family, ToFixed(1), ToFixed(1), info) == kNoFont)
		{
			delete info->fCached->fFontData;
			delete info->fCached;
			delete info->fFontData;
			delete info;
			continue;
		}
		// does it have Arabic?  (ha's initial form, a glyph every Arabic face has)
		info->fGetGlyph(0xFEEB, 0, info);
		if (info->fGlyphWidth > 0)
			return info;
		if (info->fClose != nil)
			info->fClose(info);
	}
	return nil;
}


FontEngineInfo*
ArabicGlyphSource(FontEngineInfo* info, UniChar ch)
{
	if (!IsArabicChar(ch) || gNoFace || info->fHostFace != nil)
		return info;
	long pixels = info->fAscent + info->fDescent;
	if (info->fScaleY != 0 && info->fScaleY != 0x10000)
		pixels = (FixedMultiply(ToFixed(pixels), info->fScaleY) + 0x8000) >> 16;
	if (pixels < 6)
		pixels = 6;
	for (long i = 0; i < gFaceCount; i++)
		if (gFaces[i].pixels == pixels)
			return gFaces[i].info != nil ? gFaces[i].info : info;
	FontEngineInfo* face = OpenArabicFace(pixels);
	if (face == nil && gFaceCount == 0)
		gNoFace = true;		// (the first size failing means the host has no such face at all)
	if (gFaceCount < (long) (sizeof(gFaces) / sizeof(gFaces[0])))
	{
		gFaces[gFaceCount].pixels = pixels;
		gFaces[gFaceCount].info = face;
		gFaceCount++;
	}
	return face != nil ? face : info;
}


void
ArabicCopyGlyph(FontEngineInfo* to, const FontEngineInfo* from)
{
	if (to == from)
		return;
	to->fGlyphHeight = from->fGlyphHeight;
	to->fGlyphWidth = from->fGlyphWidth;
	to->fGlyphBearingX = from->fGlyphBearingX;
	to->fGlyphBearingY = from->fGlyphBearingY;
	to->fGlyphAdvance = from->fGlyphAdvance;
	to->fGlyphBits = from->fGlyphBits;
	to->fGlyphRowBytes = from->fGlyphRowBytes;
}
