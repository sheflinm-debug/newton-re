/*
	File:		recognition/XrDump.cpp

	Contains:	The record of the xrs the cursive reader reads (XrDump.h).

	DEVIATION: all of it.
*/

#include "XrDump.h"
#include "CursiveReader.h"		// xrdata_type
#include "LowLevel.h"			// xrd_el_type, XrGetH
#include "XrReader.h"			// RWG_type, RWS_type
#include <stdio.h>
#include <stdlib.h>

static FILE*
DumpFile(void)
{
	static int sState = -1;		// -1 not yet read, 0 off, 1 open
	static FILE* sFile = nullptr;
	if (sState < 0)
	{
		const char* path = getenv("NEWTON_XR_DUMP");
		sState = 0;
		if (path != nullptr && *path != 0)
		{
			sFile = fopen(path, "a");
			if (sFile != nullptr)
				sState = 1;
			else
				fprintf(stderr, "[xrdump] cannot open %s\n", path);
		}
	}
	return sState == 1 ? sFile : nullptr;
}


bool
XrDumping(void)
{
	return DumpFile() != nullptr;
}


void
XrDumpMark(const char* tag)
{
	FILE* f = DumpFile();
	if (f == nullptr)
		return;
	fputs("{\"mark\": \"", f);
	for (const char* p = tag; *p != 0; p++)
	{
		if (*p == '"' || *p == '\\')
			fputc('\\', f);
		if ((unsigned char) *p >= 0x20)
			fputc(*p, f);
	}
	fputs("\"}\n", f);
	fflush(f);
}


void
XrDumpWord(const xrdata_type* xr, long nPoints)
{
	FILE* f = DumpFile();
	if (f == nullptr || xr == nullptr)
		return;
	fprintf(f, "{\"points\": %ld, \"xrs\": [", nPoints);
	const xrd_el_type* e = (const xrd_el_type*) xr->fElements;
	for (long i = 0; e != nullptr && i < xr->fLength; i++)
	{
		const xrd_el_type* x = &e[i];
		fprintf(f, "%s[%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d]", i == 0 ? "" : ", ",
				x->type, x->attrib, x->penalty, x->height, x->shift, x->orient, x->link,
				XrGetH(x->begpoint), XrGetH(x->endpoint),
				XrGetH(x->box + kXrLeft), XrGetH(x->box + kXrTop), XrGetH(x->box + kXrRight), XrGetH(x->box + kXrBottom));
	}
	fputs("]}\n", f);
	fflush(f);
}


void
XrDumpGraph(const RWG_type* rwg)
{
	FILE* f = DumpFile();
	if (f == nullptr || rwg == nullptr || rwg->rws == nullptr)
		return;
	fputs("{\"graph\": [[", f);
	bool firstSym = true;
	for (long i = 0; i < rwg->size; i++)
	{
		const RWS_type* s = &rwg->rws[i];
		if (s->type == 4)
		{
			fputs("], [", f);
			firstSym = true;
		}
		else if (s->type == 1)
		{
			fprintf(f, "%s[%d, %d, %d, %d, %d]", firstSym ? "" : ", ", s->sym, s->var, (int) s->xrStart - 1, s->xrLen, s->weight);
			firstSym = false;
		}
	}
	fputs("]]}\n", f);
	fflush(f);
}
