/*
	File:		host/HostFiles.cpp

	Contains:	DEVIATION (host addition; docs/modern/files.md): the Files
				application (HostFiles.ns, compiled in as a string) evaluated
				at boot, kept in the global HostFiles:host and started - it
				installs itself only when the Host panel's "Files" is on
				(host/Features.h's kFeatureFiles, NEWTON_FILES) - and the
				natives it reads the host's folder with.  The folder is
				NEWTON_FILES_ROOT, or "Newton Files" in the user's home.

				  HostFilesRoot() - the folder, made with its standard
					  folders (Documents, Downloads, Pictures, Music,
					  Received) when it is not there.
				  HostFilesList(path) - a folder's entries, folders first:
					  [{name, folder, size, modified}], modified as Time()
					  has it; nil when it cannot be read.
				  HostFilesReadText(path, maxChars) - a file's text (UTF-8,
					  or Latin-1 when it is not UTF-8; line ends as the
					  Newton's returns), at most maxChars of it.
				  HostFilesWriteText(path, text) - a file made or replaced
					  with the text (UTF-8, LF line ends).
				  HostFilesMakeFolder(path).

				A path is relative to the folder, names joined by "/"; one
				that climbs out of it ("..") or names a drive is refused.
*/

#include "HostFiles.h"
#include "HostFilesOS.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char gHostFilesSource[];		// HostFiles.ns (HostFilesSource.cpp, generated)

static const char* const kStandardFolders[] = { "Documents", "Downloads", "Pictures", "Music", "Received" };


