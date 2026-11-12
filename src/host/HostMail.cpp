/*
	File:		host/HostMail.cpp

	Contains:	DEVIATION (host addition; docs/modern/transports.md, Internet
				mail): the Mail transport (HostMail.ns, compiled in as a
				string) evaluated at boot, kept in the global HostMail:host
				and started - it registers itself only when the Host panel's
				"Internet mail" is on (host/Features.h's kFeatureMail,
				NEWTON_MAIL) - and the natives it uses, over
				HostMailEngine.h (SMTP, IMAP), HostMailFormat.h (messages)
				and HostMailKeys.h (passwords).

				  HostMailConfig() - the main account the host is set up
					  with: {id: "main", name, address, smtp, imap, user},
					  from NEWTON_MAIL_ADDRESS, _NAME, _SMTP, _IMAP and _USER
					  (the address when none); nil without an address.
					  Also pollSeconds (NEWTON_MAIL_POLL, 300) and keyStore.
				  HostMailSetPassword(id, password), HostMailHasPassword(id),
				  HostMailForgetPassword(id) - the key store.
				  HostMailSend(account, to, subject, text) - to an array of
					  addresses: ==> a job (an integer), or {error}.
				  HostMailFetch(account) - its new messages: ==> a job.
				  HostMailPoll(job) - 'busy; a send's true; a fetch's
					  [{fromName, fromAddress, subject, date, text}]; or
					  {error} - the job then ended.
*/

#include "HostMail.h"
#include "HostMailEngine.h"
#include "HostMailFormat.h"
#include "HostMailKeys.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const char gHostMailSource[];		// HostMail.ns (HostMailSource.cpp, generated)

static const int kMostJobs = 16;
static HostMailJob* gJobs[kMostJobs];


// a NewtonScript string as UTF-8 (malloc'd)
static char*
UTF8Of(RefArg str)
{
	if (!IsString(str))
	{
		char* empty = (char*) malloc(1);
		empty[0] = 0;
		return empty;
	}
	const UniChar* u = (const UniChar*) BinaryData(str);
	long length = (Length(str) - 2) / (long) sizeof(UniChar);
	char* out = (char*) malloc((size_t) length * 3 + 1);
	size_t n = 0;
	for (long i = 0; i < length && u[i] != 0; i++)
	{
		unsigned c = u[i];
		if (c == 0x0D)
			out[n++] = '\n';
		else if (c < 0x80)
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

// a slot of an account frame as UTF-8 (malloc'd; "" when it has none)
static char*
SlotUTF8(RefArg frame, const char* name)
{
	RefVar value(GetFrameSlot(frame, RefVar(Intern((char*) name))));
	return UTF8Of(value);
}

static const char*
Environment(const char* name, const char* otherwise)
{
	const char* value = getenv(name);
	return value != NULL && value[0] != 0 ? value : otherwise;
}

static Ref
ErrorFrame(const char* what)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RefVar(Intern((char*) "error")), RefVar(MakeString(what)));
	return frame;
}

static int
AddJob(HostMailJob* job)
{
	for (int i = 0; i < kMostJobs; i++)
		if (gJobs[i] == NULL)
		{
			gJobs[i] = job;
			return i;
		}
	HostMailEnd(job);
	return -1;
}


static Ref
FHostMailConfig(RefArg /*rcvr*/)
{
	const char* address = Environment("NEWTON_MAIL_ADDRESS", NULL);
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RefVar(Intern((char*) "pollSeconds")), RefVar(MAKEINT(atoi(Environment("NEWTON_MAIL_POLL", "300")))));
	SetFrameSlot(config, RefVar(Intern((char*) "keyStore")), RefVar(MakeString(HostMailKeyStoreName())));
	if (address != NULL)
	{
		RefVar account(AllocateFrame());
		SetFrameSlot(account, RefVar(Intern((char*) "id")), RefVar(MakeString("main")));
		SetFrameSlot(account, RefVar(Intern((char*) "name")), RefVar(StringOfUTF8(Environment("NEWTON_MAIL_NAME", ""))));
		SetFrameSlot(account, RefVar(Intern((char*) "address")), RefVar(StringOfUTF8(address)));
		SetFrameSlot(account, RefVar(Intern((char*) "smtp")), RefVar(StringOfUTF8(Environment("NEWTON_MAIL_SMTP", ""))));
		SetFrameSlot(account, RefVar(Intern((char*) "imap")), RefVar(StringOfUTF8(Environment("NEWTON_MAIL_IMAP", ""))));
		SetFrameSlot(account, RefVar(Intern((char*) "user")), RefVar(StringOfUTF8(Environment("NEWTON_MAIL_USER", address))));
		SetFrameSlot(config, RefVar(Intern((char*) "main")), account);
	}
	return config;
}


