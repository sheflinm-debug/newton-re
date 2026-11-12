/*
	File:		LiveDetails.h

	Contains:	DEVIATION (host; docs/modern/structure.md, "Live details"):
				not in the ROM.  With structure on (host/Features.h), the
				details a paragraph's text holds are found as it is drawn,
				underlined with a dotted line, and a tap on one offers what
				can be done with it:

				  a web address ("http://", "https://", "www.")  Open
				  an e-mail address (name@host.domain)            Mail
				  a telephone number (7 to 15 digits, with spaces,
				    dashes, brackets, dots, a leading +)          Call, Text

				Nothing is stored: the details are found again from the
				text each time, so they cannot fall out of step with it.
				What a choice does is the NewtonScript side's (HostMarks.ns,
				DetailTapped): Open, Mail and Call hand the address to the
				device (HostOpenURL: the host's browser, mail program or
				telephone), Text opens Messages where there is one.
*/

#ifndef __LIVEDETAILS_H
#define __LIVEDETAILS_H

#include "Newton.h"
#include "objects.h"

class TParagraphView;
struct Point;

enum DetailKind { kDetailNone, kDetailURL, kDetailEmail, kDetailPhone };

// The detail at or after `from` in the text: its kind, start and length
// (kDetailNone when there is no more).
DetailKind	FindDetail(const UniChar* text, long length, long from, long* start, long* count);

// the paragraph's details underlined (its PostDraw)
void		DrawDetails(TParagraphView* view);

// a tap on the paragraph: true when it was on a detail, and was offered
Boolean		TapDetail(TParagraphView* view, Point& pt);

#endif	/* __LIVEDETAILS_H */
