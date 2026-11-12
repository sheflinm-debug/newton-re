/*
	File:		host/HostMailEngine.cpp

	Contains:	SMTP and IMAP, polled.  See HostMailEngine.h.  Written for
				this project from RFC 5321 (SMTP), RFC 4954 and 4616 (AUTH
				PLAIN) and RFC 3501 (IMAP4rev1).
*/

#include "HostMailEngine.h"
#include "HostMailFormat.h"
#include "print/host/HostIPPSocket.h"
#include "HostSockets.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const long kTimeout = 60;		// seconds without progress

enum HostMailState
{
	kConnecting,
	kWaiting,						// for the server's answer to what was sent
	kDone,
	kFailed
};

struct HostMailJob
{
	bool			imap;
	THostIPPSocket	socket;
	HostMailState	state;
	int				step;				// where in its protocol the job is
	time_t			lastProgress;
	char			error[256];
	// what is to be sent, and has been
	char*			out;
	size_t			outSize;
	size_t			outDone;
	// what has come
	char*			in;
	size_t			inSize;
	size_t			inRoom;
	// SMTP
	char*			user;
	char*			password;
	char*			from;
	char**			to;
	int				toCount;
	int				toDone;
	char*			data;
	// IMAP
	int				tag;
	long			uids[kHostMailMostFetched];
	int				uidCount;
	int				uidDone;
	char*			fetched[kHostMailMostFetched];
	size_t			fetchedSize[kHostMailMostFetched];
	int				fetchedCount;
};

