/*
	File:		MarginMarks.cpp

	Contains:	DEVIATION (host): the margin of a page of paragraphs - stars
				and checkboxes (MarginMarks.h).  Nothing here is in the ROM.
*/

#include "MarginMarks.h"
#include "ParagraphView.h"
#include "ViewFlags.h"
#include "Draw.h"
#include "Frames.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "RichString.h"
#include "host/Features.h"

static Ref
Sym(const char* name)
{
	return Intern((char*) name);
}


long
CheckboxWrittenLength(RefArg text, Boolean* ticked)
{
	*ticked = false;
	if (!IsString(text))
		return 0;
	const UniChar* s = (const UniChar*) BinaryData(text);
	long length = (Length(text) - 2) / (long) sizeof(UniChar);
	long n = 0;
	if (length >= 1 && s[0] >= 0x2610 && s[0] <= 0x2612)
	{
		*ticked = s[0] != 0x2610;
		n = 1;
	}
	else if (length >= 2 && s[0] == '[' && s[1] == ']')
		n = 2;
	else if (length >= 3 && s[0] == '[' && s[2] == ']')
	{
		UniChar c = s[1];
		if (c == ' ')
			n = 3;
		else if (c == 'x' || c == 'X' || c == 'v' || c == 'V' || c == 0x2713 || c == 0x2714)
		{
			*ticked = true;
			n = 3;
		}
	}
	if (n > 0 && n < length && s[n] == ' ')
		n++;
	return n;
}


long
ListWrittenLength(RefArg text, Ref* kind)
{
	*kind = NILREF;
	if (!IsString(text))
		return 0;
	const UniChar* s = (const UniChar*) BinaryData(text);
	long length = (Length(text) - 2) / (long) sizeof(UniChar);
	// a dash, a star or a bullet, and a space: a bulleted item
	if (length >= 3 && (s[0] == '-' || s[0] == '*' || s[0] == 0x2022) && s[1] == ' ' && s[2] != ' ')
	{
		*kind = Sym("bullet");
		return 2;
	}
	// one or two digits, a full stop or a bracket, and a space: a numbered one
	long n = 0;
	while (n < 2 && n < length && s[n] >= '0' && s[n] <= '9')
		n++;
	if (n > 0 && n + 2 < length && (s[n] == '.' || s[n] == ')') && s[n + 1] == ' ' && s[n + 2] != ' ')
	{
		*kind = Sym("number");
		return n + 2;
	}
	return 0;
}


// What the margin beside one paragraph holds, and where.
struct MarginItems
{
	Boolean	fMarked;		// a star
	Ref		fCheck;			// 'open, 'done or nil
	Boolean	fOffer;			// a dotted box: a checkbox written as text
	Boolean	fOfferTicked;
	long	fOfferLength;
	Ref		fList;			// 'bullet, 'number or nil: a list item
	Ref		fListOffer;		// the same, written as text: an offer
	long	fListOfferLength;
	long	fNumber;		// a numbered item's number
	Rect	fBox;			// the checkbox or the list's mark (or the offer)
	Rect	fStar;
};


// A numbered item's number: one more than the numbered items straight
// above it on the page (the page's children are in the order written, so
// they are taken by where they are).
static long
NumberOf(TView* editor, TView* child)
{
	RefVar listSym(Sym("list"));
	long number = 1;
	long above = child->viewBounds.top;
	for (;;)
	{
		// the paragraph nearest above
		TView* nearest = nil;
		long children = editor->fChildren->Count();
		for (long i = 0; i < children; i++)
		{
			TView* other = editor->fChildren->At((short) i);
			if (other == nil || other == child || !other->DerivedFrom(clParagraphView))
				continue;
			if (other->viewBounds.top < above && (nearest == nil || other->viewBounds.top > nearest->viewBounds.top))
				nearest = other;
		}
		if (nearest == nil)
			break;
		RefVar data(nearest->DataFrame());
		if (!IsFrame(data) || !EQRef(GetFrameSlot(data, listSym), Sym("number")))
			break;
		number++;
		above = nearest->viewBounds.top;
		if (number > 99)
			break;
	}
	return number;
}


