/*
	File:		host/HostFilesOS.h

	Contains:	DEVIATION (host addition; docs/modern/files.md): the host's
				file system as the Files application needs it, in plain C -
				no Newton headers, which collide with <windows.h> - for
				host/HostFiles.cpp.  Paths are UTF-8, absolute, with '/'
				between names; on Windows they are taken as wide paths.
*/

#ifndef __HOSTFILESOS_H
#define __HOSTFILESOS_H

#include <stddef.h>

struct HostFileInfo
{
	char		name[512];		// UTF-8
	int			folder;			// a directory
	long long	size;			// bytes
	long		modified;		// local time, minutes since 1 January 1904 (the Newton's Time())
	long long	stamp;			// the modification time as finely as the host has it (for noticing changes)
};

// A file or directory renamed or moved (not over one that is there).
// ==> 0, or -1.
int		HostFilesRename(const char* from, const char* to);

// ==> 0 nothing there, 1 a file, 2 a directory.
int		HostFilesKind(const char* path);

// A file renamed over one that may be there (an atomic replace where the
// host has one).  ==> 0, or -1.
int		HostFilesReplace(const char* from, const char* to);

// Bytes from the system's secure random generator.  ==> 0, or -1.
int		HostFilesRandom(unsigned char* bytes, size_t count);

// The entries of a directory, without "." and ".." and hidden ones (a
// leading '.').  ==> how many were put in `out` (at most `max`), -1 when it
// cannot be read.
int		HostFilesListDir(const char* dir, HostFileInfo* out, int max);

// Up to `max` bytes from the start of a file.  ==> how many, -1 when it
// cannot be read; `total` its whole size.
long	HostFilesReadPrefix(const char* path, char* buffer, long max, long long* total);

// A file written whole (made or replaced).  ==> 0, or -1.
int		HostFilesWrite(const char* path, const char* data, long size);

// A directory made, and its parents (0 also when it is there).  ==> 0, or -1.
int		HostFilesMakeDirs(const char* path);

// The user's home directory (UTF-8), "" when there is none.
const char*	HostFilesHome(void);

#endif	/* __HOSTFILESOS_H */
