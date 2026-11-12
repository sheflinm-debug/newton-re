/*
	File:		hal/host/HostCamera.cpp

	Contains:	The host's camera: the fake camera over a folder of
				pictures.  See HostCamera.h.
*/

#include "HostCamera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#endif

namespace {

const int kMostPictures = 200;

struct FakeCamera
{
	bool		looked;
	char		folder[1024];
	char*		names[kMostPictures];		// the pictures, by name
	int			count;
	int			current;					// the one in front of the camera
	HostImage	frame;
	int			frameOf;					// which picture `frame` is (-1 none)
};

FakeCamera gCamera;

bool
IsPictureName(const char* name)
{
	const char* dot = strrchr(name, '.');
	if (dot == NULL || name[0] == '.')
		return false;
#if defined(_WIN32)
	return _stricmp(dot, ".jpg") == 0 || _stricmp(dot, ".jpeg") == 0 || _stricmp(dot, ".png") == 0;
#else
	return strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0;
#endif
}

int
ByName(const void* a, const void* b)
{
	return strcmp(*(char* const*) a, *(char* const*) b);
}

FakeCamera*
Camera(void)
{
	if (!gCamera.looked)
	{
		gCamera.looked = true;
		gCamera.frameOf = -1;
		const char* folder = getenv("NEWTON_CAMERA");
		if (folder == NULL || folder[0] == 0)
			return NULL;
		snprintf(gCamera.folder, sizeof(gCamera.folder), "%s", folder);
#if defined(_WIN32)
		char pattern[1100];
		snprintf(pattern, sizeof(pattern), "%s\\*", folder);
		WIN32_FIND_DATAA data;
		HANDLE find = FindFirstFileA(pattern, &data);
		if (find != INVALID_HANDLE_VALUE)
		{
			do
				if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsPictureName(data.cFileName) && gCamera.count < kMostPictures)
					gCamera.names[gCamera.count++] = strdup(data.cFileName);
			while (FindNextFileA(find, &data));
			FindClose(find);
		}
#else
		DIR* d = opendir(folder);
		if (d != NULL)
		{
			struct dirent* e;
			while ((e = readdir(d)) != NULL)
				if (IsPictureName(e->d_name) && gCamera.count < kMostPictures)
					gCamera.names[gCamera.count++] = strdup(e->d_name);
			closedir(d);
		}
#endif
		qsort(gCamera.names, (size_t) gCamera.count, sizeof(char*), ByName);
		fprintf(stderr, "[camera] the fake camera: %d pictures in %s\n", gCamera.count, folder);
	}
	return gCamera.count > 0 ? &gCamera : NULL;
}

uint8_t*
ReadPicture(FakeCamera* camera, int index, size_t* size)
{
	char path[1600];
	snprintf(path, sizeof(path), "%s/%s", camera->folder, camera->names[index]);
	FILE* f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* data = (uint8_t*) malloc((size_t) n + 1);
	*size = fread(data, 1, (size_t) n, f);
	fclose(f);
	return data;
}

} // namespace


const char*
HostCameraName(void)
{
	return Camera() != NULL ? "Fake camera" : "";
}

const HostImage*
HostCameraFrame(void)
{
	FakeCamera* camera = Camera();
	if (camera == NULL)
		return NULL;
	if (camera->frameOf != camera->current)
	{
		HostImageFree(&camera->frame);
		camera->frameOf = -1;
		size_t size;
		uint8_t* data = ReadPicture(camera, camera->current, &size);
		const char* why = "";
		if (data == NULL || HostImageDecode(data, size, &camera->frame, &why) != 0)
		{
			fprintf(stderr, "[camera] %s: %s\n", camera->names[camera->current], data == NULL ? "not read" : why);
			free(data);
			return NULL;
		}
		free(data);
		camera->frameOf = camera->current;
	}
	return &camera->frame;
}

int
HostCameraTake(uint8_t** bytes, size_t* size, const char** extension)
{
	FakeCamera* camera = Camera();
	if (camera == NULL)
		return -1;
	*bytes = ReadPicture(camera, camera->current, size);
	if (*bytes == NULL)
		return -1;
	*extension = HostImageKindOf(*bytes, *size) == kHostImagePNG ? "png" : "jpg";
	fprintf(stderr, "[camera] took %s\n", camera->names[camera->current]);
	camera->current = (camera->current + 1) % camera->count;
	return 0;
}