static Boolean
ItemsOf(TView* editor, TView* child, MarginItems* items)
{
	items->fMarked = false;
	items->fCheck = NILREF;
	items->fOffer = false;
	items->fOfferTicked = false;
	items->fOfferLength = 0;
	items->fList = NILREF;
	items->fListOffer = NILREF;
	items->fListOfferLength = 0;
	items->fNumber = 0;
	if (child == nil || !child->DerivedFrom(clParagraphView))
		return false;
	RefVar data(child->DataFrame());
	if (!IsFrame(data))
		return false;
	if (HostFeature(kFeatureMarking))
		items->fMarked = NOTNIL(GetFrameSlot(data, RefVar(Sym("marked"))));
	if (HostFeature(kFeatureStructure))
	{
		Ref check = GetFrameSlot(data, RefVar(Sym("check")));
		if (EQRef(check, Sym("open")) || EQRef(check, Sym("done")))
			items->fCheck = check;
		else
		{
			Boolean ticked;
			long n = CheckboxWrittenLength(RefVar(((TParagraphView*) child)->Text()), &ticked);
			if (n > 0 && (child->fFlags & (vReadOnly | vWriteProtected)) == 0)
			{
				items->fOffer = true;
				items->fOfferTicked = ticked;
				items->fOfferLength = n;
			}
			else
			{
				// not a to-do item: a list item, or one written as text
				Ref list = GetFrameSlot(data, RefVar(Sym("list")));
				if (EQRef(list, Sym("bullet")) || EQRef(list, Sym("number")))
				{
					items->fList = list;
					if (EQRef(list, Sym("number")))
						items->fNumber = NumberOf(editor, child);
				}
				else if ((child->fFlags & (vReadOnly | vWriteProtected)) == 0)
				{
					Ref kind;
					long length = ListWrittenLength(RefVar(((TParagraphView*) child)->Text()), &kind);
					if (length > 0)
					{
						items->fListOffer = kind;
						items->fListOfferLength = length;
					}
				}
			}
		}
	}
	if (!items->fMarked && ISNIL(items->fCheck) && !items->fOffer && ISNIL(items->fList) && ISNIL(items->fListOffer))
		return false;
	Boolean box = NOTNIL(items->fCheck) || items->fOffer || NOTNIL(items->fList) || NOTNIL(items->fListOffer);
	long boxLeft = child->viewBounds.left - 11;
	long starLeft = box && items->fMarked ? boxLeft - 10 : boxLeft;
	long floor = editor->viewBounds.left + 1;
	if (starLeft < floor)
	{
		boxLeft += floor - starLeft;
		starLeft = floor;
	}
	long top = child->viewBounds.top + 3;
	SetRect(&items->fBox, (short) boxLeft, (short) top, (short) (boxLeft + 8), (short) (top + 8));
	SetRect(&items->fStar, (short) starLeft, (short) top, (short) (starLeft + 7), (short) (top + 7));
	return true;
}


static void
Dot(long x, long y)
{
	Rect r;
	SetRect(&r, (short) x, (short) y, (short) (x + 1), (short) (y + 1));
	PaintRect(&r);
}


