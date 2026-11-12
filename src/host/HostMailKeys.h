/*
	File:		host/HostMailKeys.h

	Contains:	DEVIATION (host addition; docs/modern/transports.md): the
				passwords of mail accounts, kept by the host by account id -
				never in a soup.  On Windows the Credential Manager ("Newton
				mail/<id>", generic credentials of this user); in memory
				only (gone when newton quits) with NEWTON_MAIL_KEYSTORE=memory,
				as the tests run, and on other hosts until their key stores
				are used (NOT YET: the Secret Service, the Keychain).  The
				main account's password may instead come from
				NEWTON_MAIL_PASSWORD.  Plain C++.
*/

#ifndef __HOSTMAILKEYS_H
#define __HOSTMAILKEYS_H

bool		HostMailKeySet(const char* id, const char* password);
char*		HostMailKeyGet(const char* id);		// malloc'd, NULL when there is none
void		HostMailKeyForget(const char* id);
const char*	HostMailKeyStoreName(void);

#endif	/* __HOSTMAILKEYS_H */
