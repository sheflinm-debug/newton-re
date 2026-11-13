/*
	File:		host/HostWeb.cpp

	Contains:	DEVIATION (host addition; docs/modern/web.md, tier 2): the Web
				application (HostWeb.ns, compiled in as a string) evaluated
				at boot, kept in the global HostWeb:host and started - it
				installs itself only when the Host panel's "Web" is on
				(host/Features.h's kFeatureWeb, NEWTON_WEB) - and the natives
				it fetches with, over print/host/HostIPPSocket.h (TCP, TLS
				for https with the system's store vouching for the
				certificate) and HostWebPage.h (HTTP, the article).

				  HostWebAddressOf(typed) - what the address line holds made
					  a URL: an address as it is, anything else a search
					  (NEWTON_WEB_SEARCH, "%s" the words; DuckDuckGo's HTML
					  page by default).
				  HostWebFetch(url) - a page fetched: ==> a job, or {error}.
				  HostWebPoll(job) - 'busy, or (the job then ended)
					  {page: {title, url, blocks: [{kind, level, text}],
					  links: [url]}} for HTML and plain text,
					  {download: path, type, size, url} for anything else -
					  saved into the Files folder's Downloads - or {error}.
					  Redirects are followed (five at most).
*/

#include "HostWeb.h"
#include "HostWebPage.h"
#include "HostFilesOS.h"
#include "print/host/HostIPPSocket.h"
#include "HostSockets.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include "HostFilesPaths.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const char gHostWebSource[];			// HostWeb.ns (HostWebSource.cpp, generated)

static const long kWebTimeout = 30;				// seconds without progress
static const size_t kMostBytes = 16 * 1024 * 1024;
static const int kMostRedirects = 5;
static const int kMostJobs = 4;

struct WebJob
{
	THostIPPSocket	socket;
	char			url[2048];
	int				redirects;
	bool			connected;
	char*			out;
	size_t			outSize;
	size_t			outDone;
	char*			in;
	size_t			inSize;
	size_t			inRoom;
	time_t			lastProgress;
	char			error[256];
};

static WebJob* gJobs[kMostJobs];


// UTF-8 as a NewtonScript string (line ends the Newton's returns)
static Ref
StringOfUTF8(const char* text)
{
	size_t size = strlen(text);
	UniChar* u = (UniChar*) malloc((size + 1) * sizeof(UniChar));
	long n = 0;
	const unsigned char* p = (const unsigned char*) text;
	const unsigned char* end = p + size;
	while (p < end)
	{
		unsigned long c = *p++;
		if (c >= 0xC0)
		{
			int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
			c &= more == 3 ? 0x07 : more == 2 ? 0x0F : 0x1F;
			while (more-- > 0 && p < end)
				c = (c << 6) | (*p++ & 0x3F);
		}
		if (c == '\r')
			continue;
		if (c == '\n')
			c = 0x0D;
		u[n++] = c > 0xFFFF ? (UniChar) '?' : (UniChar) c;
	}
	RefVar str(MakeString(u, n));
	free(u);
	return str;
}

static char*
UTF8Of(RefArg str)
{
	const UniChar* u = (const UniChar*) BinaryData(str);
	long length = (Length(str) - 2) / (long) sizeof(UniChar);
	char* out = (char*) malloc((size_t) length * 3 + 1);
	size_t n = 0;
	for (long i = 0; i < length && u[i] != 0; i++)
	{
		unsigned c = u[i];
		if (c < 0x80)
			out[n++] = (char) c;
		else if (c < 0x800)
		{
			out[n++] = (char) (0xC0 | (c >> 6));
			out[n++] = (char) (0x80 | (c & 0x3F));
		}
		else
		{
			out[n++] = (char) (0xE0 | (c >> 12));
			out[n++] = (char) (0x80 | ((c >> 6) & 0x3F));
			out[n++] = (char) (0x80 | (c & 0x3F));
		}
	}
	out[n] = 0;
	return out;
}

static Ref
ErrorFrame(const char* what)
{
	fprintf(stderr, "[web] %s\n", what);
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "error")), RefVar(MakeString(what)));
	return frame;
}

