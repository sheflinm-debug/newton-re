/*
	File:		host/HostPictures.h

	Contains:	DEVIATION (host addition; docs/modern/camera-gallery.md):
				the Gallery and the Camera (HostGallery.ns) and the natives
				they draw pictures and take them with.  See HostPictures.cpp.
*/

#ifndef __HOSTPICTURES_H
#define __HOSTPICTURES_H

void	HostInstallGallery(void);			// at boot, after HostInstallFiles
void	HostGalleryFeatureChanged(void);	// "Camera and Gallery" turned on or off on the panel

#endif	/* __HOSTPICTURES_H */