static const char*
FilesRoot(void)
{
	static char sRoot[1024] = "";
	if (sRoot[0] == 0)
	{
		const char* asked = getenv("NEWTON_FILES_ROOT");
		if (asked != NULL && asked[0] != 0)
			snprintf(sRoot, sizeof(sRoot), "%s", asked);
		else
			snprintf(sRoot, sizeof(sRoot), "%s/Newton Files", HostFilesHome());
		for (char* p = sRoot; *p; p++)
			if (*p == '\\')
				*p = '/';
		size_t n = strlen(sRoot);
		while (n > 1 && sRoot[n - 1] == '/')
			sRoot[--n] = 0;
	}
	return sRoot;
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


// The absolute path of a relative one (malloc'd), nil when it is refused.
static char*
FullPath(RefArg relative)
{
	if (!IsString(relative))
		return NULL;
	char* rel = UTF8Of(relative);
	for (char* p = rel; *p; p++)
		if (*p == '\\' || *p == '\n')
			*p = '/';
	bool refused = rel[0] == '/' || strchr(rel, ':') != NULL;
	// no ".." among its names
	for (const char* p = rel; *p && !refused; )
	{
		const char* end = strchr(p, '/');
		size_t n = end != NULL ? (size_t) (end - p) : strlen(p);
		if (n == 2 && p[0] == '.' && p[1] == '.')
			refused = true;
		p += n;
		if (*p == '/')
			p++;
	}
	if (refused)
	{
		fprintf(stderr, "[files] refused: %s\n", rel);
		free(rel);
		return NULL;
	}
	const char* root = FilesRoot();
	char* full = (char*) malloc(strlen(root) + strlen(rel) + 2);
	if (rel[0] == 0)
		strcpy(full, root);
	else
		sprintf(full, "%s/%s", root, rel);
	free(rel);
	size_t n = strlen(full);
	while (n > 1 && full[n - 1] == '/')
		full[--n] = 0;
	return full;
}


static Ref
StringOfUTF8(const char* text, long size, long maxChars)
{
	UniChar* u = (UniChar*) malloc(((size_t) size + 1) * sizeof(UniChar));
	long n = 0;
	const unsigned char* p = (const unsigned char*) text;
	const unsigned char* end = p + size;
	if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
		p += 3;		// (a byte order mark)
	// UTF-8, unless a byte says it is not
	bool utf8 = true;
	for (const unsigned char* q = p; q < end && utf8; q++)
		if (*q >= 0x80)
		{
			int more = *q >= 0xF0 ? 3 : *q >= 0xE0 ? 2 : *q >= 0xC0 ? 1 : -1;
			if (more < 0)
				utf8 = false;
			for (int k = 1; k <= more && utf8; k++)
				if (q + k < end && (q[k] & 0xC0) != 0x80)
					utf8 = false;
			if (more > 0)
				q += more;
		}
	while (p < end && n < maxChars)
	{
		unsigned long c = *p++;
		if (utf8 && c >= 0xC0)
		{
			int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
			c &= more == 3 ? 0x07 : more == 2 ? 0x0F : 0x1F;
			while (more-- > 0 && p < end)
				c = (c << 6) | (*p++ & 0x3F);
		}
		if (c == '\r')
		{
			if (p < end && *p == '\n')
				p++;
			c = 0x0D;
		}
		else if (c == '\n')
			c = 0x0D;
		u[n++] = c > 0xFFFF ? (UniChar) '?' : (UniChar) c;
	}
	RefVar str(MakeString(u, n));
	free(u);
	return str;
}


static Ref
FHostFilesRoot(RefArg /*rcvr*/)
{
	const char* root = FilesRoot();
	if (HostFilesMakeDirs(root) != 0)
		return NILREF;
	for (size_t i = 0; i < sizeof(kStandardFolders) / sizeof(kStandardFolders[0]); i++)
	{
		char path[1200];
		snprintf(path, sizeof(path), "%s/%s", root, kStandardFolders[i]);
		HostFilesMakeDirs(path);
	}
	return StringOfUTF8(root, (long) strlen(root), 1024);
}


static Ref
FHostFilesList(RefArg /*rcvr*/, RefArg relative)
{
	char* full = FullPath(relative);
	if (full == NULL)
		return NILREF;
	const int kMax = 1000;
	HostFileInfo* infos = (HostFileInfo*) malloc(sizeof(HostFileInfo) * kMax);
	int count = HostFilesListDir(full, infos, kMax);
	free(full);
	if (count < 0)
	{
		free(infos);
		return NILREF;
	}
	// folders first, then by name, without case
	for (int i = 1; i < count; i++)
		for (int j = i; j > 0; j--)
		{
			HostFileInfo* a = &infos[j - 1];
			HostFileInfo* b = &infos[j];
			int order = a->folder != b->folder ? (a->folder ? -1 : 1)
#if defined(_WIN32)
				: _stricmp(a->name, b->name);
#else
				: strcasecmp(a->name, b->name);
#endif
			if (order <= 0)
				break;
			HostFileInfo t = *a;
			*a = *b;
			*b = t;
		}
	RefVar list(MakeArray(0));
	for (int i = 0; i < count; i++)
	{
		RefVar entry(AllocateFrame());
		SetFrameSlot(entry, RefVar(Intern((char*) "name")), RefVar(StringOfUTF8(infos[i].name, (long) strlen(infos[i].name), 512)));
		SetFrameSlot(entry, RefVar(Intern((char*) "folder")), RefVar(MAKEBOOLEAN(infos[i].folder)));
		long long size = infos[i].size;
		SetFrameSlot(entry, RefVar(Intern((char*) "size")), RefVar(MAKEINT(size > 0x1FFFFFFF ? 0x1FFFFFFF : (long) size)));
		SetFrameSlot(entry, RefVar(Intern((char*) "modified")), RefVar(MAKEINT(infos[i].modified)));
		AddArraySlot(list, entry);
	}
	free(infos);
	return list;
}


static Ref
FHostFilesReadText(RefArg /*rcvr*/, RefArg relative, RefArg maxChars)
{
	char* full = FullPath(relative);
	if (full == NULL)
		return NILREF;
	long most = ISINT(maxChars) ? (long) RINT(maxChars) : 32000;
	if (most < 1)
		most = 1;
	long bytes = most * 4;
	char* buffer = (char*) malloc((size_t) bytes);
	long long total = 0;
	long got = HostFilesReadPrefix(full, buffer, bytes, &total);
	free(full);
	if (got < 0)
	{
		free(buffer);
		return NILREF;
	}
	RefVar text(StringOfUTF8(buffer, got, most));
	free(buffer);
	return text;
}


static Ref
FHostFilesWriteText(RefArg /*rcvr*/, RefArg relative, RefArg text)
{
	if (!IsString(text))
		return NILREF;
	char* full = FullPath(relative);
	if (full == NULL)
		return NILREF;
	char* data = UTF8Of(text);
	int err = HostFilesWrite(full, data, (long) strlen(data));
	fprintf(stderr, "[files] %s %s\n", err == 0 ? "wrote" : "could not write", full);
	free(data);
	free(full);
	return MAKEBOOLEAN(err == 0);
}


static Ref
FHostFilesMakeFolder(RefArg /*rcvr*/, RefArg relative)
{
	char* full = FullPath(relative);
	if (full == NULL)
		return NILREF;
	int err = HostFilesMakeDirs(full);
	free(full);
	return MAKEBOOLEAN(err == 0);
}


void
HostInstallFiles(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFilesRoot")), RefVar(MakeCFunction((void*) FHostFilesRoot, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFilesList")), RefVar(MakeCFunction((void*) FHostFilesList, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFilesReadText")), RefVar(MakeCFunction((void*) FHostFilesReadText, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFilesWriteText")), RefVar(MakeCFunction((void*) FHostFilesWriteText, 2, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostFilesMakeFolder")), RefVar(MakeCFunction((void*) FHostFilesMakeFolder, 1, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostFilesSource))));
		RefVar files(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostFiles:host")), files);
		NSSend(files, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Files application did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostFilesFeatureChanged(void)
{
	RefVar files(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostFiles:host"))));
	if (!IsFrame(files))
		return;
	newton_try
	{
		NSSend(files, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Files application: %s\n", CurrentException()->name);
	}
	end_try;
}