namespace {

char*
Copy(const char* s)
{
	char* c = (char*) malloc(strlen(s) + 1);
	strcpy(c, s);
	return c;
}

void
Fail(HostMailJob* job, const char* what)
{
	if (job->state != kFailed)
	{
		snprintf(job->error, sizeof(job->error), "%s", what);
		job->state = kFailed;
		fprintf(stderr, "[mail] %s\n", what);
	}
}

// something to send next (the answer to it awaited)
void
Say(HostMailJob* job, const char* text, size_t size)
{
	free(job->out);
	job->out = (char*) malloc(size + 1);
	memcpy(job->out, text, size);
	job->out[size] = 0;
	job->outSize = size;
	job->outDone = 0;
	job->inSize = 0;
	job->state = kWaiting;
}

void
SayLine(HostMailJob* job, const char* line)
{
	size_t n = strlen(line);
	char* t = (char*) malloc(n + 3);
	memcpy(t, line, n);
	memcpy(t + n, "\r\n", 3);
	Say(job, t, n + 2);
	free(t);
}

// IMAP: a tagged command
void
Command(HostMailJob* job, const char* text)
{
	char line[1200];
	job->tag++;
	snprintf(line, sizeof(line), "a%d %s", job->tag, text);
	SayLine(job, line);
}

// an IMAP string, quoted
void
Quote(char* out, size_t size, const char* s)
{
	size_t o = 0;
	if (o < size - 1) out[o++] = '"';
	for (; *s && o < size - 3; s++)
	{
		if (*s == '"' || *s == '\\')
			out[o++] = '\\';
		out[o++] = *s;
	}
	out[o++] = '"';
	out[o] = 0;
}

// SMTP: a whole reply has come ("ddd text" the last line): ==> its code,
// 0 not yet
int
SMTPReply(HostMailJob* job)
{
	const char* p = job->in;
	const char* end = job->in + job->inSize;
	while (p < end)
	{
		const char* lineEnd = (const char*) memchr(p, '\n', (size_t) (end - p));
		if (lineEnd == NULL)
			return 0;
		if (lineEnd - p >= 3 && isdigit((unsigned char) p[0]) && (lineEnd - p == 3 || p[3] == ' ' || p[3] == '\r'))
			return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
		p = lineEnd + 1;
	}
	return 0;
}

// IMAP: the tagged answer to the last command has come (literals {n}
// skipped as bytes): ==> 1 OK, -1 NO or BAD, 0 not yet; `untagged` the
// end of the untagged lines before it
int
IMAPReply(HostMailJob* job, bool greeting)
{
	const char* p = job->in;
	const char* end = job->in + job->inSize;
	char tag[16];
	snprintf(tag, sizeof(tag), "a%d ", job->tag);
	while (p < end)
	{
		const char* lineEnd = (const char*) memchr(p, '\n', (size_t) (end - p));
		if (lineEnd == NULL)
			return 0;
		if (greeting && p[0] == '*')
			return strncmp(p, "* OK", 4) == 0 || strncmp(p, "* PREAUTH", 9) == 0 ? 1 : -1;
		if (!greeting && strncmp(p, tag, strlen(tag)) == 0)
			return strncmp(p + strlen(tag), "OK", 2) == 0 ? 1 : -1;
		// a literal at the line's end: its bytes skipped
		const char* e = lineEnd;
		if (e > p && e[-1] == '\r')
			e--;
		if (e > p && e[-1] == '}')
		{
			const char* open = e - 1;
			while (open > p && *open != '{')
				open--;
			if (*open == '{')
			{
				long n = strtol(open + 1, NULL, 10);
				if (lineEnd + 1 + n > end)
					return 0;
				p = lineEnd + 1 + n;
				continue;
			}
		}
		p = lineEnd + 1;
	}
	return 0;
}

// the text of the tagged line (for an error)
void
IMAPProblem(HostMailJob* job, const char* doing)
{
	char tag[16];
	snprintf(tag, sizeof(tag), "a%d ", job->tag);
	const char* line = job->in;
	for (const char* p = job->in; p < job->in + job->inSize; p++)
		if ((p == job->in || p[-1] == '\n') && strncmp(p, tag, strlen(tag)) == 0)
			line = p;
	char what[256];
	size_t n = 0;
	while (line[n] && line[n] != '\r' && line[n] != '\n' && n < 150)
		n++;
	snprintf(what, sizeof(what), "the mail server refused %s: %.*s", doing, (int) n, line);
	Fail(job, what);
}

void
SMTPProblem(HostMailJob* job, const char* doing)
{
	// the last line of the reply
	const char* last = job->in;
	for (const char* p = job->in; p + 1 < job->in + job->inSize; p++)
		if (*p == '\n' && p[1] != 0)
			last = p + 1;
	size_t n = 0;
	while (last[n] && last[n] != '\r' && last[n] != '\n' && n < 150)
		n++;
	char what[256];
	snprintf(what, sizeof(what), "the mail server refused %s: %.*s", doing, (int) n, last);
	Fail(job, what);
}

// SMTP: the answer `code` to the step before, and the next thing said
void
SMTPNext(HostMailJob* job, int code)
{
	switch (job->step)
	{
		case 0:		// the greeting
			if (code != 220) return SMTPProblem(job, "the connection");
			SayLine(job, "EHLO newton");
			job->step = 1;
			return;
		case 1:		// EHLO
			if (code != 250) return SMTPProblem(job, "EHLO");
			if (job->user[0] != 0)
			{
				// AUTH PLAIN: "\0user\0password" in base64
				size_t ul = strlen(job->user), pl = strlen(job->password);
				unsigned char* plain = (unsigned char*) malloc(ul + pl + 2);
				plain[0] = 0;
				memcpy(plain + 1, job->user, ul);
				plain[1 + ul] = 0;
				memcpy(plain + 2 + ul, job->password, pl);
				char* b = HostMailBase64Encode(plain, ul + pl + 2);
				free(plain);
				char* line = (char*) malloc(strlen(b) + 16);
				sprintf(line, "AUTH PLAIN %s", b);
				SayLine(job, line);
				free(line);
				free(b);
				job->step = 2;
				return;
			}
			job->step = 2;
			code = 235;
			// fall through: no login
		case 2:		// AUTH
		{
			if (code != 235) return SMTPProblem(job, "the login");
			char line[600];
			snprintf(line, sizeof(line), "MAIL FROM:<%s>", job->from);
			SayLine(job, line);
			job->step = 3;
			job->toDone = 0;
			return;
		}
		case 3:		// MAIL FROM, then each RCPT TO
			if (code != 250 && code != 251)
				return SMTPProblem(job, job->toDone == 0 ? "the sender" : "a recipient");
			if (job->toDone < job->toCount)
			{
				char line[600];
				snprintf(line, sizeof(line), "RCPT TO:<%s>", job->to[job->toDone++]);
				SayLine(job, line);
				return;
			}
			SayLine(job, "DATA");
			job->step = 4;
			return;
		case 4:		// DATA
			if (code != 354) return SMTPProblem(job, "the message");
			Say(job, job->data, strlen(job->data));
			job->step = 5;
			return;
		case 5:		// the message
			if (code != 250) return SMTPProblem(job, "the message");
			SayLine(job, "QUIT");
			job->step = 6;
			return;
		default:	// QUIT's answer, whatever it is
			job->state = kDone;
			job->socket.Close();
			return;
	}
}

// IMAP: the answer (ok) to the step before, and the next thing said
void
IMAPNext(HostMailJob* job, int ok)
{
	char line[1100];
	switch (job->step)
	{
		case 0:		// the greeting
		{
			if (ok < 0) return Fail(job, "the mail server would not talk");
			char user[500], password[500];
			Quote(user, sizeof(user), job->user);
			Quote(password, sizeof(password), job->password);
			snprintf(line, sizeof(line), "LOGIN %s %s", user, password);
			Command(job, line);
			job->step = 1;
			return;
		}
		case 1:		// LOGIN
			if (ok < 0) return IMAPProblem(job, "the login");
			Command(job, "SELECT INBOX");
			job->step = 2;
			return;
		case 2:		// SELECT
			if (ok < 0) return IMAPProblem(job, "the In Box");
			Command(job, "UID SEARCH UNSEEN");
			job->step = 3;
			return;
		case 3:		// SEARCH: "* SEARCH 4 7 9"
		{
			if (ok < 0) return IMAPProblem(job, "the search");
			job->uidCount = 0;
			const char* s = strstr(job->in, "* SEARCH");
			if (s != NULL)
			{
				s += 8;
				while (*s && *s != '\r' && *s != '\n' && job->uidCount < kHostMailMostFetched)
				{
					while (*s == ' ')
						s++;
					if (!isdigit((unsigned char) *s))
						break;
					job->uids[job->uidCount++] = strtol(s, (char**) &s, 10);
				}
			}
			job->uidDone = 0;
		}
			// fall through
		case 4:		// FETCH's answer (the message in its literal) and the next
			if (job->step == 4)
			{
				if (ok < 0) return IMAPProblem(job, "a message");
				const char* open = NULL;
				for (const char* p = job->in; p < job->in + job->inSize; p++)
					if (*p == '{' && open == NULL)
						open = p;
				const char* close = open != NULL ? strstr(open, "}\r\n") : NULL;
				if (close != NULL && job->fetchedCount < kHostMailMostFetched)
				{
					long n = strtol(open + 1, NULL, 10);
					const char* bytes = close + 3;
					if (bytes + n <= job->in + job->inSize)
					{
						char* m = (char*) malloc((size_t) n + 1);
						memcpy(m, bytes, (size_t) n);
						m[n] = 0;
						job->fetched[job->fetchedCount] = m;
						job->fetchedSize[job->fetchedCount] = (size_t) n;
						job->fetchedCount++;
					}
				}
				snprintf(line, sizeof(line), "UID STORE %ld +FLAGS (\\Seen)", job->uids[job->uidDone - 1]);
				Command(job, line);
				job->step = 5;
				return;
			}
			// (from SEARCH)
			if (job->uidDone < job->uidCount)
			{
				snprintf(line, sizeof(line), "UID FETCH %ld (BODY.PEEK[])", job->uids[job->uidDone++]);
				Command(job, line);
				job->step = 4;
				return;
			}
			Command(job, "LOGOUT");
			job->step = 6;
			return;
		case 5:		// STORE: the next message, or done
			if (job->uidDone < job->uidCount)
			{
				snprintf(line, sizeof(line), "UID FETCH %ld (BODY.PEEK[])", job->uids[job->uidDone++]);
				Command(job, line);
				job->step = 4;
				return;
			}
			Command(job, "LOGOUT");
			job->step = 6;
			return;
		default:	// LOGOUT
			job->state = kDone;
			job->socket.Close();
			return;
	}
}

HostMailJob*
NewJob(const char* url, bool imap, const char* user, const char* password)
{
	char host[256];
	int port;
	bool tls;
	if (!HostMailParseServer(url, imap, host, sizeof(host), &port, &tls))
		return NULL;
	if (HostSocketsInit() != kHostSocketOK)
		return NULL;
	HostMailJob* job = new HostMailJob;
	job->imap = imap;
	job->state = kConnecting;
	job->step = 0;
	job->lastProgress = time(NULL);
	job->error[0] = 0;
	job->out = NULL;
	job->outSize = job->outDone = 0;
	job->inRoom = 8192;
	job->in = (char*) malloc(job->inRoom + 1);
	job->inSize = 0;
	job->in[0] = 0;
	job->user = Copy(user != NULL ? user : "");
	job->password = Copy(password != NULL ? password : "");
	job->from = NULL;
	job->to = NULL;
	job->toCount = job->toDone = 0;
	job->data = NULL;
	job->tag = 0;
	job->uidCount = job->uidDone = 0;
	job->fetchedCount = 0;
	HostIPPURI uri;
	memset(&uri, 0, sizeof(uri));
	snprintf(uri.fHost, sizeof(uri.fHost), "%s", host);
	uri.fPort = (unsigned short) port;
	strcpy(uri.fPath, "/");
	uri.fTLS = tls;
	int err = job->socket.Connect(&uri);
	if (err != kHostSocketOK)
	{
		char what[300];
		snprintf(what, sizeof(what), "%s could not be reached", host);
		Fail(job, what);
	}
	return job;
}

} // namespace


