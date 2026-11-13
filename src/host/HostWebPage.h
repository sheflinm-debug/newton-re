/*
	File:		host/HostWebPage.h

	Contains:	DEVIATION (host addition; docs/modern/web.md, tier 2): a web
				page made Newton text - the HTTP response taken apart, URLs
				resolved, the HTML read into a small tree and its article
				found and written out as headings, paragraphs, list items,
				quotations and preformatted text, its links numbered.
				Plain C++, no Newton headers (tests/test_HostWeb.cpp).

				The article is found as reader views find it (the idea of
				Mozilla's Readability, written here from scratch, not taken
				from it): navigation, headers, footers, sidebars, forms,
				scripts and whatever is classed as a menu, a comment, an
				advertisement or the like is left out; each paragraph of
				some length scores its parent (and half its grandparent) by
				its length and commas; the best scoring element, with its
				siblings that score near it, is the article - the whole body
				when nothing scores.  No script runs; no style is applied.
*/

#ifndef __HOSTWEBPAGE_H
#define __HOSTWEBPAGE_H

#include <stddef.h>

// ---- URLs ---------------------------------------------------------------

struct HostWebURL
{
	bool		tls;
	char		host[256];
	int			port;
	char		path[2048];		// the path and query, "/" at least; no fragment
};

// An absolute http:// or https:// URL taken apart.  ==> false when it is not one.
bool	HostWebParseURL(const char* url, HostWebURL* parts);

// A reference (absolute, "//host/...", "/path", "path", "?query", "#here")
// made absolute against a base URL (malloc'd); NULL for what is not a page
// to go to (javascript:, mailto:, a fragment of the same page).
char*	HostWebResolve(const char* base, const char* reference);

// What the address line holds made a URL: an address as it is ("http" added
// to "example.org/x"), anything else a search with the engine's URL
// ("%s" the words, escaped).  (malloc'd)
char*	HostWebAddress(const char* typed, const char* searchURL);

// ---- HTTP ---------------------------------------------------------------

struct HostWebResponse
{
	int		status;				// 0: not HTTP
	char	contentType[128];	// lower case, no parameters
	char	charset[48];		// lower case, "" when the header names none
	char	location[2048];		// a redirect's
	char*	body;				// malloc'd, de-chunked, 0-terminated
	size_t	bodySize;
};

// A whole response (the connection closed) taken apart.  ==> false when it
// is not HTTP.
bool	HostWebParseResponse(const char* data, size_t size, HostWebResponse* out);
void	HostWebFreeResponse(HostWebResponse* response);

// ---- pages --------------------------------------------------------------

enum HostWebBlockKind { kWebParagraph, kWebHeading, kWebItem, kWebQuote, kWebPre };

struct HostWebBlock
{
	HostWebBlockKind	kind;
	int					level;		// a heading's (1-6)
	char*				text;		// UTF-8; a link's text followed by " [n]"
};

struct HostWebPage
{
	char*			title;
	HostWebBlock*	blocks;
	int				blockCount;
	char**			links;			// absolute URLs; [n] is links[n - 1]
	int				linkCount;
};

// A page's HTML (in its charset: UTF-8, or ISO 8859-1/Windows-1252 made
// UTF-8 - the header's, else the page's own <meta>) made Newton text.  A
// text/plain body is one preformatted block.  ==> 0.
int		HostWebMakePage(const char* body, size_t size, const char* charset, bool plainText,
						const char* url, HostWebPage* out);
void	HostWebFreePage(HostWebPage* page);

#endif	/* __HOSTWEBPAGE_H */