static Ref
FHostMailSetPassword(RefArg /*rcvr*/, RefArg id, RefArg password)
{
	if (!IsString(id) || !IsString(password))
		return NILREF;
	char* i = UTF8Of(id);
	char* p = UTF8Of(password);
	bool kept = HostMailKeySet(i, p);
	memset(p, 0, strlen(p));
	free(i);
	free(p);
	return MAKEBOOLEAN(kept);
}

static Ref
FHostMailHasPassword(RefArg /*rcvr*/, RefArg id)
{
	char* i = UTF8Of(id);
	char* p = HostMailKeyGet(i);
	free(i);
	bool has = p != NULL;
	if (p != NULL)
	{
		memset(p, 0, strlen(p));
		free(p);
	}
	return MAKEBOOLEAN(has);
}

static Ref
FHostMailForgetPassword(RefArg /*rcvr*/, RefArg id)
{
	char* i = UTF8Of(id);
	HostMailKeyForget(i);
	free(i);
	return TRUEREF;
}


static Ref
FHostMailSend(RefArg /*rcvr*/, RefArg account, RefArg to, RefArg subject, RefArg text)
{
	if (!IsFrame(account) || !IsArray(to) || Length(to) == 0)
		return ErrorFrame("no account, or nobody to send to");
	char* id = SlotUTF8(account, "id");
	char* server = SlotUTF8(account, "smtp");
	char* user = SlotUTF8(account, "user");
	char* address = SlotUTF8(account, "address");
	char* name = SlotUTF8(account, "name");
	char* password = HostMailKeyGet(id);
	char* subjectText = UTF8Of(subject);
	char* bodyText = UTF8Of(text);
	int count = (int) Length(to);
	char** recipients = (char**) malloc(sizeof(char*) * (size_t) count);
	size_t joinedSize = 1;
	for (int i = 0; i < count; i++)
	{
		recipients[i] = UTF8Of(RefVar(GetArraySlot(to, i)));
		joinedSize += strlen(recipients[i]) + 2;
	}
	char* joined = (char*) malloc(joinedSize);
	joined[0] = 0;
	for (int i = 0; i < count; i++)
	{
		if (i > 0)
			strcat(joined, ", ");
		strcat(joined, recipients[i]);
	}
	char date[64];
	HostMailDate(date, sizeof(date));
	char messageId[160];
	const char* at = strchr(address, '@');
	snprintf(messageId, sizeof(messageId), "<%ld.%d.newton@%s>", (long) time(NULL), rand(), at != NULL ? at + 1 : "newton");
	char* message = HostMailCompose(name, address, joined, subjectText, bodyText, date, messageId);
	char* data = HostMailDotStuff(message);
	Ref result;
	if (server[0] == 0)
		result = ErrorFrame("the account has no outgoing (SMTP) server");
	else if (user[0] != 0 && password == NULL)
		result = ErrorFrame("the account has no password");
	else
	{
		HostMailJob* job = HostMailStartSend(server, user, password != NULL ? password : "", address,
											 (const char* const*) recipients, count, data);
		int index = job != NULL ? AddJob(job) : -1;
		if (index >= 0)
			fprintf(stderr, "[mail] sending \"%s\" from %s to %s by %s\n", subjectText, address, joined, server);
		result = index >= 0 ? MAKEINT(index) : ErrorFrame(job == NULL ? "not a mail server's address" : "too many mail jobs");
	}
	for (int i = 0; i < count; i++)
		free(recipients[i]);
	free(recipients);
	free(joined);
	free(message);
	free(data);
	if (password != NULL)
	{
		memset(password, 0, strlen(password));
		free(password);
	}
	free(id);
	free(server);
	free(user);
	free(address);
	free(name);
	free(subjectText);
	free(bodyText);
	return result;
}