HostMailJob*
HostMailStartSend(const char* serverURL, const char* user, const char* password,
				  const char* from, const char* const* to, int toCount, const char* data)
{
	HostMailJob* job = NewJob(serverURL, false, user, password);
	if (job == NULL)
		return NULL;
	job->from = Copy(from);
	job->to = (char**) malloc(sizeof(char*) * (size_t) (toCount > 0 ? toCount : 1));
	for (int i = 0; i < toCount; i++)
		job->to[i] = Copy(to[i]);
	job->toCount = toCount;
	job->data = Copy(data);
	return job;
}

HostMailJob*
HostMailStartFetch(const char* serverURL, const char* user, const char* password)
{
	return NewJob(serverURL, true, user, password);
}

int
HostMailStep(HostMailJob* job)
{
	for (int rounds = 0; rounds < 64; rounds++)
	{
		if (job->state == kDone)
			return 1;
		if (job->state == kFailed)
			return -1;
		if (time(NULL) - job->lastProgress > kTimeout)
		{
			Fail(job, "the mail server did not answer in time");
			return -1;
		}
		if (job->state == kConnecting)
		{
			int err = job->socket.Connected();
			if (err == kHostSocketWouldBlock)
				return 0;
			if (err != kHostSocketOK)
			{
				Fail(job, "the mail server could not be reached");
				continue;
			}
			if (job->socket.fTLS && !job->socket.fSystemTrusts && getenv("NEWTON_MAIL_TRUST_ANY") == NULL)
			{
				Fail(job, "the mail server's certificate is not trusted");
				continue;
			}
			job->state = kWaiting;		// (the greeting first)
			job->lastProgress = time(NULL);
			continue;
		}
		// what is to be said, sent
		while (job->outDone < job->outSize)
		{
			size_t count = 0;
			int err = job->socket.Send(job->out + job->outDone, job->outSize - job->outDone, &count);
			if (err == kHostSocketWouldBlock)
				return 0;
			if (err != kHostSocketOK)
			{
				Fail(job, "the connection to the mail server was lost");
				return -1;
			}
			job->outDone += count;
			job->lastProgress = time(NULL);
		}
		int flushed = job->socket.Flush();
		if (flushed == kHostSocketWouldBlock)
			return 0;
		// the answer
		if (job->inSize + 4096 > job->inRoom)
		{
			job->inRoom = job->inRoom * 2 + 4096;
			job->in = (char*) realloc(job->in, job->inRoom + 1);
		}
		size_t count = 0;
		int err = job->socket.Receive(job->in + job->inSize, job->inRoom - job->inSize, &count);
		if (err == kHostSocketOK && count > 0)
		{
			job->inSize += count;
			job->in[job->inSize] = 0;
			job->lastProgress = time(NULL);
		}
		else if (err == kHostSocketClosed)
		{
			// (after QUIT or LOGOUT that is the end)
			if ((!job->imap && job->step == 6) || (job->imap && job->step == 6))
			{
				job->state = kDone;
				continue;
			}
			Fail(job, "the mail server closed the connection");
			continue;
		}
		else if (err != kHostSocketOK && err != kHostSocketWouldBlock)
		{
			Fail(job, "the connection to the mail server was lost");
			continue;
		}
		if (job->imap)
		{
			int ok = IMAPReply(job, job->step == 0);
			if (ok == 0)
				return 0;
			IMAPNext(job, ok);
		}
		else
		{
			int code = SMTPReply(job);
			if (code == 0)
				return 0;
			SMTPNext(job, code);
		}
	}
	return job->state == kDone ? 1 : job->state == kFailed ? -1 : 0;
}

const char*
HostMailError(HostMailJob* job)
{
	return job->error;
}

int
HostMailFetchedCount(HostMailJob* job)
{
	return job->fetchedCount;
}

const char*
HostMailFetched(HostMailJob* job, int index, size_t* size)
{
	*size = job->fetchedSize[index];
	return job->fetched[index];
}

bool
HostMailIsFetch(HostMailJob* job)
{
	return job->imap;
}

void
HostMailEnd(HostMailJob* job)
{
	if (job == NULL)
		return;
	job->socket.Close();
	free(job->out);
	free(job->in);
	free(job->user);
	free(job->password);
	free(job->from);
	for (int i = 0; i < job->toCount; i++)
		free(job->to[i]);
	free(job->to);
	free(job->data);
	for (int i = 0; i < job->fetchedCount; i++)
		free(job->fetched[i]);
	delete job;
}
