/*
	File:		host/HostLookInside.cpp

	Contains:	DEVIATION (host addition; docs/modern/malleable.md): looking
				inside the running system and reshaping it - the inspector,
				the Script slip and tweaks (HostLookInside.ns, compiled in as
				a string) evaluated at boot, kept in the global
				HostLookInside:host and started - only when the Host panel's
				"Look inside" is on (host/Features.h's kFeatureLookInside,
				NEWTON_LOOK_INSIDE) - and the natives they use.

				  HostSourceOf(fn) - the source of a ROM function, from the
					  decompiled romsrc/ tree (romsrc/functions/*.ns, each
					  headed "@@ 0x<its ROM address>"); nil for a function
					  that is not the ROM's (a native, a package's, one
					  compiled since start - tweaks keep their own source).
					  The tree is read once, on the first ask.
				  HostStartsBegin() - a start counted: ==> how many starts
					  before this one never finished (HostStartsFinished
					  not called), the count kept in NEWTON_STARTS_FILE, or
					  ".newton-starts" in the home folder.
				  HostStartsFinished() - this start went well.
				  HostTweaksRefused() - why tweaks are not to be loaded
					  ("--no-tweaks", NEWTON_NO_TWEAKS), else nil.
*/

#include "HostLookInside.h"
#include "HostFilesOS.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include "ROMImport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char gHostLookInsideSource[];		// HostLookInside.ns (HostLookInsideSource.cpp, generated)

#ifndef NEWTON_ROMSRC_DIR
#define NEWTON_ROMSRC_DIR ""
#endif

static bool gNoTweaks = false;

void
HostSetNoTweaks(void)
{
	gNoTweaks = true;
}


// --- the ROM's functions' source ------------------------------------------

struct SourceEntry
{
	Ref		fn;			// the function, as the host has it
	char*	path;
};

static SourceEntry*	gSources = NULL;
static int			gSourceCount = -1;		// -1: not read yet

static const char*
RomsrcDir(void)
{
	const char* asked = getenv("NEWTON_ROMSRC");
	return asked != NULL && asked[0] != 0 ? asked : NEWTON_ROMSRC_DIR;
}

static void
ReadSources(void)
{
	gSourceCount = 0;
	char dir[1200];
	snprintf(dir, sizeof(dir), "%s/functions", RomsrcDir());
	const int kMax = 12000;
	HostFileInfo* infos = (HostFileInfo*) malloc(sizeof(HostFileInfo) * kMax);
	int count = HostFilesListDir(dir, infos, kMax);
	if (count <= 0)
	{
		free(infos);
		fprintf(stderr, "[look inside] no romsrc/functions at %s: no source for the ROM's functions\n", dir);
		return;
	}
	gSources = (SourceEntry*) malloc(sizeof(SourceEntry) * (size_t) count);
	for (int i = 0; i < count; i++)
	{
		char path[1800];
		snprintf(path, sizeof(path), "%s/%s", dir, infos[i].name);
		char head[40];
		long long total = 0;
		long got = HostFilesReadPrefix(path, head, sizeof(head) - 1, &total);
		if (got < 6)
			continue;
		head[got] = 0;
		if (strncmp(head, "@@ 0x", 5) != 0)
			continue;
		unsigned long address = strtoul(head + 5, NULL, 16);
		Ref fn = TranslateROMRef((ULong32) address);
		if (ISNIL(fn))
			continue;
		gSources[gSourceCount].fn = fn;
		gSources[gSourceCount].path = (char*) malloc(strlen(path) + 1);
		strcpy(gSources[gSourceCount].path, path);
		gSourceCount++;
	}
	free(infos);
	fprintf(stderr, "[look inside] the source of %d ROM functions found\n", gSourceCount);
}

static Ref
FHostSourceOf(RefArg /*rcvr*/, RefArg fn)
{
	if (gSourceCount < 0)
		ReadSources();
	for (int i = 0; i < gSourceCount; i++)
		if (gSources[i].fn == (Ref) fn)
		{
			long long total = 0;
			char probe[1];
			HostFilesReadPrefix(gSources[i].path, probe, 0, &total);
			char* text = (char*) malloc((size_t) total + 1);
			long got = HostFilesReadPrefix(gSources[i].path, text, (long) total, &total);
			if (got < 0)
			{
				free(text);
				return NILREF;
			}
			text[got] = 0;
			// the function: after the header (or "@@main"), up to "@@end"
			char* start = strstr(text, "@@main\n");
			start = start != NULL ? start + 7 : strchr(text, '\n');
			if (start == NULL)
			{
				free(text);
				return NILREF;
			}
			if (*start == '\n')
				start++;
			char* end = strstr(start, "\n@@end");
			if (end != NULL)
				*end = 0;
			// (as a Newton string: the file is ASCII, its escapes NewtonScript's)
			for (char* p = start; *p; p++)
				if (*p == '\n')
					*p = '\r';
			RefVar source(MakeString(start));
			free(text);
			return source;
		}
	return NILREF;
}


// --- starts that never finished -------------------------------------------

static const char*
StartsFile(void)
{
	static char sPath[1100] = "";
	if (sPath[0] == 0)
	{
		const char* asked = getenv("NEWTON_STARTS_FILE");
		if (asked != NULL && asked[0] != 0)
			snprintf(sPath, sizeof(sPath), "%s", asked);
		else
			snprintf(sPath, sizeof(sPath), "%s/.newton-starts", HostFilesHome());
	}
	return sPath;
}

static long
ReadStarts(void)
{
	char text[32];
	long long total = 0;
	long got = HostFilesReadPrefix(StartsFile(), text, sizeof(text) - 1, &total);
	if (got <= 0)
		return 0;
	text[got] = 0;
	return atol(text);
}

static void
WriteStarts(long n)
{
	char text[32];
	int length = snprintf(text, sizeof(text), "%ld\n", n);
	HostFilesWrite(StartsFile(), text, length);
}

static Ref
FHostStartsBegin(RefArg /*rcvr*/)
{
	long before = ReadStarts();
	WriteStarts(before + 1);
	return MAKEINT(before);
}

static Ref
FHostStartsFinished(RefArg /*rcvr*/)
{
	WriteStarts(0);
	return TRUEREF;
}

static Ref
FHostTweaksRefused(RefArg /*rcvr*/)
{
	if (gNoTweaks)
		return MakeString("--no-tweaks");
	const char* asked = getenv("NEWTON_NO_TWEAKS");
	if (asked != NULL && asked[0] != 0 && asked[0] != '0')
		return MakeString("NEWTON_NO_TWEAKS");
	return NILREF;
}


void
HostInstallLookInside(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSourceOf")), RefVar(MakeCFunction((void*) FHostSourceOf, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostStartsBegin")), RefVar(MakeCFunction((void*) FHostStartsBegin, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostStartsFinished")), RefVar(MakeCFunction((void*) FHostStartsFinished, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostTweaksRefused")), RefVar(MakeCFunction((void*) FHostTweaksRefused, 0, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostLookInsideSource))));
		RefVar look(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostLookInside:host")), look);
		NSSend(look, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] Look inside did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostLookInsideFeatureChanged(void)
{
	RefVar look(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostLookInside:host"))));
	if (!IsFrame(look))
		return;
	newton_try
	{
		NSSend(look, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] Look inside: %s\n", CurrentException()->name);
	}
	end_try;
}
