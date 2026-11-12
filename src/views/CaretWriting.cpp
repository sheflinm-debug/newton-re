/*
	File:		CaretWriting.cpp

	Contains:	DEVIATION (host, host/KeepInk.h; docs/modern/ink-and-text.md):
				the "show as text / show as writing" strip of the caret's
				pop-up.  See CaretWriting.h.  Nothing here is in the ROM.
*/

#include "CaretWriting.h"
#include "ParagraphView.h"
#include "Hilites.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "Frames.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "host/KeepInk.h"
#include "host/Features.h"
#include <string.h>

/*------------------------------------------------------------------------------
	T h e   s t r i p ' s   p i c t u r e
	43 pixels wide as the grids above it are, two cells of 19: the frame
	is the dither the ROM's pictures are framed with, as the space bar's
	is, and is drawn here; the cells hold these.
------------------------------------------------------------------------------*/

enum { kStripWidth = 43, kStripHeight = 19, kStripRowBytes = 8, kArtRows = 8, kArtTop = 6 };

static const char* const kTextArt[kArtRows] = {		// "Abc"
	".###..#..........",
	"#...#.#..........",
	"#...#.####...###.",
	"#####.#...#.#....",
	"#...#.#...#.#....",
	"#...#.#...#.#....",
	"#...#.####...###.",
	".................",
};

static const char* const kWritingArt[kArtRows] = {	// a line of joined loops
	"....#.....#......",
	"...#.#...#.#.....",
	"...#.#...#.#.....",
	"...#.#...#.#...#.",
	"..#..#..#..#..#..",
	".#...#.#...#.#...",
	"#.....#.....#....",
	".................",
};

static void
SetStripPixel(unsigned char* rows, long x, long y)
{
	rows[y * kStripRowBytes + (x >> 3)] |= (unsigned char) (0x80 >> (x & 7));
}

static const char* const kMarkArt[kArtRows] = {	// a star, and "Mark"
	"....#..........#...#............#...",
	"...###.........##.##............#...",
	"#########......#.#.#..###..#.#..#..#",
	".#######.......#...#.....#.##...#.#.",
	"..#####........#...#..####.#....##..",
	".###.###.......#...#.#...#.#....#.#.",
	".##...##.......#...#..####.#....#..#",
	"#.......#...........................",
};

// A grid item of one row: `columns` cells across the 43 pixels, framed
// with the ROM's dither, the art of cell i drawn from x = lefts[i].
static Ref
MakeArtItem(long columns, const char* const* const* arts, const long* lefts)
{
	long size = 16 + kStripHeight * kStripRowBytes;
	RefVar bits(AllocateBinary(RSSYMbits, size));
	unsigned char* p = (unsigned char*) BinaryData(bits);
	memset(p, 0, size);
	// (a bitmap's header: base address, row bytes, pad, bounds - halfwords, big-endian)
	p[5] = kStripRowBytes;
	p[13] = kStripHeight;
	p[15] = kStripWidth;
	unsigned char* rows = p + 16;
	for (long y = 0; y < kStripHeight; y++)
	{
		Boolean odd = (y & 1) != 0;
		if (y < 2 || y >= kStripHeight - 2)
		{
			for (long x = odd ? 1 : 0; x < kStripWidth; x += 2)
				SetStripPixel(rows, x, y);
		}
		else if (odd)
		{
			SetStripPixel(rows, 1, y);
			if (columns == 2)
				SetStripPixel(rows, 21, y);
			SetStripPixel(rows, kStripWidth - 2, y);
		}
		else
		{
			SetStripPixel(rows, 0, y);
			SetStripPixel(rows, kStripWidth - 1, y);
		}
	}
	for (long c = 0; c < columns; c++)
		for (long y = 0; y < kArtRows; y++)
			for (long x = 0; arts[c][y][x] != 0 && lefts[c] + x < kStripWidth - 2; x++)
				if (arts[c][y][x] == '#')
					SetStripPixel(rows, lefts[c] + x, kArtTop + y);

	RefVar bounds(AllocateFrame());
	SetFrameSlot(bounds, RSSYMleft, RefVar(MAKEINT(0)));
	SetFrameSlot(bounds, RSSYMtop, RefVar(MAKEINT(0)));
	SetFrameSlot(bounds, RSSYMright, RefVar(MAKEINT(kStripWidth)));
	SetFrameSlot(bounds, RSSYMbottom, RefVar(MAKEINT(kStripHeight)));
	RefVar item(AllocateFrame());
	SetFrameSlot(item, RSSYMbits, bits);
	SetFrameSlot(item, RSSYMbounds, bounds);
	SetFrameSlot(item, RSSYMwidth, RefVar(MAKEINT(columns)));
	SetFrameSlot(item, RSSYMheight, RefVar(MAKEINT(1)));
	return item;
}

