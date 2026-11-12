/*
	File:		hal/host/HostCamera.h

	Contains:	DEVIATION (host addition; docs/modern/camera-gallery.md):
				the host's camera, as the Camera application needs it -
				whether there is one, a preview frame, a picture taken as
				the file's bytes.  Plain C++: no Newton headers.

				Behind it so far only the fake camera of the spec:
				NEWTON_CAMERA=<folder> makes a camera of a folder of
				pictures (JPEG or PNG; src/host/demo/camera holds three) -
				its preview is the picture it is pointing at, a picture
				taken is that file's bytes as they are, and the next
				picture is then in front of it.  Without NEWTON_CAMERA
				there is no camera, and the Camera application is not
				installed.
				NOT YET: Linux's V4L2, Windows' Media Foundation, a
				device's own camera; front and back.
*/

#ifndef __HOSTCAMERA_H
#define __HOSTCAMERA_H

#include "HostImage.h"

// The camera's name ("" none).
const char*	HostCameraName(void);

// What the camera sees, decoded (the caller does not free it: the camera
// keeps it until the next call).  ==> NULL when there is no camera.
const HostImage*	HostCameraFrame(void);

// A picture taken: its bytes (malloc'd: the caller frees them) and the
// file name ending it should have ("jpg", "png").  ==> 0, or -1.
int		HostCameraTake(uint8_t** bytes, size_t* size, const char** extension);

#endif	/* __HOSTCAMERA_H */
