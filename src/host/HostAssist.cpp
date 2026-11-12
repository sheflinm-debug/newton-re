/*
	File:		host/HostAssist.cpp

	Contains:	DEVIATION (host addition; docs/modern/assistant.md): the
				Assistant's engines' natives and HostAssist.ns started.  See
				HostAssist.h.
*/

#include "HostAssist.h"
#include "host/Features.h"
#include "print/host/HostIPPSocket.h"
#include "HostSockets.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const char gHostAssistSource[];		// HostAssist.ns (HostAssistSource.cpp, generated)

/*------------------------------------------------------------------------------
	The protocol, plain C++
------------------------------------------------------------------------------*/

size_t
HostAssistJSONString(const char* text, char* out, size_t size)
{
	size_t n = 0;
	auto put = [&](char c) { if (n + 1 < size) out[n] = c; n++; };
	put('"');
	for (const unsigned char* p = (const unsigned char*) text; *p; p++)
	{
		unsigned char c = *p;
		if (c == '"' || c == '\\')
		{
			put('\\');
			put((char) c);
		}
		else if (c == '\n')
		{
			put('\\');
			put('n');
		}
		else if (c == '\r')
		{
			put('\\');
			put('r');
		}
		else if (c == '\t')
		{
			put('\\');
			put('t');
		}
		else if (c < 0x20)
		{
			char hex[8];
			snprintf(hex, sizeof(hex), "\\u%04x", c);
			for (char* h = hex; *h; h++)
				put(*h);
		}
		else
			put((char) c);			// (UTF-8 passes as it is)
	}
	put('"');
	if (n + 1 > size)
		return 0;
	out[n] = 0;
	return n;
}


// a code point as UTF-8
static size_t
PutUTF8(unsigned long c, char* out)
{
	if (c < 0x80)
	{
		out[0] = (char) c;
		return 1;
	}
	if (c < 0x800)
	{
		out[0] = (char) (0xC0 | (c >> 6));
		out[1] = (char) (0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000)
	{
		out[0] = (char) (0xE0 | (c >> 12));
		out[1] = (char) (0x80 | ((c >> 6) & 0x3F));
		out[2] = (char) (0x80 | (c & 0x3F));
		return 3;
	}
	out[0] = (char) (0xF0 | (c >> 18));
	out[1] = (char) (0x80 | ((c >> 12) & 0x3F));
	out[2] = (char) (0x80 | ((c >> 6) & 0x3F));
	out[3] = (char) (0x80 | (c & 0x3F));
	return 4;
}


static int
HexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}


bool
HostAssistReplyContent(const char* json, char* out, size_t size)
{
	const char* p = strstr(json, "\"choices\"");
	if (p == NULL)
		return false;
	p = strstr(p, "\"content\"");
	if (p == NULL)
		return false;
	p += 9;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p != ':')
		return false;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p != '"')
		return false;
	p++;
	size_t n = 0;
	while (*p && *p != '"')
	{
		char buf[4];
		size_t len = 1;
		buf[0] = *p;
		if (*p == '\\')
		{
			p++;
			switch (*p)
			{
			case 'n': buf[0] = '\n'; break;
			case 'r': buf[0] = '\r'; break;
			case 't': buf[0] = '\t'; break;
			case 'b': buf[0] = '\b'; break;
			case 'f': buf[0] = '\f'; break;
			case 'u':
				{
					unsigned long c = 0;
					for (int i = 1; i <= 4; i++)
					{
						int h = HexValue(p[i]);
						if (h < 0)
							return false;
						c = (c << 4) | (unsigned long) h;
					}
					p += 4;
					// a surrogate pair joined
					if (c >= 0xD800 && c < 0xDC00 && p[1] == '\\' && p[2] == 'u')
					{
						unsigned long low = 0;
						bool ok = true;
						for (int i = 3; i <= 6; i++)
						{
							int h = HexValue(p[i]);
							if (h < 0)
								ok = false;
							else
								low = (low << 4) | (unsigned long) h;
						}
						if (ok && low >= 0xDC00 && low < 0xE000)
						{
							c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
							p += 6;
						}
					}
					len = PutUTF8(c, buf);
				}
				break;
			case 0:
				return false;
			default: buf[0] = *p; break;		// \" \\ \/
			}
		}
		if (n + len + 1 > size)
			return false;
		memcpy(out + n, buf, len);
		n += len;
		p++;
	}
	if (*p != '"')
		return false;
	out[n] = 0;
	return true;
}


