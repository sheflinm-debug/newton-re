/*
	File:		host/HostOpenURL.cpp

	Contains:	An address handed to the device that runs newton - its
				browser, mail program or telephone (host/HostMarks.cpp's
				HostOpenURL).  Kept apart from the Newton headers, which
				<windows.h> collides with (Polygon, ...).  The caller has
				already let through only plain http:, https:, mailto: and
				tel: addresses.
*/

#include "HostOpenURL.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif !defined(ESP_PLATFORM)
#include <unistd.h>
#include <sys/types.h>
#endif

bool
HostOpenURLText(const char* url)
{
#if defined(_WIN32)
	return (INT_PTR) ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
#elif defined(ESP_PLATFORM)
	(void) url;
	return false;
#else
	pid_t pid = fork();
	if (pid == 0)
	{
#if defined(__APPLE__)
		execlp("open", "open", url, (char*) NULL);
#else
		execlp("xdg-open", "xdg-open", url, (char*) NULL);
#endif
		_exit(127);
	}
	return pid > 0;
#endif
}
