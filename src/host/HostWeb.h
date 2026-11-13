/*
	File:		host/HostWeb.h

	Contains:	DEVIATION (host addition; docs/modern/web.md): the Web
				application (HostWeb.ns) and its natives.  See HostWeb.cpp.
*/

#ifndef __HOSTWEB_H
#define __HOSTWEB_H

void	HostInstallWeb(void);				// at boot, after HostInstallFiles
void	HostWebFeatureChanged(void);		// "Web" turned on or off on the panel

#endif	/* __HOSTWEB_H */
