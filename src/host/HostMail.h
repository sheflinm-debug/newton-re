/*
	File:		host/HostMail.h

	Contains:	DEVIATION (host addition; docs/modern/transports.md): the
				Mail transport (HostMail.ns) and its natives.  See
				HostMail.cpp.
*/

#ifndef __HOSTMAIL_H
#define __HOSTMAIL_H

void	HostInstallMail(void);				// at boot
void	HostMailFeatureChanged(void);		// "Internet mail" turned on or off on the panel

#endif	/* __HOSTMAIL_H */
