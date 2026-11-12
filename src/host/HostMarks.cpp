/*
	File:		host/HostMarks.cpp

	Contains:	DEVIATION (host addition; docs/modern/marking.md): the
				Marked application (HostMarks.ns, compiled in as a string)
				evaluated at boot, kept in the global HostMarks:host and
				started - which installs it only when marking is on
				(host/Features.h).  And the native the modern additions'
				NewtonScript asks whether it is on:
				  HostFeatureOn(name) - 'marking, 'structure.
*/

#include "HostMarks.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include <stdio.h>
#include <string.h>

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


void
HostInstallMarks(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFeatureOn")), RefVar(MakeCFunction((void*) FHostFeatureOn, 1, nil)));
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
