/*
	File:		host/HostFilesOS.cpp

	Contains:	The host's file system for the Files application.  See
				HostFilesOS.h.  No Newton headers here.
*/

#include "HostFilesOS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif


// local calendar time as the Newton's minutes since 1904
static long
NewtonMinutes(int year, int month, int day, int hour, int minute)
{
	// days from 1 January 1904 to the date (month 1-12)
	static const int kBefore[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	long days = 0;
	for (int y = 1904; y < year; y++)
		days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
	days += kBefore[month - 1] + (day - 1);
	if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
		days++;
	return days * 1440 + hour * 60 + minute;
}


static long
MinutesOfTime(time_t t)
{
	struct tm local;
#if defined(_WIN32)
	localtime_s(&local, &t);
#else
	localtime_r(&t, &local);
#endif
	return NewtonMinutes(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
}


#if defined(_WIN32)

static wchar_t*
Wide(const char* utf8)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	wchar_t* w = (wchar_t*) malloc((size_t) n * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
	for (wchar_t* p = w; *p; p++)
		if (*p == L'/')
			*p = L'\\';
	return w;
}

int
HostFilesListDir(const char* dir, HostFileInfo* out, int max)
{
	size_t length = strlen(dir);
	char* pattern = (char*) malloc(length + 3);
	memcpy(pattern, dir, length);
	strcpy(pattern + length, "/*");
	wchar_t* w = Wide(pattern);
	free(pattern);
	WIN32_FIND_DATAW data;
	HANDLE find = FindFirstFileW(w, &data);
	free(w);
	if (find == INVALID_HANDLE_VALUE)
		return -1;
	int count = 0;
	do
	{
		if (data.cFileName[0] == L'.' || (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
			continue;
		if (count >= max)
			break;
		HostFileInfo* info = &out[count];
		WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, info->name, (int) sizeof(info->name), NULL, NULL);
		info->folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		info->size = ((long long) data.nFileSizeHigh << 32) | data.nFileSizeLow;
		// FILETIME: 100 ns since 1601, UTC
		unsigned long long ft = ((unsigned long long) data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
		time_t t = (time_t) (ft / 10000000ULL - 11644473600ULL);
		info->modified = MinutesOfTime(t);
		info->stamp = (long long) ft;
		count++;
	}
	while (FindNextFileW(find, &data));
	FindClose(find);
	return count;
}

int
HostFilesRename(const char* from, const char* to)
{
	wchar_t* a = Wide(from);
	wchar_t* b = Wide(to);
	BOOL moved = MoveFileExW(a, b, 0);		// (no MOVEFILE_REPLACE_EXISTING: never over a file)
	free(a);
	free(b);
	return moved ? 0 : -1;
}

int
HostFilesKind(const char* path)
{
	wchar_t* w = Wide(path);
	DWORD attributes = GetFileAttributesW(w);
	free(w);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return 0;
	return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1;
}

long
HostFilesReadPrefix(const char* path, char* buffer, long max, long long* total)
{
	wchar_t* w = Wide(path);
	FILE* f = _wfopen(w, L"rb");
	free(w);
	if (f == NULL)
		return -1;
	_fseeki64(f, 0, SEEK_END);
	*total = _ftelli64(f);
	_fseeki64(f, 0, SEEK_SET);
	long got = (long) fread(buffer, 1, (size_t) max, f);
	fclose(f);
	return got;
}

int
HostFilesWrite(const char* path, const char* data, long size)
{
	wchar_t* w = Wide(path);
	FILE* f = _wfopen(w, L"wb");
	free(w);
	if (f == NULL)
		return -1;
	size_t put = fwrite(data, 1, (size_t) size, f);
	fclose(f);
	return put == (size_t) size ? 0 : -1;
}

static int
MakeOneDir(const char* path)
{
	wchar_t* w = Wide(path);
	BOOL made = CreateDirectoryW(w, NULL);
	DWORD err = GetLastError();
	free(w);
	return made || err == ERROR_ALREADY_EXISTS ? 0 : -1;
}

const char*
HostFilesHome(void)
{
	static char sHome[1024] = "";
	if (sHome[0] == 0)
	{
		const wchar_t* profile = _wgetenv(L"USERPROFILE");
		if (profile != NULL)
		{
			WideCharToMultiByte(CP_UTF8, 0, profile, -1, sHome, (int) sizeof(sHome), NULL, NULL);
			for (char* p = sHome; *p; p++)
				if (*p == '\\')
					*p = '/';
		}
	}
	return sHome;
}

#else

int
HostFilesListDir(const char* dir, HostFileInfo* out, int max)
{
	DIR* d = opendir(dir);
	if (d == NULL)
		return -1;
	int count = 0;
	struct dirent* e;
	size_t length = strlen(dir);
	while ((e = readdir(d)) != NULL && count < max)
	{
		if (e->d_name[0] == '.')
			continue;
		char* full = (char*) malloc(length + strlen(e->d_name) + 2);
		sprintf(full, "%s/%s", dir, e->d_name);
		struct stat st;
		int ok = stat(full, &st) == 0;
		free(full);
		if (!ok)
			continue;
		HostFileInfo* info = &out[count];
		snprintf(info->name, sizeof(info->name), "%s", e->d_name);
		info->folder = S_ISDIR(st.st_mode);
		info->size = (long long) st.st_size;
		info->modified = MinutesOfTime(st.st_mtime);
		info->stamp = (long long) st.st_mtime;
		count++;
	}
	closedir(d);
	return count;
}

int
HostFilesRename(const char* from, const char* to)
{
	struct stat st;
	if (stat(to, &st) == 0)
		return -1;							// (rename would replace it)
	return rename(from, to) == 0 ? 0 : -1;
}

int
HostFilesKind(const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	return S_ISDIR(st.st_mode) ? 2 : 1;
}

long
HostFilesReadPrefix(const char* path, char* buffer, long max, long long* total)
{
	FILE* f = fopen(path, "rb");
	if (f == NULL)
		return -1;
	fseeko(f, 0, SEEK_END);
	*total = (long long) ftello(f);
	fseeko(f, 0, SEEK_SET);
	long got = (long) fread(buffer, 1, (size_t) max, f);
	fclose(f);
	return got;
}

int
HostFilesWrite(const char* path, const char* data, long size)
{
	FILE* f = fopen(path, "wb");
	if (f == NULL)
		return -1;
	size_t put = fwrite(data, 1, (size_t) size, f);
	fclose(f);
	return put == (size_t) size ? 0 : -1;
}

static int
MakeOneDir(const char* path)
{
	return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

const char*
HostFilesHome(void)
{
	const char* home = getenv("HOME");
	return home != NULL ? home : "";
}

#endif


int
HostFilesMakeDirs(const char* path)
{
	size_t length = strlen(path);
	char* partial = (char*) malloc(length + 1);
	int result = 0;
	for (size_t i = 1; i <= length && result == 0; i++)
		if (path[i] == '/' || path[i] == 0)
		{
			memcpy(partial, path, i);
			partial[i] = 0;
			// (a drive's root, "C:", is there already)
			if (!(i == 2 && partial[1] == ':'))
				result = MakeOneDir(partial);
		}
	free(partial);
	return result;
}
