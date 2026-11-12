/*
	File:		hal/host/HostImage.h

	Contains:	DEVIATION (host addition; docs/modern/camera-gallery.md):
				pictures decoded on the host - PNG and baseline JPEG, by a
				decoder of our own (HostImage.cpp: no library, nothing
				vendored), and a picture scaled into the Newton's grays.
				Plain C++: no Newton headers.

				What is read:
				  PNG - every colour type and bit depth, palettes and
					transparency (over white), Adam7 interlacing.
				  JPEG - baseline and extended Huffman (SOF0, SOF1), one or
					three components, any sampling, restart intervals.
				NOT YET: progressive and arithmetic JPEG, CMYK, the EXIF
				orientation; GIF, WebP, HEIC (the host's own decoders).
*/

#ifndef __HOSTIMAGE_H
#define __HOSTIMAGE_H

#include <stddef.h>
#include <stdint.h>

struct HostImage
{
	int			width;
	int			height;
	uint8_t*	rgb;			// width * height * 3, rows top down (malloc'd)
};

enum HostImageKind { kHostImageUnknown = 0, kHostImagePNG, kHostImageJPEG };

// The kind of picture the bytes start with.
HostImageKind	HostImageKindOf(const uint8_t* data, size_t size);

// A picture's size, from its header only.  ==> 0, or -1 (`why` says).
int		HostImageSize(const uint8_t* data, size_t size, int* width, int* height, const char** why);

// A picture decoded.  ==> 0 and `out` filled (free it with
// HostImageFree), or -1 (`why` says: "not a picture", "progressive JPEG:
// NOT YET", ...).
int		HostImageDecode(const uint8_t* data, size_t size, HostImage* out, const char** why);

void	HostImageFree(HostImage* image);

// The picture fitted inside width x height, its proportions kept (never
// made larger than it is unless `enlarge`): ==> the size chosen.
void	HostImageFit(const HostImage* image, int boxWidth, int boxHeight, int enlarge, int* width, int* height);

// The picture scaled to width x height (each pixel the average of the
// ones it covers) as grays, 0 white to 255 black - the Newton's way round
// - weighted as the ROM's RGBtoGray weights colours (0.299, 0.587, 0.114).
// `gray` holds width * height bytes.
void	HostImageGrays(const HostImage* image, int width, int height, uint8_t* gray);

// Grays packed as a Newton pixel map's rows: depth 1 (dithered, error
// diffusion), 2, 4 or 8 bits, most significant pixel first; `rowBytes`
// apart.
void	HostImagePack(const uint8_t* gray, int width, int height, int depth, uint8_t* rows, long rowBytes);

#endif	/* __HOSTIMAGE_H */
