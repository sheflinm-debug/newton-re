/*
	File:		host/HostMailFormat.h

	Contains:	DEVIATION (host addition; docs/modern/transports.md, Internet
				mail): mail messages as RFC 5322 has them, made and read -
				plain C++, no Newton headers (tests/test_HostMail.cpp).

				Made: a plain text message, UTF-8, quoted-printable (so any
				SMTP server takes it), the subject and the sender's name
				encoded as RFC 2047 says when they are not ASCII.
				Read: the headers unfolded; From, Subject and Date (RFC 2047
				words decoded); the text - a text/plain part (the first,
				inside multipart/alternative or mixed), or text/html with
				its tags taken out when there is no plain one - its
				quoted-printable or base64 undone, UTF-8, ISO 8859-1 or
				Windows-1252 made UTF-8.
				NOT YET: attachments (listed as "[attachment: name]"),
				other character sets (passed through as they are), HTML
				beyond its text.
*/

#ifndef __HOSTMAILFORMAT_H
#define __HOSTMAILFORMAT_H

#include <stddef.h>

// A message made (malloc'd, CRLF line ends): from "Name <address>" or an
// address, to the addresses joined with ", ", the date as RFC 5322 has it
// ("Thu, 12 Nov 2026 14:05:00 +1100"), messageId "<...>".
char*	HostMailCompose(const char* fromName, const char* fromAddress, const char* to,
						const char* subject, const char* text, const char* date, const char* messageId);

// The message dot-stuffed for SMTP's DATA, the ending "." line added
// (malloc'd).
char*	HostMailDotStuff(const char* message);

struct HostMailMessage
{
	char*	fromName;		// "" when the From header has none
	char*	fromAddress;
	char*	subject;
	char*	date;			// the Date header as written
	char*	text;			// UTF-8, LF line ends
};

// A message read.  ==> 0 and `out` filled (free it with HostMailFree).
int		HostMailParse(const char* raw, size_t size, HostMailMessage* out);
void	HostMailFree(HostMailMessage* message);

// base64 (malloc'd; *size the decoded length)
char*	HostMailBase64Encode(const unsigned char* data, size_t size);
unsigned char*	HostMailBase64Decode(const char* text, size_t length, size_t* size);

// A server URL taken apart - smtp://, smtps:// (TLS), imap://, imaps:// -
// the port its scheme's when it names none (25, 465, 143, 993).  ==>
// false when it is not one.
bool	HostMailParseServer(const char* url, bool imap, char* host, size_t hostSize, int* port, bool* tls);

// The RFC 5322 date of now, in the host's zone.
void	HostMailDate(char* buffer, size_t size);

#endif	/* __HOSTMAILFORMAT_H */