// the request begun for job->url: ==> false (job->error says) when it cannot be
static bool
Begin(WebJob* job)
{
	HostWebURL u;
	if (!HostWebParseURL(job->url, &u))
	{
		snprintf(job->error, sizeof(job->error), "not a web address: %.200s", job->url);
		return false;
	}
	job->socket.Close();
	HostIPPURI uri;
	memset(&uri, 0, sizeof(uri));
	snprintf(uri.fHost, sizeof(uri.fHost), "%s", u.host);
	uri.fPort = (unsigned short) u.port;
	snprintf(uri.fPath, sizeof(uri.fPath), "/");
	uri.fTLS = u.tls;
	if (job->socket.Connect(&uri) != kHostSocketOK)
	{
		snprintf(job->error, sizeof(job->error), "%s could not be reached", u.host);
		return false;
	}
	char hostHeader[300];
	if (u.port != (u.tls ? 443 : 80))
		snprintf(hostHeader, sizeof(hostHeader), "%s:%d", u.host, u.port);
	else
		snprintf(hostHeader, sizeof(hostHeader), "%s", u.host);
	free(job->out);
	job->out = (char*) malloc(strlen(u.path) + strlen(hostHeader) + 400);
	job->outSize = (size_t) sprintf(job->out,
		"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (compatible; Newton; newton-re)\r\n"
		"Accept: text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.5\r\nAccept-Encoding: identity\r\n"
		"Connection: close\r\n\r\n", u.path, hostHeader);
	job->outDone = 0;
	job->inSize = 0;
	job->connected = false;
	job->lastProgress = time(NULL);
	fprintf(stderr, "[web] GET %s\n", job->url);
	return true;
}

static void
EndJob(int index)
{
	WebJob* job = gJobs[index];
	if (job == NULL)
		return;
	job->socket.Close();
	free(job->out);
	free(job->in);
	delete job;
	gJobs[index] = NULL;
}

static Ref
FHostWebAddressOf(RefArg /*rcvr*/, RefArg typed)
{
	if (!IsString(typed))
		return NILREF;
	static const char* sEngine = NULL;
	if (sEngine == NULL)
	{
		sEngine = getenv("NEWTON_WEB_SEARCH");
		if (sEngine == NULL || sEngine[0] == 0)
			sEngine = "https://html.duckduckgo.com/html/?q=%s";
	}
	char* t = UTF8Of(typed);
	char* url = HostWebAddress(t, sEngine);
	free(t);
	RefVar result(StringOfUTF8(url));
	free(url);
	return result;
}

static Ref
FHostWebFetch(RefArg /*rcvr*/, RefArg url)
{
	if (!IsString(url))
		return ErrorFrame("no address");
	if (HostSocketsInit() != kHostSocketOK)
		return ErrorFrame("the network cannot be used");
	int index = -1;
	for (int i = 0; i < kMostJobs && index < 0; i++)
		if (gJobs[i] == NULL)
			index = i;
	if (index < 0)
	{
		// (the oldest given up: a browser fetches one page at a time)
		EndJob(0);
		index = 0;
	}
	WebJob* job = new WebJob;
	char* u = UTF8Of(url);
	snprintf(job->url, sizeof(job->url), "%s", u);
	free(u);
	job->redirects = 0;
	job->out = NULL;
	job->inRoom = 65536;
	job->in = (char*) malloc(job->inRoom + 1);
	job->inSize = 0;
	job->error[0] = 0;
	gJobs[index] = job;
	if (!Begin(job))
	{
		RefVar e(ErrorFrame(job->error));
		EndJob(index);
		return e;
	}
	return MAKEINT(index);
}

