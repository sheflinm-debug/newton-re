/*
	File:		LiveDetails.cpp

	Contains:	DEVIATION (host): live details in a paragraph's text
				(LiveDetails.h).  Nothing here is in the ROM.
*/

#include "LiveDetails.h"
#include "ParagraphView.h"
#include "ViewFlags.h"
#include "Draw.h"
#include "Frames.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "host/Features.h"

static Boolean
IsSpace(UniChar c)
{
	return c == ' ' || c == '\t' || c == 0x0D || c == 0x0A || c == 0;
}

static Boolean
IsDigit(UniChar c)
{
	return c >= '0' && c <= '9';
}

static Boolean
IsWordChar(UniChar c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsDigit(c)
		|| c == '.' || c == '-' || c == '_' || c == '+';
}

static Boolean
StartsWith(const UniChar* text, long length, long at, const char* prefix)
{
	for (long i = 0; prefix[i] != 0; i++)
	{
		if (at + i >= length)
			return false;
		UniChar c = text[at + i];
		if (c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';
		if (c != (UniChar) prefix[i])
			return false;
	}
	return true;
}

// a run's end without the full stops, commas and brackets a sentence puts after it
static long
TrimEnd(const UniChar* text, long start, long end)
{
	while (end > start)
	{
		UniChar c = text[end - 1];
		if (c == '.' || c == ',' || c == ';' || c == ':' || c == ')' || c == '!' || c == '?' || c == '"' || c == '\'')
			end--;
		else
			break;
	}
	return end;
}


DetailKind
FindDetail(const UniChar* text, long length, long from, long* start, long* count)
{
	for (long i = from; i < length; i++)
	{
		Boolean wordStart = i == 0 || IsSpace(text[i - 1]) || text[i - 1] == '(' || text[i - 1] == '<';
		if (!wordStart)
			continue;
		// a web address
		if (StartsWith(text, length, i, "http://") || StartsWith(text, length, i, "https://") || StartsWith(text, length, i, "www."))
		{
			long end = i;
			while (end < length && !IsSpace(text[end]))
				end++;
			end = TrimEnd(text, i, end);
			if (end - i > 4)
			{
				*start = i;
				*count = end - i;
				return kDetailURL;
			}
		}
		// an e-mail address: a word with an @, and a dot after it
		{
			long end = i;
			long at = -1;
			while (end < length && (IsWordChar(text[end]) || text[end] == '@'))
			{
				if (text[end] == '@')
					at = at < 0 ? end : -2;
				end++;
			}
			end = TrimEnd(text, i, end);
			if (at > i && at < end - 3)
			{
				Boolean dot = false;
				for (long k = at + 2; k < end - 1; k++)
					if (text[k] == '.')
						dot = true;
				if (dot)
				{
					*start = i;
					*count = end - i;
					return kDetailEmail;
				}
			}
		}
		// a telephone number
		if (IsDigit(text[i]) || text[i] == '+' || text[i] == '(')
		{
			long end = i;
			long digits = 0;
			long lastDigit = i;
			while (end < length)
			{
				UniChar c = text[end];
				if (IsDigit(c))
				{
					digits++;
					lastDigit = end;
				}
				else if (!(c == ' ' || c == '-' || c == '(' || c == ')' || c == '.' || (c == '+' && end == i)))
					break;
				// (two spaces, or a space and then not a digit or a bracket, end it)
				if (c == ' ' && end + 1 < length && !IsDigit(text[end + 1]) && text[end + 1] != '(')
					break;
				end++;
			}
			end = lastDigit + 1;
			// (a number the next character joins to a word is no number: "1234567th")
			Boolean alone = end >= length || IsSpace(text[end]) || text[end] == ',' || text[end] == '.' || text[end] == ')' || text[end] == ';';
			if (digits >= 7 && digits <= 15 && alone)
			{
				*start = i;
				*count = end - i;
				return kDetailPhone;
			}
			if (end > i)
				i = end - 1;
		}
	}
	return kDetailNone;
}


static Boolean
DetailsShown(TParagraphView* view)
{
	return HostFeature(kFeatureStructure) && (view->fFlags & vReadOnly) == 0;
}


void
DrawDetails(TParagraphView* view)
{
	if (!DetailsShown(view))
		return;
	RefVar textRef(view->Text());
	if (!IsString(textRef))
		return;
	long length = (Length(textRef) - 2) / (long) sizeof(UniChar);
	long from = 0;
	long start, count;
	// (the text read again after each piece is drawn: drawing allocates nothing,
	//  but OffsetToCaret may make the line caches)
	for (int guard = 0; guard < 32; guard++)
	{
		DetailKind kind;
		{
			LockRef(textRef);
			kind = FindDetail((const UniChar*) BinaryData(textRef), length, from, &start, &count);
			UnlockRef(textRef);
		}
		if (kind == kDetailNone)
			break;
		from = start + count;
		// a dotted line under it, a line of the paragraph at a time
		long at = start;
		while (at < start + count)
		{
			Rect a, b;
			view->OffsetToCaret(at, &a);
			if (a.top == -32768)
				break;
			long end = at;
			Rect c = a;
			// the last character on the same line as `at`
			while (end + 1 <= start + count)
			{
				view->OffsetToCaret(end + 1, &b);
				if (b.top != a.top || b.top == -32768)
					break;
				c = b;
				end++;
			}
			short y = (short) (a.bottom - 1);
			for (long x = a.left; x < c.left; x += 2)
			{
				Rect dot;
				SetRect(&dot, (short) x, y, (short) (x + 1), (short) (y + 1));
				PaintRect(&dot);
			}
			at = end > at ? end : at + 1;
			if (end == at && end < start + count)
				at++;
		}
	}
}


Boolean
TapDetail(TParagraphView* view, Point& pt)
{
	if (!DetailsShown(view))
		return false;
	long offset = view->PointToOffset(pt, 0, true, nil, nil, nil, nil);
	if (offset < 0)
		return false;
	RefVar textRef(view->Text());
	if (!IsString(textRef))
		return false;
	long length = (Length(textRef) - 2) / (long) sizeof(UniChar);
	long from = 0;
	long start, count;
	DetailKind kind;
	for (;;)
	{
		LockRef(textRef);
		kind = FindDetail((const UniChar*) BinaryData(textRef), length, from, &start, &count);
		UnlockRef(textRef);
		if (kind == kDetailNone || start > offset)
			return false;
		if (offset < start + count)
			break;
		from = start + count;
	}
	RefVar detail(MakeString((const UniChar*) BinaryData(textRef) + start, count));
	static const char* const kKinds[] = { "none", "url", "email", "phone" };
	RefVar marks(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostMarks:host"))));
	if (!IsFrame(marks))
		return false;
	RefVar args(MakeArray(4));
	SetArraySlot(args, 0, RefVar(Intern((char*) kKinds[kind])));
	SetArraySlot(args, 1, detail);
	SetArraySlot(args, 2, RefVar(MAKEINT(pt.h)));
	SetArraySlot(args, 3, RefVar(MAKEINT(pt.v)));
	DoMessage(marks, RefVar(Intern((char*) "DetailTapped")), args);
	return true;
}
