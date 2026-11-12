/*
	File:		host/HostPictures.cpp

	Contains:	DEVIATION (host addition; docs/modern/camera-gallery.md): the
				Gallery and the Camera (HostGallery.ns, compiled in as a
				string) evaluated at boot, kept in the global HostGallery:host
				and started - they install themselves only when the Host
				panel's "Camera and Gallery" is on (host/Features.h's
				kFeatureGallery, NEWTON_GALLERY), the Camera only when the
				host has a camera (hal/host/HostCamera.h) - and the natives
				they use.  Pictures are decoded by hal/host/HostImage.h and
				made the Newton's grays; paths are the Files folder's
				(HostFilesPaths.h).

				  HostPictureInfo(path) - {width, height} from the file's
					  header, nil when it is not a picture this host reads.
				  HostPictureBitmap(path, width, height, depth) - the
					  picture fitted inside width x height (never enlarged),
					  a bitmap shape of that depth (1 dithered, 2, 4 or 8
					  grays); nil when it cannot be read.
				  HostCameraName() - the camera's name, nil when there is
					  none.
				  HostCameraPreview(width, height, depth) - what it sees,
					  a bitmap as HostPictureBitmap makes.
				  HostCameraTake(folder) - a picture taken, written into
					  that folder of the Files folder as
					  IMG_YYYYMMDD_HHMMSS.jpg (a number added when the name
					  is taken): ==> its name, nil when none was taken.
*/

#include "HostPictures.h"
#include "HostFilesOS.h"
#include "HostImage.h"
#include "HostCamera.h"
#include "host/Features.h"
#include "Frames.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NewtonExceptions.h"
#include "Pictures.h"
#include "RSSymbols.h"
#include "ByteOrder.h"
#include "HostFilesPaths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const char gHostGallerySource[];		// HostGallery.ns (HostGallerySource.cpp, generated)

static const long kMostPictureBytes = 128L * 1024 * 1024;		// (a larger file is not opened)


// A whole file read (malloc'd), NULL when it cannot be or is too large.
static uint8_t*
ReadWhole(const char* path, size_t* size)
{
	long long total = 0;
	char probe[1];
	if (HostFilesReadPrefix(path, probe, 0, &total) < 0 || total <= 0 || total > kMostPictureBytes)
		return NULL;
	uint8_t* data = (uint8_t*) malloc((size_t) total);
	long got = HostFilesReadPrefix(path, (char*) data, (long) total, &total);
	if (got < 0)
	{
		free(data);
		return NULL;
	}
	*size = (size_t) got;
	return data;
}


// A decoded picture made a Newton bitmap shape fitted inside the box.
static Ref
BitmapOf(const HostImage* image, long boxWidth, long boxHeight, long depth)
{
	if (!(depth == 1 || depth == 2 || depth == 4 || depth == 8))
		depth = 4;
	if (boxWidth < 1) boxWidth = 1;
	if (boxHeight < 1) boxHeight = 1;
	int width, height;
	HostImageFit(image, (int) boxWidth, (int) boxHeight, 0, &width, &height);
	uint8_t* gray = (uint8_t*) malloc((size_t) width * height);
	HostImageGrays(image, width, height, gray);
	RefVar options(AllocateFrame());
	SetFrameSlot(options, RefVar(Intern((char*) "depth")), RefVar(MAKEINT(depth)));
	RefVar shape(FMakeBitmap(RefVar(), RefVar(MAKEINT(width)), RefVar(MAKEINT(height)), options));
	RefVar pixels(GetFrameSlot(shape, RSSYMdata));
	uint8_t* header = (uint8_t*) BinaryData(pixels);
	ULong32 offset = GetBigEndianWord(header);
	long rowBytes = (long) GetBigEndianHalf(header + 4);
	HostImagePack(gray, width, height, (int) depth, header + offset, rowBytes);
	free(gray);
	return shape;
}


static Ref
FHostPictureInfo(RefArg /*rcvr*/, RefArg relative)
{
	char* full = HostFilesFullPath(relative);
	if (full == NULL)
		return NILREF;
	// (a JPEG's frame header may follow a large EXIF thumbnail)
	const long kPrefix = 1024 * 1024;
	char* buffer = (char*) malloc(kPrefix);
	long long total = 0;
	long got = HostFilesReadPrefix(full, buffer, kPrefix, &total);
	free(full);
	int width = 0, height = 0;
	const char* why = "";
	int err = got > 0 ? HostImageSize((const uint8_t*) buffer, (size_t) got, &width, &height, &why) : -1;
	free(buffer);
	if (err != 0)
		return NILREF;
	RefVar info(AllocateFrame());
	SetFrameSlot(info, RefVar(Intern((char*) "width")), RefVar(MAKEINT(width)));
	SetFrameSlot(info, RefVar(Intern((char*) "height")), RefVar(MAKEINT(height)));
	return info;
}