void
DrawMargin(TView* editor)
{
	if ((!HostFeature(kFeatureMarking) && !HostFeature(kFeatureStructure)) || editor->fChildren == nil)
		return;
	static const char* const kStar[7] = {
		"...#...",
		"...#...",
		"#######",
		".#####.",
		"..###..",
		".##.##.",
		"#.....#" };
	static const char* const kTick[7] = {		// a tick, bold, over the box's top
		"........#",
		".......##",
		"......##.",
		"#....##..",
		"##..##...",
		".####....",
		"..##....." };
	long children = editor->fChildren->Count();
	for (long i = 0; i < children; i++)
	{
		MarginItems items;
		if (!ItemsOf(editor, editor->fChildren->At((short) i), &items))
			continue;
		if (items.fMarked)
			for (long y = 0; y < 7; y++)
				for (long x = 0; x < 7; x++)
					if (kStar[y][x] == '#')
						Dot(items.fStar.left + x, items.fStar.top + y);
		if (NOTNIL(items.fList) || NOTNIL(items.fListOffer))
		{
			const Rect& b = items.fBox;
			Boolean offer = NOTNIL(items.fListOffer);
			if (EQRef(items.fList, Sym("number")))
			{
				// the number, its digits right against the box's right
				static const char* const kDigits[10][5] = {
					{ "###", "#.#", "#.#", "#.#", "###" }, { ".#.", "##.", ".#.", ".#.", "###" },
					{ "###", "..#", "###", "#..", "###" }, { "###", "..#", ".##", "..#", "###" },
					{ "#.#", "#.#", "###", "..#", "..#" }, { "###", "#..", "###", "..#", "###" },
					{ "###", "#..", "###", "#.#", "###" }, { "###", "..#", ".#.", ".#.", ".#." },
					{ "###", "#.#", "###", "#.#", "###" }, { "###", "#.#", "###", "..#", "###" } };
				long n = items.fNumber;
				long x = b.right - 2;					// the full stop
				Dot(x, b.top + 6);
				do
				{
					long d = n % 10;
					x -= 4;
					for (long y = 0; y < 5; y++)
						for (long c = 0; c < 3; c++)
							if (kDigits[d][y][c] == '#')
								Dot(x + c, b.top + 2 + y);
					n /= 10;
				} while (n > 0);
			}
			else
			{
				// a bullet: a round dot, hollow and dotted for an offer
				static const char* const kBullet[4] = { ".##.", "####", "####", ".##." };
				for (long y = 0; y < 4; y++)
					for (long x = 0; x < 4; x++)
						if (kBullet[y][x] == '#' && (!offer || ((x + y) & 1) == 0))
							Dot(b.left + 2 + x, b.top + 2 + y);
				if (offer && EQRef(items.fListOffer, Sym("number")))
					Dot(b.left + 7, b.top + 6);			// (a numbered one's offer: its full stop too)
			}
		}
		if (NOTNIL(items.fCheck) || items.fOffer)
		{
			const Rect& b = items.fBox;
			for (long x = b.left; x < b.right; x++)
				for (long y = b.top; y < b.bottom; y++)
				{
					Boolean edge = x == b.left || x == b.right - 1 || y == b.top || y == b.bottom - 1;
					// an offer's box dotted, an item's whole
					if (edge && (!items.fOffer || ((x + y) & 1) == 0))
						Dot(x, y);
				}
			if (EQRef(items.fCheck, Sym("done")))
				for (long y = 0; y < 7; y++)
					for (long x = 0; x < 9; x++)
						if (kTick[y][x] == '#')
							Dot(b.left + 1 + x, b.top + y - 2);
		}
	}
}


// The NewtonScript side told (HostMarks.ns, the global HostMarks:host).
static void
Tell(const char* message, RefArg paragraph)
{
	RefVar marks(GetFrameSlot(RefVar(gVarFrame), RefVar(Sym("HostMarks:host"))));
	if (!IsFrame(marks))
		return;
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, paragraph);
	DoMessage(marks, RefVar(Sym(message)), args);
}


Boolean
TapMargin(TView* editor, Point& pt)
{
	if ((!HostFeature(kFeatureMarking) && !HostFeature(kFeatureStructure)) || editor->fChildren == nil)
		return false;
	long children = editor->fChildren->Count();
	for (long i = 0; i < children; i++)
	{
		TView* child = editor->fChildren->At((short) i);
		MarginItems items;
		if (!ItemsOf(editor, child, &items))
			continue;
		Rect box = items.fBox;
		InsetRect(&box, -3, -3);
		if ((NOTNIL(items.fCheck) || items.fOffer) && PtInRect(pt, &box))
		{
			if (items.fOffer)
			{
				// the box written taken out of the text, and the paragraph
				// made an item
				TParagraphView* para = (TParagraphView*) child;
				DoInsertItems(para, RefVar(MakeString("")), false, true, 0, items.fOfferLength, false, RefVar(NILREF));
				SetFrameSlot(RefVar(child->DataFrame()), RefVar(Sym("check")),
							 RefVar(Sym(items.fOfferTicked ? "done" : "open")));
				Tell("SaveNoteOf", child->fContext);
			}
			else
				Tell("ToggleDone", child->fContext);
			editor->Dirty(nil);
			return true;
		}
		if (NOTNIL(items.fListOffer) && PtInRect(pt, &box))
		{
			// the dash or number written taken out, and the paragraph
			// made a list item
			TParagraphView* para = (TParagraphView*) child;
			RefVar kind(items.fListOffer);
			DoInsertItems(para, RefVar(MakeString("")), false, true, 0, items.fListOfferLength, false, RefVar(NILREF));
			SetFrameSlot(RefVar(child->DataFrame()), RefVar(Sym("list")), kind);
			Tell("SaveNoteOf", child->fContext);
			editor->Dirty(nil);
			return true;
		}
		Rect star = items.fStar;
		InsetRect(&star, -3, -3);
		if (items.fMarked && PtInRect(pt, &star))
		{
			RefVar list(MakeArray(1));
			SetArraySlot(list, 0, child->fContext);
			Tell("ToggleMarks", list);
			editor->Dirty(nil);
			return true;
		}
	}
	return false;
}
