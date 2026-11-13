/*
	File:		host/HostLookInside.h

	Contains:	DEVIATION (host addition; docs/modern/malleable.md): the
				inspector, the Script slip and tweaks (HostLookInside.ns) and
				their natives.  See HostLookInside.cpp.
*/

#ifndef __HOSTLOOKINSIDE_H
#define __HOSTLOOKINSIDE_H

void	HostInstallLookInside(void);			// at boot, the last of the host's additions
void	HostLookInsideFeatureChanged(void);		// "Look inside" turned on or off on the panel
void	HostSetNoTweaks(void);					// newton --no-tweaks

#endif	/* __HOSTLOOKINSIDE_H */