static Ref
FHostPictureBitmap(RefArg /*rcvr*/, RefArg relative, RefArg width, RefArg height, RefArg depth)
{
	char* full = HostFilesFullPath(relative);
	if (full == NULL)
		return NILREF;
	size_t size = 0;
	uint8_t* data = ReadWhole(full, &size);
	HostImage image;
	const char* why = "not read";
	int err = data != NULL ? HostImageDecode(data, size, &image, &why) : -1;
	free(data);
	if (err != 0)
	{
		fprintf(stderr, "[pictures] %s: %s\n", full, why);
		free(full);
		return NILREF;
	}
	free(full);
	RefVar shape(BitmapOf(&image, ISINT(width) ? RINT(width) : 64, ISINT(height) ? RINT(height) : 64, ISINT(depth) ? RINT(depth) : 4));
	HostImageFree(&image);
	return shape;
}


static Ref
FHostCameraName(RefArg /*rcvr*/)
{
	const char* name = HostCameraName();
	return name[0] != 0 ? MakeString(name) : NILREF;
}


static Ref
FHostCameraPreview(RefArg /*rcvr*/, RefArg width, RefArg height, RefArg depth)
{
	const HostImage* frame = HostCameraFrame();
	if (frame == NULL)
		return NILREF;
	return BitmapOf(frame, ISINT(width) ? RINT(width) : 160, ISINT(height) ? RINT(height) : 120, ISINT(depth) ? RINT(depth) : 4);
}


static Ref
FHostCameraTake(RefArg /*rcvr*/, RefArg folder)
{
	uint8_t* bytes = NULL;
	size_t size = 0;
	const char* extension = "jpg";
	if (!IsString(folder) || HostCameraTake(&bytes, &size, &extension) != 0)
		return NILREF;
	char* dir = HostFilesFullPath(folder);
	if (dir == NULL)
	{
		free(bytes);
		return NILREF;
	}
	HostFilesMakeDirs(dir);
	time_t now = time(NULL);
	struct tm local;
#if defined(_WIN32)
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char name[64];
	char* path = (char*) malloc(strlen(dir) + 80);
	for (int n = 1; ; n++)
	{
		if (n == 1)
			snprintf(name, sizeof(name), "IMG_%04d%02d%02d_%02d%02d%02d.%s", local.tm_year + 1900, local.tm_mon + 1,
				local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, extension);
		else
			snprintf(name, sizeof(name), "IMG_%04d%02d%02d_%02d%02d%02d_%d.%s", local.tm_year + 1900, local.tm_mon + 1,
				local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, n, extension);
		sprintf(path, "%s/%s", dir, name);
		if (HostFilesKind(path) == 0)
			break;
	}
	int err = HostFilesWrite(path, (const char*) bytes, (long) size);
	fprintf(stderr, "[camera] %s %s\n", err == 0 ? "wrote" : "could not write", path);
	free(bytes);
	free(path);
	free(dir);
	if (err != 0)
		return NILREF;
	return MakeString(name);
}


void
HostInstallGallery(void)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPictureInfo")), RefVar(MakeCFunction((void*) FHostPictureInfo, 1, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostPictureBitmap")), RefVar(MakeCFunction((void*) FHostPictureBitmap, 4, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostCameraName")), RefVar(MakeCFunction((void*) FHostCameraName, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostCameraPreview")), RefVar(MakeCFunction((void*) FHostCameraPreview, 3, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "HostCameraTake")), RefVar(MakeCFunction((void*) FHostCameraTake, 1, nil)));
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostGallerySource))));
		RefVar gallery(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostGallery:host")), gallery);
		NSSend(gallery, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Gallery did not start: %s\n", CurrentException()->name);
	}
	end_try;
}


void
HostGalleryFeatureChanged(void)
{
	RefVar gallery(GetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "HostGallery:host"))));
	if (!IsFrame(gallery))
		return;
	newton_try
	{
		NSSend(gallery, RefVar(Intern((char*) "Start")));		// (Start stops it when it is off)
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Gallery: %s\n", CurrentException()->name);
	}
	end_try;
}
