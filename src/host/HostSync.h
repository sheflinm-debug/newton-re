/*
	File:		host/HostSync.h

	Contains:	DEVIATION (host addition; docs/modern/sync.md): sync between
				Newtons through a folder (HostSync.ns) and its natives.  See
				HostSync.cpp.
*/

#ifndef __HOSTSYNC_H
#define __HOSTSYNC_H

void	HostInstallSync(void);				// at boot
void	HostSyncFeatureChanged(void);		// "Sync" turned on or off on the panel

#endif	/* __HOSTSYNC_H */
