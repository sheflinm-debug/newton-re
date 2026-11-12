/*
	File:		host/HostFilesPaths.h

	Contains:	DEVIATION (host addition; docs/modern/files.md): a path of
				the Files folder, as NewtonScript gives it, made absolute -
				for the natives of the Files application (HostFiles.cpp)
				and of the Gallery and Camera (HostPictures.cpp).  Include
				it after the Newton headers.
*/

#ifndef __HOSTFILESPATHS_H
#define __HOSTFILESPATHS_H

// The absolute path (UTF-8, '/' between names; malloc'd) of a path
// relative to the Files folder; NULL when it is not a string, climbs out
// of the folder ("..") or names a drive.
char*	HostFilesFullPath(RefArg relative);

#endif	/* __HOSTFILESPATHS_H */
