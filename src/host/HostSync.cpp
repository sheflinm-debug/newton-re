/*
	File:		host/HostSync.cpp

	Contains:	DEVIATION (host addition; docs/modern/sync.md): sync between
				Newtons through a folder (HostSync.ns, compiled in as a
				string) evaluated at boot, kept in the global HostSync:host
				and started - it watches its soups only when the Host panel's
				"Sync" is on (host/Features.h's kFeatureSync, NEWTON_SYNC) -
				and the natives it uses.

				  HostSyncFolder() - the sync folder (NEWTON_SYNC_FOLDER, else
					  "Newton Sync" in the user's home), made when it is not
					  there; nil when it cannot be.
				  HostSyncList() - the names of the .nsof files in it.
				  HostSyncRead(name) - a file's bytes as a binary, nil when it
					  cannot be read.
				  HostSyncWrite(name, binary) - the file written whole and
					  put in place in one step (a temporary file renamed
					  over it), so another device never reads half of it.
				  HostSyncSeconds() - the host's clock, seconds since 1970,
					  as a 12-digit string (NEWTON_SYNC_CLOCK_OFFSET seconds
					  added, for the tests' wrong clocks).
				  HostSyncEvery() - seconds between syncs that look for
					  the other devices' changes (NEWTON_SYNC_EVERY, 300; 0
					  never).
				  HostSyncRandomID() - 128 random bits from the system's
					  generator, 32 hex digits.

				A name is a plain file name: one with a path in it is
				refused.
*/

#include "HostSync.h"
#include "HostFilesOS.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include "RSSymbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const char gHostSyncSource[];		// HostSync.ns (HostSyncSource.cpp, generated)

static const long kMostSyncFile = 64L * 1024 * 1024;


static const char*
SyncFolder(void)
{
	static char sFolder[1024] = "";
	if (sFolder[0] == 0)
	{
		const char* asked = getenv("NEWTON_SYNC_FOLDER");
		if (asked != NULL && asked[0] != 0)
			snprintf(sFolder, sizeof(sFolder), "%s", asked);
		else
			snprintf(sFolder, sizeof(sFolder), "%s/Newton Sync", HostFilesHome());
		for (char* p = sFolder; *p; p++)
			if (*p == '\\')
				*p = '/';
		size_t n = strlen(sFolder);
		while (n > 1 && sFolder[n - 1] == '/')
			sFolder[--n] = 0;
	}
	return sFolder;
}