int
HostAssistHTTPBody(char* response, size_t size, char** body, size_t* bodySize)
{
	if (size < 12 || strncmp(response, "HTTP/", 5) != 0)
		return -1;
	const char* space = (const char*) memchr(response, ' ', size);
	if (space == NULL)
		return -1;
	int status = atoi(space + 1);
	char* end = NULL;
	for (size_t i = 0; i + 3 < size; i++)
		if (response[i] == '\r' && response[i + 1] == '\n' && response[i + 2] == '\r' && response[i + 3] == '\n')
		{
			end = response + i + 4;
			break;
		}
	if (end == NULL)
		return -1;
	*body = end;
	*bodySize = size - (size_t) (end - response);
	// chunks joined, in place
	bool chunked = false;
	for (char* h = response; h + 26 <= end; h++)
		if (strncmp(h, "\r\nTransfer-Encoding:", 20) == 0 || strncmp(h, "\r\ntransfer-encoding:", 20) == 0)
		{
			char* v = h + 20;
			while (*v == ' ')
				v++;
			if (strncmp(v, "chunked", 7) == 0)
				chunked = true;
		}
	if (chunked)
	{
		char* in = end;
		char* limit = response + size;
		char* out = end;
		while (in < limit)
		{
			unsigned long count = strtoul(in, NULL, 16);
			char* line = (char*) memchr(in, '\n', (size_t) (limit - in));
			if (line == NULL || count == 0)
				break;
			in = line + 1;
			if (in + count > limit)
				count = (unsigned long) (limit - in);
			memmove(out, in, count);
			out += count;
			in += count + 2;		// (the chunk's CRLF)
		}
		*bodySize = (size_t) (out - end);
	}
	return status;
}


/*------------------------------------------------------------------------------
	The request
------------------------------------------------------------------------------*/

struct AssistRequest
{
	THostIPPSocket	socket;
	char*			out;			// the request, head and body
	size_t			outSize;
	size_t			outDone;
	char*			in;				// the response so far
	size_t			inSize;
	size_t			inCapacity;
	time_t			started;
	bool			connected;
};

static AssistRequest*	gRequest = NULL;
static const long		kAssistTimeout = 90;			// seconds

static void
EndRequest(void)
{
	if (gRequest == NULL)
		return;
	gRequest->socket.Close();
	free(gRequest->out);
	free(gRequest->in);
	delete gRequest;
	gRequest = NULL;
}


static const char*
Variable(const char* name, const char* otherwise)
{
	const char* v = getenv(name);
	return v != NULL && v[0] != 0 ? v : otherwise;
}

static const char*
AssistEngine(void)
{
	static const char* sEngine = Variable("NEWTON_ASSIST_ENGINE", "none");
	return sEngine;
}

static const char*
AssistURL(void)
{
	static const char* sURL = Variable("NEWTON_ASSIST_URL", "http://localhost:11434/v1/chat/completions");
	return sURL;
}

static const char*
AssistModel(void)
{
	static const char* sModel = Variable("NEWTON_ASSIST_MODEL", "llama3.2");
	return sModel;
}

static const char*
AssistKey(void)
{
	static const char* sKey = Variable("NEWTON_ASSIST_KEY", "");
	return sKey;
}


// a NewtonScript string as UTF-8 (malloc'd; the caller frees it)
static char*
UTF8Of(RefArg str)
{
	const UniChar* u = (const UniChar*) BinaryData(str);
	long length = (Length(str) - 2) / (long) sizeof(UniChar);
	char* out = (char*) malloc((size_t) length * 3 + 1);
	size_t n = 0;
	for (long i = 0; i < length && u[i] != 0; i++)
		n += PutUTF8(u[i], out + n);
	out[n] = 0;
	return out;
}


