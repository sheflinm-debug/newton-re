/*
	File:		host/HostFiles.h

	Contains:	DEVIATION (host addition; docs/modern/files.md): the Files
				application (HostFiles.ns) and the natives it reads the
				host's folder with.  See HostFiles.cpp.
*/

#ifndef __HOSTFILES_H
#define __HOSTFILES_H

void	HostInstallFiles(void);				// at boot, before the Host panel
void	HostFilesFeatureChanged(void);		// "Files" turned on or off on the panel

#endif	/* __HOSTFILES_H */
