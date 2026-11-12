/*
	Features.h - the switches of the modern additions (docs/modern/).

	DEVIATION (host): each addition the owner chose for a modern NewtonOS
	is off unless asked for, and off is the ROM's machine
	(docs/modern/README.md, "Rules common to all of them").  Each has a
	setting on the Host preferences panel (host/HostSettings.h, kept in
	the System soup) and an environment variable that overrides the panel
	(1 on, 0 off) for tests.  Ink kept after recognition has its own
	header, host/KeepInk.h, as it came first.

	Header-only, as RomBugs.h and KeepInk.h are, so every library can ask
	without a link dependency; each variable is read once (the Windows
	CRT's getenv locks).
*/

#ifndef __FEATURES_H
#define __FEATURES_H

#include <stdlib.h>

enum HostFeatureId
{
	kFeatureMarking,		// docs/modern/marking.md: a star beside a line, and the Marked application
	kFeatureStructure,		// docs/modern/structure.md: checkboxes and lists recognised from writing
	kFeaturePermissions,	// docs/modern/permissions.md: what each package declares and is granted, recorded
	kFeatureAssistant,		// docs/modern/assistant.md: a sentence the Assistant cannot parse handed to a model
	kFeatureFiles,			// docs/modern/files.md: the Files application over a folder of the host's
	kFeatureCount
};

// the setting's name on the Host panel, and the variable that overrides it
inline const char*
HostFeatureSetting(int id)
{
	static const char* const kNames[kFeatureCount] = { "marking", "structure", "permissions", "assistant", "files" };
	return kNames[id];
}

inline const char*
HostFeatureVariable(int id)
{
	static const char* const kNames[kFeatureCount] = { "NEWTON_MARKING", "NEWTON_STRUCTURE", "NEWTON_PERMISSIONS", "NEWTON_ASSISTANT", "NEWTON_FILES" };
	return kNames[id];
}

inline int*
HostFeatureStates(void)
{
	static int sStates[kFeatureCount] = { -1, -1, -1, -1, -1 };		// -1: the environment not yet read
	return sStates;
}

// whether the environment says, one way or the other (the panel then
// cannot change it)
inline bool
HostFeatureFromEnvironment(int id)
{
	static int sSaid[kFeatureCount] = { -1, -1, -1, -1, -1 };
	if (sSaid[id] < 0)
	{
		const char* asked = getenv(HostFeatureVariable(id));
		sSaid[id] = asked != NULL && asked[0] != 0;
	}
	return sSaid[id] != 0;
}

inline bool
HostFeature(int id)
{
	int& state = HostFeatureStates()[id];
	if (state < 0)
	{
		const char* asked = getenv(HostFeatureVariable(id));
		state = asked != NULL && asked[0] != 0 && asked[0] != '0';
	}
	return state != 0;
}

inline void
SetHostFeature(int id, bool on)
{
	HostFeatureStates()[id] = on ? 1 : 0;
}

#endif	/* __FEATURES_H */