// UTF-8 as a NewtonScript string (characters past 0xFFFF as '?')
static Ref
StringOfUTF8(const char* text)
{
	size_t length = strlen(text);
	UniChar* u = (UniChar*) malloc((length + 1) * sizeof(UniChar));
	long n = 0;
	const unsigned char* p = (const unsigned char*) text;
	while (*p)
	{
		unsigned long c = *p++;
		int more = 0;
		if (c >= 0xF0) { c &= 0x07; more = 3; }
		else if (c >= 0xE0) { c &= 0x0F; more = 2; }
		else if (c >= 0xC0) { c &= 0x1F; more = 1; }
		while (more-- > 0 && (*p & 0xC0) == 0x80)
			c = (c << 6) | (*p++ & 0x3F);
		u[n++] = c > 0xFFFF ? (UniChar) '?' : (UniChar) c;
	}
	RefVar str(MakeString(u, n));
	free(u);
	return str;
}


// HostAssistConfig(): {engine, url, model}
static Ref
FHostAssistConfig(RefArg /*rcvr*/)
{
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RefVar(Intern((char*) "engine")), RefVar(Intern((char*) AssistEngine())));
	SetFrameSlot(config, RefVar(Intern((char*) "url")), RefVar(MakeString(AssistURL())));
	SetFrameSlot(config, RefVar(Intern((char*) "model")), RefVar(MakeString(AssistModel())));
	return config;
}


// HostAssistRequest(instructions, sentence): ==> true when it is begun
static Ref
FHostAssistRequest(RefArg /*rcvr*/, RefArg instructions, RefArg sentence)
{
	if (!IsString(instructions) || !IsString(sentence))
		return NILREF;
	EndRequest();
	HostIPPURI uri;
	if (!HostIPPParseURI(AssistURL(), &uri))
	{
		fprintf(stderr, "[assist] not an address: %s\n", AssistURL());
		return NILREF;
	}
	if (HostSocketsInit() != kHostSocketOK)
		return NILREF;
	char* system = UTF8Of(instructions);
	char* user = UTF8Of(sentence);
	size_t room = strlen(system) * 6 + strlen(user) * 6 + strlen(AssistModel()) * 6 + 256;
	char* body = (char*) malloc(room);
	char* model = (char*) malloc(strlen(AssistModel()) * 6 + 3);
	char* systemJSON = (char*) malloc(strlen(system) * 6 + 3);
	char* userJSON = (char*) malloc(strlen(user) * 6 + 3);
	HostAssistJSONString(AssistModel(), model, strlen(AssistModel()) * 6 + 3);
	HostAssistJSONString(system, systemJSON, strlen(system) * 6 + 3);
	HostAssistJSONString(user, userJSON, strlen(user) * 6 + 3);
	int bodySize = snprintf(body, room,
		"{\"model\": %s, \"stream\": false, \"temperature\": 0, \"messages\": ["
		"{\"role\": \"system\", \"content\": %s}, {\"role\": \"user\", \"content\": %s}]}",
		model, systemJSON, userJSON);
	free(model);
	free(systemJSON);
	free(userJSON);
	free(system);
	free(user);
	char auth[600] = "";
	if (AssistKey()[0] != 0)
		snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n", AssistKey());
	char head[1400];
	int headSize = snprintf(head, sizeof(head),
		"POST %s HTTP/1.0\r\nHost: %s\r\nContent-Type: application/json\r\nAccept: application/json\r\n"
		"%sContent-Length: %d\r\nConnection: close\r\n\r\n",
		uri.fPath, uri.fHost, auth, bodySize);
	gRequest = new AssistRequest;
	gRequest->outSize = (size_t) headSize + (size_t) bodySize;
	gRequest->out = (char*) malloc(gRequest->outSize);
	memcpy(gRequest->out, head, (size_t) headSize);
	memcpy(gRequest->out + headSize, body, (size_t) bodySize);
	free(body);
	gRequest->outDone = 0;
	gRequest->inCapacity = 16384;
	gRequest->in = (char*) malloc(gRequest->inCapacity + 1);
	gRequest->inSize = 0;
	gRequest->started = time(NULL);
	gRequest->connected = false;
	int err = gRequest->socket.Connect(&uri);
	if (err != kHostSocketOK)
	{
		fprintf(stderr, "[assist] %s: not reached (%d)\n", AssistURL(), err);
		EndRequest();
		return NILREF;
	}
	fprintf(stderr, "[assist] asking %s (%s)\n", AssistURL(), AssistModel());
	return TRUEREF;
}


