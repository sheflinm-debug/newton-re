/*
	File:		hal/tests/test_HostImage.cpp

	Contains:	The host's picture decoder (hal/host/HostImage.h) against
				Pillow: each picture of src/hal/tests/pictures (made by
				tools/host/picturefixtures.py) decoded and compared with the
				.ppm Pillow decoded from the same file - a PNG exactly, a
				JPEG within what two IDCTs and two upsamplers' rounding
				may differ by.  A progressive JPEG is
				refused, by name.  Then fitting, grays and packing.

				test_HostImage <pictures folder>  ==> "all passed"
*/

#include "HostImage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gFailures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAILED: "); printf(__VA_ARGS__); printf("\n"); gFailures++; } } while (0)

static uint8_t*
ReadFile(const char* path, size_t* size)
{
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

// a binary PPM (P6, 255): its pixels, NULL when it cannot be read
static uint8_t*
ReadPPM(const char* path, int* width, int* height)
{
	size_t size;
	uint8_t* data = ReadFile(path, &size);
	if (data == NULL)
		return NULL;
	int fields[3], got = 0;
	size_t pos = 2;
	while (got < 3 && pos < size)
	{
		while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t'))
			pos++;
		int v = 0;
		while (pos < size && data[pos] >= '0' && data[pos] <= '9')
			v = v * 10 + (data[pos++] - '0');
		fields[got++] = v;
	}
	pos++;
	*width = fields[0];
	*height = fields[1];
	uint8_t* pixels = (uint8_t*) malloc((size_t) *width * *height * 3);
	memcpy(pixels, data + pos, (size_t) *width * *height * 3);
	free(data);
	return pixels;
}

static void
Compare(const char* folder, const char* name, double meanAllowed, int maxAllowed)
{
	char path[1024], reference[1024];
	snprintf(path, sizeof(path), "%s/%s", folder, name);
	snprintf(reference, sizeof(reference), "%s/%.*s.ppm", folder, (int) (strrchr(name, '.') - name), name);
	size_t size;
	uint8_t* data = ReadFile(path, &size);
	CHECK(data != NULL, "%s: not there", path);
	if (data == NULL)
		return;
	int w = 0, h = 0;
	const char* why = "";
	CHECK(HostImageSize(data, size, &w, &h, &why) == 0 && w == 61 && h == 47, "%s: size %dx%d (%s)", name, w, h, why);
	HostImage image;
	int err = HostImageDecode(data, size, &image, &why);
	free(data);
	CHECK(err == 0, "%s: not decoded: %s", name, why);
	if (err != 0)
		return;
	int rw, rh;
	uint8_t* want = ReadPPM(reference, &rw, &rh);
	CHECK(want != NULL && rw == image.width && rh == image.height, "%s: the reference", name);
	if (want != NULL && rw == image.width && rh == image.height)
	{
		long total = 0;
		int most = 0;
		size_t n = (size_t) rw * rh * 3;
		for (size_t i = 0; i < n; i++)
		{
			int d = abs((int) image.rgb[i] - (int) want[i]);
			total += d;
			if (d > most)
				most = d;
		}
		double mean = (double) total / (double) n;
		printf("%-18s mean difference %.3f, most %d\n", name, mean, most);
		CHECK(mean <= meanAllowed && most <= maxAllowed, "%s: differs from Pillow's (mean %.3f, most %d)", name, mean, most);
	}
	free(want);
	HostImageFree(&image);
}

int
main(int argc, char** argv)
{
	const char* folder = argc > 1 ? argv[1] : "pictures";

	// PNG: exactly as Pillow has it
	const char* pngs[] = { "rgb8.png", "rgba8.png", "gray8.png", "gray1.png", "gray16.png", "palette4.png", "palette8trns.png", "interlaced.png" };
	for (size_t i = 0; i < sizeof(pngs) / sizeof(pngs[0]); i++)
		Compare(folder, pngs[i], 0.0, 1);		// (alpha over white may round the other way)

	// JPEG: libjpeg's integer IDCT and fixed-point upsampling against our
	// float ones - within a few levels (seen: mean 0.3, most 3)
	Compare(folder, "q95-444.jpg", 1.0, 8);
	Compare(folder, "gray.jpg", 1.0, 8);
	Compare(folder, "q90-420.jpg", 1.0, 8);
	Compare(folder, "q75-422.jpg", 1.0, 8);
	Compare(folder, "restart.jpg", 1.0, 8);

	{
		char path[1024];
		snprintf(path, sizeof(path), "%s/progressive.jpg", folder);
		size_t size;
		uint8_t* data = ReadFile(path, &size);
		HostImage image;
		const char* why = "";
		CHECK(data != NULL && HostImageDecode(data, size, &image, &why) == -1 && strstr(why, "progressive") != NULL,
			"progressive.jpg: not refused (%s)", why);
		printf("progressive.jpg    refused: %s\n", why);
		free(data);
	}

	// not a picture
	{
		const uint8_t junk[] = "hello, world";
		HostImage image;
		const char* why = "";
		CHECK(HostImageDecode(junk, sizeof(junk), &image, &why) == -1, "junk decoded");
	}

	// fitting, grays and packing, on a picture made here: left half
	// black, right half white, 40 x 20
	HostImage made;
	made.width = 40;
	made.height = 20;
	made.rgb = (uint8_t*) malloc(40 * 20 * 3);
	for (int y = 0; y < 20; y++)
		for (int x = 0; x < 40; x++)
			memset(made.rgb + (y * 40 + x) * 3, x < 20 ? 0 : 255, 3);
	int w, h;
	HostImageFit(&made, 10, 10, 0, &w, &h);
	CHECK(w == 10 && h == 5, "fit 40x20 in 10x10: %dx%d", w, h);
	HostImageFit(&made, 100, 100, 0, &w, &h);
	CHECK(w == 40 && h == 20, "fit 40x20 in 100x100 without enlarging: %dx%d", w, h);
	HostImageFit(&made, 100, 100, 1, &w, &h);
	CHECK(w == 100 && h == 50, "fit 40x20 in 100x100 enlarged: %dx%d", w, h);
	uint8_t grays[4 * 2];
	HostImageGrays(&made, 4, 2, grays);
	CHECK(grays[0] == 255 && grays[1] == 255 && grays[2] == 0 && grays[3] == 0, "grays: %d %d %d %d", grays[0], grays[1], grays[2], grays[3]);
	uint8_t rows[2 * 4];
	HostImagePack(grays, 4, 2, 4, rows, 4);
	CHECK(rows[0] == 0xFF && rows[1] == 0x00, "four bits: %02x %02x", rows[0], rows[1]);
	HostImagePack(grays, 4, 2, 1, rows, 4);
	CHECK(rows[0] == 0xC0 && rows[4] == 0xC0, "one bit: %02x %02x", rows[0], rows[4]);
	// a mid gray dithers to about half its pixels black
	{
		uint8_t mid[32 * 32];
		memset(mid, 128, sizeof(mid));
		uint8_t bits[32 * 4];
		HostImagePack(mid, 32, 32, 1, bits, 4);
		int black = 0;
		for (int i = 0; i < 32 * 4; i++)
			for (int b = 0; b < 8; b++)
				black += (bits[i] >> b) & 1;
		CHECK(black > 450 && black < 580, "dithered mid gray: %d of 1024 black", black);
	}
	HostImageFree(&made);

	if (gFailures == 0)
		printf("all passed\n");
	return gFailures == 0 ? 0 : 1;
}