// a download's file in Downloads: its name from the URL, a number added when taken
static Ref
SaveDownload(const char* url, const char* body, size_t size)
{
	HostWebURL u;
	HostWebParseURL(url, &u);
	char name[200] = "";
	const char* path = u.path;
	const char* q = strchr(path, '?');
	size_t pathLength = q != NULL ? (size_t) (q - path) : strlen(path);
	const char* start = path;
	for (size_t i = 0; i < pathLength; i++)
		if (path[i] == '/')
			start = path + i + 1;
	size_t n = 0;
	for (const char* p = start; p < path + pathLength && n + 1 < sizeof(name); p++)
	{
		char c = *p;
		if (isalnum((unsigned char) c) || c == '.' || c == '-' || c == '_')
			name[n++] = c;
		else if (c == '%' || c == ' ')
			name[n++] = '_';
	}
	name[n] = 0;
	if (n == 0 || name[0] == '.')
		snprintf(name, sizeof(name), "download");
	char relative[300];
	char* full = NULL;
	for (int k = 1; k < 1000; k++)
	{
		if (k == 1)
			snprintf(relative, sizeof(relative), "Downloads/%s", name);
		else
		{
			const char* dot = strrchr(name, '.');
			if (dot != NULL)
				snprintf(relative, sizeof(relative), "Downloads/%.*s %d%s", (int) (dot - name), name, k, dot);
			else
				snprintf(relative, sizeof(relative), "Downloads/%s %d", name, k);
		}
		free(full);
		full = HostFilesFullPath(RefVar(MakeString(relative)));
		if (full == NULL || HostFilesKind(full) == 0)
			break;
	}
	if (full == NULL)
		return ErrorFrame("the download has nowhere to go");
	// (the folder made first: Downloads may not be there yet)
	char* folder = (char*) malloc(strlen(full) + 1);
	strcpy(folder, full);
	char* slash = strrchr(folder, '/');
	if (slash != NULL)
	{
		*slash = 0;
		HostFilesMakeDirs(folder);
	}
	free(folder);
	int err = HostFilesWrite(full, body, (long) size);
	fprintf(stderr, "[web] %s %s (%ld bytes)\n", err == 0 ? "downloaded" : "could not save", full, (long) size);
	free(full);
	if (err != 0)
		return ErrorFrame("the download could not be saved");
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "download")), RefVar(MakeString(relative)));
	SetFrameSlot(frame, RefVar(Intern((char*) "size")), RefVar(MAKEINT((long) size)));
	return frame;
}

static Ref
PageFrame(const HostWebPage* page, const char* url)
{
	static const char* const kKinds[] = { "para", "heading", "item", "quote", "pre" };
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "title")), RefVar(StringOfUTF8(page->title)));
	SetFrameSlot(frame, RefVar(Intern((char*) "url")), RefVar(MakeString(url)));
	RefVar blocks(MakeArray(0));
	RefVar kindSym(Intern((char*) "kind")), levelSym(Intern((char*) "level")), textSym(Intern((char*) "text"));
	for (int i = 0; i < page->blockCount; i++)
	{
		RefVar b(AllocateFrame());
		SetFrameSlot(b, kindSym, RefVar(Intern((char*) kKinds[page->blocks[i].kind])));
		SetFrameSlot(b, levelSym, RefVar(MAKEINT(page->blocks[i].level)));
		SetFrameSlot(b, textSym, RefVar(StringOfUTF8(page->blocks[i].text)));
		AddArraySlot(blocks, b);
	}
	SetFrameSlot(frame, RefVar(Intern((char*) "blocks")), blocks);
	RefVar links(MakeArray(0));
	for (int i = 0; i < page->linkCount; i++)
		AddArraySlot(links, RefVar(MakeString(page->links[i])));
	SetFrameSlot(frame, RefVar(Intern((char*) "links")), links);
	RefVar result(AllocateFrame());
	SetFrameSlot(result, RefVar(Intern((char*) "page")), frame);
	return result;
}

