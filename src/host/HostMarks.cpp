/*
	File:		host/HostMarks.cpp

	Contains:	DEVIATION (host addition; docs/modern/marking.md): the
				Marked application (HostMarks.ns, compiled in as a string)
				evaluated at boot, kept in the global HostMarks:host and
				started - which installs it only when marking is on
				(host/Features.h).  And the native the modern additions'
				NewtonScript asks whether it is on:
				  HostFeatureOn(name) - 'marking, 'structure, 'permissions.
				And the Permissions application (HostPermissions.ns,
				docs/modern/permissions.md), likewise kept in the global
				HostPermissions:host and started.
				And HostOpenURL(url): a web, mail or telephone address handed
				to the device - its browser, mail program or telephone (live
				details, views/LiveDetails.h).  Only http:, https:, mailto:
				and tel: are passed on; NEWTON_OPEN_URL=print only prints
				what would have been opened (the ctests).
*/

#include "HostMarks.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "HostOpenURL.h"

extern const char gHostMarksSource[];		// HostMarks.ns (HostMarksSource.cpp, generated)
extern const char gHostPermissionsSource[];	// HostPermissions.ns (HostPermissionsSource.cpp, generated)

// the applications started here: their source, and the global each is kept in
static const struct { const char* source; const char* global; const char* title; } kApps[] =
{
	{ gHostMarksSource, "HostMarks:host", "Marked" },
	{ gHostPermissionsSource, "HostPermissions:host", "Permissions" }
};


// HostFeatureOn(name): whether that modern addition is on
static Ref
FHostFeatureOn(RefArg /*rcvr*/, RefArg name)
{
	if (!IsSymbol(name))
		return NILREF;
	const char* asked = SymbolName(name);
	for (int id = 0; id < kFeatureCount; id++)
		if (strcmp(asked, HostFeatureSetting(id)) == 0)
			return HostFeature(id) ? TRUEREF : NILREF;
	return NILREF;
}


// HostOpenURL(url): true when it was handed on
static Ref
FHostOpenURL(RefArg /*rcvr*/, RefArg url)
{
	if (!IsString(url))
		return NILREF;
	// the address as plain ASCII, nothing that could reach a shell
	char text[1024];
	const UniChar* u = (const UniChar*) BinaryData(url);
	long length = (Length(url) - 2) / (long) sizeof(UniChar);
	if (length <= 0 || length >= (long) sizeof(text))
		return NILREF;
	for (long i = 0; i < length; i++)
	{
		UniChar c = u[i];
		if (c <= ' ' || c >= 0x7F || c == '"' || c == '\'' || c == '`' || c == '\\' || c == '<' || c == '>' || c == '|' || c == '^')
			return NILREF;
		text[i] = (char) c;
	}
	text[length] = 0;
	if (strncmp(text, "http://", 7) != 0 && strncmp(text, "https://", 8) != 0
	 && strncmp(text, "mailto:", 7) != 0 && strncmp(text, "tel:", 4) != 0)
		return NILREF;
	static int sPrintOnly = -1;
	if (sPrintOnly < 0)
	{
		const char* how = getenv("NEWTON_OPEN_URL");
		sPrintOnly = how != NULL && strcmp(how, "print") == 0;
	}
	fprintf(stderr, "[host] open %s%s\n", text, sPrintOnly ? " (not opened: NEWTON_OPEN_URL=print)" : "");
	if (sPrintOnly)
		return TRUEREF;
	return MAKEBOOLEAN(HostOpenURLText(text));
}


void
HostInstallMarks(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFeatureOn")), RefVar(MakeCFunction((void*) FHostFeatureOn, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostOpenURL")), RefVar(MakeCFunction((void*) FHostOpenURL, 1, nil)));
	for (size_t i = 0; i < sizeof(kApps) / sizeof(kApps[0]); i++)
	{
		newton_try
		{
			RefVar fn(ParseString(RefVar(MakeString(kApps[i].source))));
			RefVar app(InterpretBlock(fn, RefVar()));
			SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) kApps[i].global)), app);
			NSSend(app, RefVar(Intern((char*) "Start")));
		}
		newton_catch_all
		{
			fprintf(stderr, "[host] the %s application did not start: %s\n", kApps[i].title, CurrentException()->name);
		}
		end_try;
	}
}


// a modern addition turned on or off: each application started again
// (Start stops it when its addition is off)
void
HostMarksFeatureChanged(void)
{
	for (size_t i = 0; i < sizeof(kApps) / sizeof(kApps[0]); i++)
	{
		RefVar app(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) kApps[i].global))));
		if (!IsFrame(app))
			continue;
		newton_try
		{
			NSSend(app, RefVar(Intern((char*) "Start")));
		}
		newton_catch_all
		{
			fprintf(stderr, "[host] the %s application: %s\n", kApps[i].title, CurrentException()->name);
		}
		end_try;
	}
}