static Ref
MakeStripItem(void)
{
	static const char* const* const kArts[2] = { kTextArt, kWritingArt };
	static const long kLefts[2] = { 3, 23 };
	return MakeArtItem(2, kArts, kLefts);
}

static Ref
MakeMarkItem(void)
{
	static const char* const* const kArts[1] = { kMarkArt };
	static const long kLefts[1] = { 4 };
	return MakeArtItem(1, kArts, kLefts);
}


/*------------------------------------------------------------------------------
	T h e   w o r d s   s h o w n   t h e   o t h e r   w a y
------------------------------------------------------------------------------*/

// One paragraph: its selection, each range let out to the whole of every
// run it touches (a word is shown one way or the other, never in part),
// or all of it.
static void
ShowParagraphAs(TParagraphView* para, Boolean selectionOnly, Boolean asInk)
{
	if (!selectionOnly)
	{
		ShowRangeAs(para, 0, para->TextLength(), asInk);
		return;
	}
	enum { kMaxRanges = 16 };
	long starts[kMaxRanges], ends[kMaxRanges];
	long count = 0;
	{
		HiliteLoop loop(para);
		while (count < kMaxRanges && loop.Next())
		{
			TParagraphHilite* hilite = (TParagraphHilite*) loop.fCurrent;
			if (hilite == nil || hilite->fEnd <= hilite->fStart)
				continue;
			starts[count] = hilite->fStart;
			ends[count] = hilite->fEnd;
			count++;
		}
	}
	if (count == 0)
		return;
	RefVar styles(para->Styles());
	if (!IsArray(styles))
		return;
	for (long i = 0; i < count; i++)
	{
		long offset = 0;
		long start = starts[i], end = ends[i];
		for (long j = 0; j + 1 < Length(styles); j += 2)
		{
			long length = RINT(RefVar(GetArraySlotRef(styles, j)));
			if (offset < ends[i] && offset + length > starts[i])
			{
				if (offset < start)
					start = offset;
				if (offset + length > end)
					end = offset + length;
			}
			offset += length;
		}
		starts[i] = start;
		ends[i] = end;
	}
	// last range first, so the offsets of those before it hold
	for (;;)
	{
		long last = -1;
		for (long i = 0; i < count; i++)
			if (starts[i] >= 0 && (last < 0 || starts[i] > starts[last]))
				last = i;
		if (last < 0)
			break;
		ShowRangeAs(para, starts[last], ends[last], asInk);
		starts[last] = -1;
	}
}


long
ShowWritingAround(TView* keyView, Boolean asInk)
{
	if (keyView == nil)
		return 0;
	enum { kMaxParagraphs = 256 };
	TParagraphView* paragraphs[kMaxParagraphs];
	long count = 0;

	TView* editor = nil;
	if (keyView->ClassID() == clEditView)
		editor = keyView;
	else if (keyView->fParent != nil && keyView->fParent->ClassID() == clEditView)
		editor = keyView->fParent;
	if (editor != nil && editor->fChildren != nil)
	{
		// (the list is taken first: reading a word again may add to it)
		long children = editor->fChildren->Count();
		for (long i = 0; i < children && count < kMaxParagraphs; i++)
		{
			TView* child = editor->fChildren->At((short) i);
			if (child != nil && child->DerivedFrom(clParagraphView))
				paragraphs[count++] = (TParagraphView*) child;
		}
	}
	else if (keyView->DerivedFrom(clParagraphView))
		paragraphs[count++] = (TParagraphView*) keyView;

	Boolean selection = false;
	for (long i = 0; i < count && !selection; i++)
		selection = NOTNIL(paragraphs[i]->FirstHilite());
	for (long i = 0; i < count; i++)
		ShowParagraphAs(paragraphs[i], selection, asInk);
	return count;
}


