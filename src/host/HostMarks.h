/*
	File:		host/HostMarks.h

	Contains:	DEVIATION (host addition; docs/modern/marking.md): the
				Marked application (HostMarks.ns) and HostFeatureOn.
*/

#ifndef __HOSTMARKS_H
#define __HOSTMARKS_H

void	HostInstallMarks(void);				// at boot, before the Host panel: HostFeatureOn, the application evaluated and started
void	HostMarksFeatureChanged(void);		// marking turned on or off: the application installed, or its icon taken away

#endif	/* __HOSTMARKS_H */