static Ref
FHostWebPoll(RefArg /*rcvr*/, RefArg jobRef)
{
	if (!ISINT(jobRef) || RINT(jobRef) < 0 || RINT(jobRef) >= kMostJobs || gJobs[RINT(jobRef)] == NULL)
		return ErrorFrame("no such fetch");
	int index = (int) RINT(jobRef);
	WebJob* job = gJobs[index];
	if (time(NULL) - job->lastProgress > kWebTimeout)
	{
		RefVar e(ErrorFrame("the page did not come in time"));
		EndJob(index);
		return e;
	}
	if (!job->connected)
	{
		int err = job->socket.Connected();
		if (err == kHostSocketWouldBlock)
			return Intern((char*) "busy");
		if (err != kHostSocketOK)
		{
			RefVar e(ErrorFrame("the site could not be reached"));
			EndJob(index);
			return e;
		}
		if (job->socket.fTLS && !job->socket.fSystemTrusts && getenv("NEWTON_WEB_TRUST_ANY") == NULL)
		{
			RefVar e(ErrorFrame("the site's certificate is not trusted"));
			EndJob(index);
			return e;
		}
		job->connected = true;
	}
	while (job->outDone < job->outSize)
	{
		size_t count = 0;
		int err = job->socket.Send(job->out + job->outDone, job->outSize - job->outDone, &count);
		if (err == kHostSocketWouldBlock)
			return Intern((char*) "busy");
		if (err != kHostSocketOK)
		{
			RefVar e(ErrorFrame("the request could not be sent"));
			EndJob(index);
			return e;
		}
		job->outDone += count;
	}
	if (job->socket.Flush() == kHostSocketWouldBlock)
		return Intern((char*) "busy");
	for (;;)
	{
		if (job->inSize + 16384 > job->inRoom)
		{
			if (job->inRoom >= kMostBytes)
			{
				RefVar e(ErrorFrame("the page is too large"));
				EndJob(index);
				return e;
			}
			job->inRoom *= 2;
			job->in = (char*) realloc(job->in, job->inRoom + 1);
		}
		size_t count = 0;
		int err = job->socket.Receive(job->in + job->inSize, job->inRoom - job->inSize, &count);
		if (err == kHostSocketWouldBlock)
			return Intern((char*) "busy");
		if (err == kHostSocketClosed || (err == kHostSocketOK && count == 0))
			break;
		if (err != kHostSocketOK)
		{
			RefVar e(ErrorFrame("the page could not be read"));
			EndJob(index);
			return e;
		}
		job->inSize += count;
		job->lastProgress = time(NULL);
	}
	job->in[job->inSize] = 0;
	HostWebResponse r;
	if (!HostWebParseResponse(job->in, job->inSize, &r))
	{
		RefVar e(ErrorFrame("the answer was not HTTP"));
		EndJob(index);
		return e;
	}
	fprintf(stderr, "[web] %d %s (%ld bytes)\n", r.status, r.contentType, (long) r.bodySize);
	// a redirect: the next address, on the same job
	if ((r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308) && r.location[0] != 0)
	{
		char* next = HostWebResolve(job->url, r.location);
		HostWebFreeResponse(&r);
		if (next == NULL || ++job->redirects > kMostRedirects)
		{
			free(next);
			RefVar e(ErrorFrame("too many redirects"));
			EndJob(index);
			return e;
		}
		snprintf(job->url, sizeof(job->url), "%s", next);
		free(next);
		if (!Begin(job))
		{
			RefVar e(ErrorFrame(job->error));
			EndJob(index);
			return e;
		}
		return Intern((char*) "busy");
	}
	RefVar result;
	if (r.status != 200)
	{
		char what[120];
		snprintf(what, sizeof(what), "the site answered %d", r.status);
		result = ErrorFrame(what);
	}
	else if (strcmp(r.contentType, "text/html") == 0 || strcmp(r.contentType, "application/xhtml+xml") == 0
			 || strcmp(r.contentType, "text/plain") == 0 || r.contentType[0] == 0)
	{
		HostWebPage page;
		HostWebMakePage(r.body, r.bodySize, r.charset, strcmp(r.contentType, "text/plain") == 0, job->url, &page);
		result = PageFrame(&page, job->url);
		HostWebFreePage(&page);
	}
	else
	{
		result = SaveDownload(job->url, r.body, r.bodySize);
		if (IsFrame(result) && FrameHasSlot(result, RefVar(Intern((char*) "download"))))
		{
			SetFrameSlot(result, RefVar(Intern((char*) "type")), RefVar(MakeString(r.contentType)));
			SetFrameSlot(result, RefVar(Intern((char*) "url")), RefVar(MakeString(job->url)));
		}
	}
	HostWebFreeResponse(&r);
	EndJob(index);
	return result;
}


void
HostInstallWeb(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostWebAddressOf")), RefVar(MakeCFunction((void*) FHostWebAddressOf, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostWebFetch")), RefVar(MakeCFunction((void*) FHostWebFetch, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostWebPoll")), RefVar(MakeCFunction((void*) FHostWebPoll, 1, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostWebSource))));
		RefVar web(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostWeb:host")), web);
		NSSend(web, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Web application did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostWebFeatureChanged(void)
{
	RefVar web(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostWeb:host"))));
	if (!IsFrame(web))
		return;
	newton_try
	{
		NSSend(web, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Web application: %s\n", CurrentException()->name);
	}
	end_try;
}
