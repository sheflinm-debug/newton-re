/*
	File:		host/HostMailEngine.h

	Contains:	DEVIATION (host addition; docs/modern/transports.md, Internet
				mail): a message sent by SMTP and new messages fetched by
				IMAP, each a job carried on a step at a time by polling
				(never waiting), over print/host/HostIPPSocket.h - TCP, and
				TLS from the first byte for smtps:// (465) and imaps://
				(993), the certificate checked against the system's store.
				Plain C++: no Newton headers.

				Servers are URLs: smtp://host[:port] (25), smtps://host[:port]
				(465), imap://host[:port] (143), imaps://host[:port] (993).
				SMTP: EHLO, AUTH PLAIN when there is a user, MAIL FROM, RCPT
				TO each, DATA, QUIT.  IMAP: LOGIN, SELECT INBOX, UID SEARCH
				UNSEEN, each UID FETCH BODY.PEEK[] then UID STORE +FLAGS
				(\Seen), LOGOUT; at most kMostFetched messages a check.
				NOT YET: STARTTLS (port 587 and 143 upgraded), OAuth (the
				large providers want it for IMAP), other folders.
*/

#ifndef __HOSTMAILENGINE_H
#define __HOSTMAILENGINE_H

#include <stddef.h>
#include "HostMailFormat.h"		// (HostMailParseServer)

const int kHostMailMostFetched = 20;

struct HostMailJob;

// A message sent: the server, the login (user "" for none), the envelope
// sender and recipients, and the message (dot-stuffed as DATA has it,
// HostMailDotStuff).  ==> the job, NULL when the URL is not a mail server's.
HostMailJob*	HostMailStartSend(const char* serverURL, const char* user, const char* password,
								  const char* from, const char* const* to, int toCount, const char* data);

// New messages fetched (and marked seen).
HostMailJob*	HostMailStartFetch(const char* serverURL, const char* user, const char* password);

// The job carried on: ==> 0 still going, 1 done, -1 failed (HostMailError).
int		HostMailStep(HostMailJob* job);
const char*	HostMailError(HostMailJob* job);

// A fetch's messages, as the server sent them.
int		HostMailFetchedCount(HostMailJob* job);
const char*	HostMailFetched(HostMailJob* job, int index, size_t* size);

bool	HostMailIsFetch(HostMailJob* job);
void	HostMailEnd(HostMailJob* job);

#endif	/* __HOSTMAILENGINE_H */