// a plain file name (ASCII here: device ids), NULL when it is not one
static char*
NameOf(RefArg name)
{
	if (!IsString(name))
		return NULL;
	const UniChar* u = (const UniChar*) BinaryData(name);
	long length = (Length(name) - 2) / (long) sizeof(UniChar);
	char* out = (char*) malloc((size_t) length + 1);
	long n = 0;
	for (long i = 0; i < length && u[i] != 0; i++)
	{
		UniChar c = u[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
		if (!ok || (c == '.' && (n == 0 || out[n - 1] == '.')))
		{
			free(out);
			return NULL;
		}
		out[n++] = (char) c;
	}
	out[n] = 0;
	if (n == 0)
	{
		free(out);
		return NULL;
	}
	return out;
}

static char*
PathOf(const char* name)
{
	char* path = (char*) malloc(strlen(SyncFolder()) + strlen(name) + 2);
	sprintf(path, "%s/%s", SyncFolder(), name);
	return path;
}


static Ref
FHostSyncFolder(RefArg /*rcvr*/)
{
	if (HostFilesMakeDirs(SyncFolder()) != 0)
		return NILREF;
	return MakeString(SyncFolder());
}


static Ref
FHostSyncList(RefArg /*rcvr*/)
{
	const int kMax = 500;
	HostFileInfo* infos = (HostFileInfo*) malloc(sizeof(HostFileInfo) * kMax);
	int count = HostFilesListDir(SyncFolder(), infos, kMax);
	RefVar list(MakeArray(0));
	for (int i = 0; i < count; i++)
	{
		size_t n = strlen(infos[i].name);
		if (!infos[i].folder && n > 5 && strcmp(infos[i].name + n - 5, ".nsof") == 0)
			AddArraySlot(list, RefVar(MakeString(infos[i].name)));
	}
	free(infos);
	return list;
}


static Ref
FHostSyncRead(RefArg /*rcvr*/, RefArg name)
{
	char* file = NameOf(name);
	if (file == NULL)
		return NILREF;
	char* path = PathOf(file);
	free(file);
	long long total = 0;
	char probe[1];
	RefVar result;
	if (HostFilesReadPrefix(path, probe, 0, &total) >= 0 && total > 0 && total <= kMostSyncFile)
	{
		char* data = (char*) malloc((size_t) total);
		long got = HostFilesReadPrefix(path, data, (long) total, &total);
		if (got == (long) total)
		{
			result = AllocateBinary(RSSYMbinary, got);
			memcpy(BinaryData(result), data, (size_t) got);
		}
		free(data);
	}
	free(path);
	return result;
}


static Ref
FHostSyncWrite(RefArg /*rcvr*/, RefArg name, RefArg binary)
{
	char* file = NameOf(name);
	if (file == NULL || !IsBinary(binary) || IsString(binary))
	{
		free(file);
		return NILREF;
	}
	HostFilesMakeDirs(SyncFolder());
	char* path = PathOf(file);
	char* temporary = (char*) malloc(strlen(path) + 8);
	sprintf(temporary, "%s.part", path);
	long size = Length(binary);
	char* copy = (char*) malloc((size_t) size + 1);
	memcpy(copy, BinaryData(binary), (size_t) size);		// (copied first: the heap may move it)
	int err = HostFilesWrite(temporary, copy, size);
	if (err == 0)
		err = HostFilesReplace(temporary, path);
	fprintf(stderr, "[sync] %s %s (%ld bytes)\n", err == 0 ? "wrote" : "could not write", path, size);
	free(copy);
	free(temporary);
	free(path);
	free(file);
	return MAKEBOOLEAN(err == 0);
}


static Ref
FHostSyncSeconds(RefArg /*rcvr*/)
{
	static long long sOffset = 0;
	static bool sRead = false;
	if (!sRead)
	{
		const char* asked = getenv("NEWTON_SYNC_CLOCK_OFFSET");
		sOffset = asked != NULL ? atoll(asked) : 0;
		sRead = true;
	}
	long long now = (long long) time(NULL) + sOffset;
	if (now < 0)
		now = 0;
	char text[32];
	snprintf(text, sizeof(text), "%012lld", now);
	return MakeString(text);
}


static Ref
FHostSyncEvery(RefArg /*rcvr*/)
{
	static long sEvery = -1;
	if (sEvery < 0)
	{
		const char* asked = getenv("NEWTON_SYNC_EVERY");
		sEvery = asked != NULL ? atol(asked) : 300;
		if (sEvery < 0)
			sEvery = 0;
	}
	return MAKEINT(sEvery);
}


static Ref
FHostSyncRandomID(RefArg /*rcvr*/)
{
	unsigned char bytes[16];
	if (HostFilesRandom(bytes, sizeof(bytes)) != 0)
		return NILREF;
	char text[33];
	for (int i = 0; i < 16; i++)
		snprintf(text + i * 2, 3, "%02x", bytes[i]);
	return MakeString(text);
}


void
HostInstallSync(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncFolder")), RefVar(MakeCFunction((void*) FHostSyncFolder, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncList")), RefVar(MakeCFunction((void*) FHostSyncList, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncRead")), RefVar(MakeCFunction((void*) FHostSyncRead, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncWrite")), RefVar(MakeCFunction((void*) FHostSyncWrite, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncSeconds")), RefVar(MakeCFunction((void*) FHostSyncSeconds, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncEvery")), RefVar(MakeCFunction((void*) FHostSyncEvery, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostSyncRandomID")), RefVar(MakeCFunction((void*) FHostSyncRandomID, 0, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostSyncSource))));
		RefVar sync(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostSync:host")), sync);
		NSSend(sync, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] sync did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostSyncFeatureChanged(void)
{
	RefVar sync(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostSync:host"))));
	if (!IsFrame(sync))
		return;
	newton_try
	{
		NSSend(sync, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] sync: %s\n", CurrentException()->name);
	}
	end_try;
}
