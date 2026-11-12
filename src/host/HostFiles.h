/*
	File:		host/HostFiles.h

	Contains:	DEVIATION (host addition; docs/modern/files.md): the Files
				application (HostFiles.ns), its soup, the Files transport
				(docs/modern/transports.md) and the natives they use.  See
				HostFiles.cpp.
*/

#ifndef __HOSTFILES_H
#define __HOSTFILES_H

void	HostInstallFiles(void);				// at boot, before the Host panel
void	HostFilesFeatureChanged(void);		// "Files" turned on or off on the panel

#endif	/* __HOSTFILES_H */
