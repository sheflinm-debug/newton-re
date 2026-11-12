/*
	File:		host/HostMarks.cpp

	Contains:	DEVIATION (host addition; docs/modern/marking.md): the
				Marked application (HostMarks.ns, compiled in as a string)
				evaluated at boot, kept in the global HostMarks:host and
				started - which installs it only when marking is on
				(host/Features.h).  And the native the modern additions'
				NewtonScript asks whether it is on:
				  HostFeatureOn(name) - 'marking, 'structure.
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
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostMarksSource))));
		RefVar marks(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostMarks:host")), marks);
		NSSend(marks, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Marked application did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostMarksFeatureChanged(void)
{
	RefVar marks(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostMarks:host"))));
	if (!IsFrame(marks))
		return;
	newton_try
	{
		NSSend(marks, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Marked application: %s\n", CurrentException()->name);
	}
	end_try;
}