static Ref
FHostMailFetch(RefArg /*rcvr*/, RefArg account)
{
	if (!IsFrame(account))
		return ErrorFrame("no account");
	char* id = SlotUTF8(account, "id");
	char* server = SlotUTF8(account, "imap");
	char* user = SlotUTF8(account, "user");
	char* password = HostMailKeyGet(id);
	Ref result;
	if (server[0] == 0)
		result = ErrorFrame("the account has no incoming (IMAP) server");
	else if (password == NULL)
		result = ErrorFrame("the account has no password");
	else
	{
		HostMailJob* job = HostMailStartFetch(server, user, password);
		int index = job != NULL ? AddJob(job) : -1;
		result = index >= 0 ? MAKEINT(index) : ErrorFrame(job == NULL ? "not a mail server's address" : "too many mail jobs");
	}
	if (password != NULL)
	{
		memset(password, 0, strlen(password));
		free(password);
	}
	free(id);
	free(server);
	free(user);
	return result;
}


static Ref
FHostMailPoll(RefArg /*rcvr*/, RefArg job)
{
	if (!ISINT(job) || RINT(job) < 0 || RINT(job) >= kMostJobs || gJobs[RINT(job)] == NULL)
		return ErrorFrame("no such mail job");
	int index = (int) RINT(job);
	HostMailJob* j = gJobs[index];
	int state = HostMailStep(j);
	if (state == 0)
		return Intern((char*) "busy");
	RefVar result;
	if (state < 0)
		result = ErrorFrame(HostMailError(j));
	else if (!HostMailIsFetch(j))
		result = TRUEREF;
	else
	{
		// (a fetch answers its messages, even none)
		int count = HostMailFetchedCount(j);
		RefVar messages(MakeArray(0));
		for (int i = 0; i < count; i++)
		{
			size_t size;
			const char* raw = HostMailFetched(j, i, &size);
			HostMailMessage m;
			HostMailParse(raw, size, &m);
			RefVar frame(AllocateFrame());
			SetFrameSlot(frame, RefVar(Intern((char*) "fromName")), RefVar(StringOfUTF8(m.fromName)));
			SetFrameSlot(frame, RefVar(Intern((char*) "fromAddress")), RefVar(StringOfUTF8(m.fromAddress)));
			SetFrameSlot(frame, RefVar(Intern((char*) "subject")), RefVar(StringOfUTF8(m.subject)));
			SetFrameSlot(frame, RefVar(Intern((char*) "date")), RefVar(StringOfUTF8(m.date)));
			SetFrameSlot(frame, RefVar(Intern((char*) "text")), RefVar(StringOfUTF8(m.text)));
			AddArraySlot(messages, frame);
			HostMailFree(&m);
		}
		result = messages;
	}
	HostMailEnd(j);
	gJobs[index] = NULL;
	return result;
}


void
HostInstallMail(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailConfig")), RefVar(MakeCFunction((void*) FHostMailConfig, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailSetPassword")), RefVar(MakeCFunction((void*) FHostMailSetPassword, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailHasPassword")), RefVar(MakeCFunction((void*) FHostMailHasPassword, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailForgetPassword")), RefVar(MakeCFunction((void*) FHostMailForgetPassword, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailSend")), RefVar(MakeCFunction((void*) FHostMailSend, 4, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailFetch")), RefVar(MakeCFunction((void*) FHostMailFetch, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostMailPoll")), RefVar(MakeCFunction((void*) FHostMailPoll, 1, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostMailSource))));
		RefVar mail(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostMail:host")), mail);
		NSSend(mail, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Mail transport did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostMailFeatureChanged(void)
{
	RefVar mail(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostMail:host"))));
	if (!IsFrame(mail))
		return;
	newton_try
	{
		NSSend(mail, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Mail transport: %s\n", CurrentException()->name);
	}
	end_try;
}