static Ref
ErrorFrame(const char* what)
{
	fprintf(stderr, "[assist] %s\n", what);
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "error")), RefVar(MakeString(what)));
	EndRequest();
	return frame;
}


// HostAssistPoll(): nil while the answer is coming, its text, or {error}
static Ref
FHostAssistPoll(RefArg /*rcvr*/)
{
	if (gRequest == NULL)
		return ErrorFrame("nothing was asked");
	if (time(NULL) - gRequest->started > kAssistTimeout)
		return ErrorFrame("no answer in time");
	AssistRequest* r = gRequest;
	if (!r->connected)
	{
		int err = r->socket.Connected();
		if (err == kHostSocketWouldBlock)
			return NILREF;
		if (err != kHostSocketOK)
			return ErrorFrame("the model's server could not be reached");
		r->connected = true;
	}
	while (r->outDone < r->outSize)
	{
		size_t count = 0;
		int err = r->socket.Send(r->out + r->outDone, r->outSize - r->outDone, &count);
		if (err == kHostSocketWouldBlock)
			return NILREF;
		if (err != kHostSocketOK)
			return ErrorFrame("the request could not be sent");
		r->outDone += count;
	}
	int flushed = r->socket.Flush();
	if (flushed == kHostSocketWouldBlock)
		return NILREF;
	for (;;)
	{
		if (r->inSize == r->inCapacity)
		{
			r->inCapacity *= 2;
			r->in = (char*) realloc(r->in, r->inCapacity + 1);
		}
		size_t count = 0;
		int err = r->socket.Receive(r->in + r->inSize, r->inCapacity - r->inSize, &count);
		if (err == kHostSocketWouldBlock)
			return NILREF;
		if (err == kHostSocketClosed)
			break;
		if (err != kHostSocketOK)
			return ErrorFrame("the answer could not be read");
		r->inSize += count;
		if (count == 0)
			break;
	}
	r->in[r->inSize] = 0;
	char* body;
	size_t bodySize;
	int status = HostAssistHTTPBody(r->in, r->inSize, &body, &bodySize);
	if (status < 0)
		return ErrorFrame("the answer was not HTTP");
	body[bodySize] = 0;
	if (status != 200)
	{
		char what[200];
		snprintf(what, sizeof(what), "the model's server answered %d: %.120s", status, body);
		return ErrorFrame(what);
	}
	char* text = (char*) malloc(bodySize + 1);
	bool found = HostAssistReplyContent(body, text, bodySize + 1);
	if (!found)
	{
		free(text);
		return ErrorFrame("the answer had no reply in it");
	}
	fprintf(stderr, "[assist] the model answered: %s\n", text);
	RefVar reply(StringOfUTF8(text));
	free(text);
	EndRequest();
	return reply;
}


void
HostInstallAssist(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostAssistConfig")), RefVar(MakeCFunction((void*) FHostAssistConfig, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostAssistRequest")), RefVar(MakeCFunction((void*) FHostAssistRequest, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostAssistPoll")), RefVar(MakeCFunction((void*) FHostAssistPoll, 0, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostAssistSource))));
		RefVar assist(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostAssist:host")), assist);
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Assistant's engines did not start: %s\n", CurrentException()->name);
	}
	end_try;
}