/*------------------------------------------------------------------------------
	T h e   p o p - u p
------------------------------------------------------------------------------*/

// The paragraphs a mark goes on: those with something selected in them,
// or with nothing selected the one with the caret.
static Ref
ParagraphsToMark(TView* keyView)
{
	RefVar list(MakeArray(0));
	if (keyView == nil)
		return list;
	TView* editor = nil;
	if (keyView->ClassID() == clEditView)
		editor = keyView;
	else if (keyView->fParent != nil && keyView->fParent->ClassID() == clEditView)
		editor = keyView->fParent;
	if (editor != nil && editor->fChildren != nil)
	{
		long children = editor->fChildren->Count();
		for (long i = 0; i < children; i++)
		{
			TView* child = editor->fChildren->At((short) i);
			if (child != nil && child->DerivedFrom(clParagraphView) && NOTNIL(child->FirstHilite()))
				AddArraySlot(list, child->fContext);
		}
	}
	if (Length(list) == 0 && keyView->DerivedFrom(clParagraphView))
		AddArraySlot(list, keyView->fContext);
	return list;
}


// The pop-up's pickActionScript: a pick of the strip or the mark is
// carried out here, any other handed to the ROM's pop-up as it would have
// had it.  The mark is the Marked application's business (HostMarks.ns,
// the global HostMarks:host): ToggleMarks(paragraphs).
static Ref
FCaretWritingPick(RefArg rcvr, RefArg picked)
{
	if (IsFrame(picked))
	{
		RefVar index(GetFrameSlot(picked, RSSYMindex));
		RefVar strip(GetFrameSlot(rcvr, RefVar(Intern((char*) "writingIndex"))));
		RefVar mark(GetFrameSlot(rcvr, RefVar(Intern((char*) "markIndex"))));
		if (ISINT(index) && ISINT(strip) && RINT(index) == RINT(strip))
		{
			RefVar x(GetFrameSlot(picked, RSSYMx));
			ShowWritingAround(gRootView->fCaretView, ISINT(x) && RINT(x) == 1);
			return NILREF;
		}
		if (ISINT(index) && ISINT(mark) && RINT(index) == RINT(mark))
		{
			RefVar marks(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostMarks:host"))));
			if (IsFrame(marks))
			{
				RefVar args(MakeArray(1));
				SetArraySlot(args, 0, RefVar(ParagraphsToMark(gRootView->fCaretView)));
				DoMessage(marks, RefVar(Intern((char*) "ToggleMarks")), args);
			}
			return NILREF;
		}
	}
	RefVar args(MakeArray(1));
	SetArraySlot(args, 0, picked);
	return DoMessage(RefVar(GetFrameSlot(rcvr, RefVar(Intern((char*) "romPopup")))), RSSYMpickactionscript, args);
}


Ref
CaretPopupWithWriting(RefArg popup)
{
	Boolean writing = KeepInk();
	Boolean marking = HostFeature(kFeatureMarking);
	if ((!writing && !marking) || !IsFrame(popup))
		return popup;
	RefVar items(GetProtoVariable(popup, RSSYMpopup, nil));
	if (!IsArray(items))
		return popup;
	long count = Length(items);
	RefVar all(MakeArray(count));
	for (long i = 0; i < count; i++)
		SetArraySlot(all, i, RefVar(GetArraySlotRef(items, i)));

	RefVar with(AllocateFrame());
	if (writing)
	{
		SetFrameSlot(with, RefVar(Intern((char*) "writingIndex")), RefVar(MAKEINT(Length(all))));
		AddArraySlot(all, RefVar(MakeStripItem()));
	}
	if (marking)
	{
		SetFrameSlot(with, RefVar(Intern((char*) "markIndex")), RefVar(MAKEINT(Length(all))));
		AddArraySlot(all, RefVar(MakeMarkItem()));
	}
	SetFrameSlot(with, RSSYMpopup, all);
	SetFrameSlot(with, RSSYMpickactionscript, RefVar(MakeCFunction((void*) FCaretWritingPick, 1, nil)));
	SetFrameSlot(with, RefVar(Intern((char*) "romPopup")), popup);
	return with;
}
