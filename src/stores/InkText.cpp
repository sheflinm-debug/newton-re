/*
	File:		InkText.cpp

	Contains:	DEVIATION (host): the readings of ink words written beside
				their paragraph, for Find (InkText.h).  Nothing here is in
				the ROM.
*/

#include "InkText.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ByteOrder.h"

// The reading's place in an 'inkWord binary (ink/InkReading.h, which
// writes it - the stores library does not link the ink one, so the few
// bytes are read here):
//   strokes | reading, UniChars big-endian | count, a halfword | 'IRd1' | 8 bytes of info
static const long			kInfoSize = 8;
static const long			kTrailerSize = 6;
static const unsigned int	kReadingMark = 0x49526431;
static const long			kMaxReadingLength = 255;
static const long			kMaxInkText = 4000;			// characters of readings kept for one paragraph
static const int			kMaxDepth = 4;


// a pointer to an object (not a magic pointer, which is the ROM's)
static inline bool
IsRealPtr(Ref r)
{
	return RTAG(r) == kTagPointer;
}


static Ref
InkTextSymbol(void)
{
	static RefStruct* sym = nil;
	if (sym == nil)
		sym = new RefStruct(Intern((char*) "inkText"));
	return *sym;
}


// The reading's characters in an ink word: where, how many; false for none.
static bool
ReadingIn(Ref ink, const char** chars, long* count)
{
	if (!IsRealPtr(ink) || !IsBinary(ink) || !IsInstance(ink, RSSYMinkword))
		return false;
	long size = Length(ink);
	long infoAt = size - kInfoSize;
	if (infoAt < kTrailerSize)
		return false;
	const char* data = (const char*) BinaryData(ink);
	if (GetBigEndianWord(data + infoAt - 4) != kReadingMark)
		return false;
	long length = GetBigEndianHalf(data + infoAt - kTrailerSize);
	long offset = infoAt - kTrailerSize - length * 2;
	if (length <= 0 || length > kMaxReadingLength || offset < 0)
		return false;
	*chars = data + offset;
	*count = length;
	return true;
}


Ref
HostInkReadingsOf(RefArg styles)
{
	if (!IsArray(styles))
		return NILREF;
	UniChar* text = nil;
	long used = 0;
	long runs = Length(styles);
	for (long i = 1; i < runs; i += 2)
	{
		RefVar style(GetArraySlotRef(styles, i));
		const char* chars;
		long count;
		if (!ReadingIn(style, &chars, &count))
			continue;
		if (text == nil)
		{
			text = new UniChar[kMaxInkText + 1];
			if (text == nil)
				return NILREF;
		}
		if (used + count + 1 > kMaxInkText)
			break;
		if (used > 0)
			text[used++] = ' ';
		for (long j = 0; j < count; j++)
			text[used++] = GetBigEndianHalf(chars + j * 2);
	}
	if (text == nil)
		return NILREF;
	RefVar result(used > 0 ? MakeString(text, used) : NILREF);
	delete[] text;
	return result;
}


static void
IndexIn(RefArg obj, int depth)
{
	if (depth > kMaxDepth || !IsRealPtr(obj) || (ObjectFlags(obj) & kObjReadOnly) != 0)
		return;
	if (IsFrame(obj))
	{
		RefVar text(GetFrameSlotRef(obj, RSSYMtext));
		RefVar styles(GetFrameSlotRef(obj, RSSYMstyles));
		if (IsString(text) && IsArray(styles))
		{
			RefVar readings(HostInkReadingsOf(styles));
			RefVar slot(InkTextSymbol());
			if (NOTNIL(readings))
				SetFrameSlot(obj, slot, readings);
			else if (FrameHasSlotRef(obj, slot))
				RemoveSlot(obj, slot);
			return;								// a paragraph holds no paragraphs
		}
		long slots = Length(obj);
		for (long i = 0; i < slots; i++)
		{
			RefVar value(GetArraySlotRef(obj, i));
			if (IsRealPtr(value) && !IsBinary(value) && !IsSymbol(value))
				IndexIn(value, depth + 1);
		}
	}
	else if (IsArray(obj))
	{
		long count = Length(obj);
		for (long i = 0; i < count; i++)
		{
			RefVar value(GetArraySlotRef(obj, i));
			if (IsRealPtr(value) && !IsBinary(value) && !IsSymbol(value))
				IndexIn(value, depth + 1);
		}
	}
}


void
HostIndexInkReadings(RefArg obj)
{
	IndexIn(obj, 0);
}
