/*
	File:		InkReading.cpp

	Contains:	A reading kept inside an ink word (InkReading.h).
				DEVIATION (host): none of this is in the ROM.
*/

#include "Ink.h"
#include "InkReading.h"
#include "Frames.h"			// MakeString
#include "RSSymbols.h"
#include "RichString.h"		// IsInkWord
#include "ByteOrder.h"

// what ends a reading, just before the eight bytes: its length in
// characters and a mark
static const long kReadingTrailerSize = 6;
static const unsigned int kReadingMark = 0x49526431;	// 'IRd1'
static const long kMaxReadingLength = 255;				// a word, not a page


// Where the reading's characters start and how many there are; false
// when the word has none.
static bool
FindReading(RefArg ink, long* outOffset, long* outLength)
{
	if (ISNIL(ink) || !IsBinary(ink) || !IsInkWord(ink))
		return false;
	long size = Length(ink);
	long infoAt = size - (long) sizeof(PackedInkWordInfo);
	if (infoAt < kReadingTrailerSize)
		return false;
	const char* data = (const char*) BinaryData(ink);
	if (GetBigEndianWord(data + infoAt - 4) != kReadingMark)
		return false;
	long length = GetBigEndianHalf(data + infoAt - kReadingTrailerSize);
	long offset = infoAt - kReadingTrailerSize - length * 2;
	if (length > kMaxReadingLength || offset < 0)
		return false;
	*outOffset = offset;
	*outLength = length;
	return true;
}


long
InkWordStrokesSize(RefArg ink)
{
	if (ISNIL(ink) || !IsBinary(ink) || !IsInkWord(ink))
		return 0;
	long offset, length;
	if (FindReading(ink, &offset, &length))
		return offset;
	return Length(ink) - (long) sizeof(PackedInkWordInfo);
}


Ref
InkWordReading(RefArg ink)
{
	long offset, length;
	if (!FindReading(ink, &offset, &length))
		return NILREF;
	UniChar text[kMaxReadingLength + 1];
	const char* data = (const char*) BinaryData(ink) + offset;
	for (long i = 0; i < length; i++)
		text[i] = GetBigEndianHalf(data + i * 2);
	return MakeString(text, length);
}


Ref
InkWordWithReading(RefArg ink, RefArg reading)
{
	if (ISNIL(ink) || !IsBinary(ink) || !IsInkWord(ink))
		return NILREF;

	long length = 0;
	if (NOTNIL(reading))
	{
		if (!IsString(reading))
			return NILREF;
		length = Length(reading) / (long) sizeof(UniChar) - 1;		// less its terminator
		if (length < 0)
			length = 0;
		if (length > kMaxReadingLength)
			length = kMaxReadingLength;
	}

	long strokesSize = InkWordStrokesSize(ink);
	long oldSize = Length(ink);
	long readingSize = length > 0 ? length * 2 + kReadingTrailerSize : 0;
	long newSize = strokesSize + readingSize + (long) sizeof(PackedInkWordInfo);
	if (length == 0 && newSize == oldSize)
		return ink;							// none to take out

	RefVar result(AllocateBinary(RSSYMinkword, newSize));
	// (nothing below allocates: the pointers hold)
	const char* from = (const char*) BinaryData(ink);
	char* to = (char*) BinaryData(result);
	BlockMove(from, to, strokesSize);
	char* at = to + strokesSize;
	if (length > 0)
	{
		const UniChar* text = (const UniChar*) BinaryData(reading);
		for (long i = 0; i < length; i++, at += 2)
			PutBigEndianHalf(at, text[i]);
		PutBigEndianHalf(at, (unsigned short) length);
		PutBigEndianWord(at + 2, kReadingMark);
		at += kReadingTrailerSize;
	}
	BlockMove(from + oldSize - (long) sizeof(PackedInkWordInfo), at, sizeof(PackedInkWordInfo));
	return result;
}
