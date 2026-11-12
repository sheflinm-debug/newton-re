/*
	File:		hal/host/HostImage.cpp

	Contains:	PNG and baseline JPEG decoded, and pictures made Newton
				grays.  See HostImage.h.

				Written for this project from the formats' specifications
				(PNG: the W3C recommendation, with RFC 1950/1951 for zlib
				and deflate; JPEG: ITU-T T.81, with JFIF's colour
				conversion) - no library is taken in.  Checked against
				Pillow's decoding of the same files by ctest hal.HostImage
				(tools/host/picturefixtures.py makes them).
*/

#include "HostImage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>


// ---------------------------------------------------------------------
// inflate (RFC 1951)
// ---------------------------------------------------------------------

namespace {

struct InBits
{
	const uint8_t*	data;
	size_t			size;
	size_t			pos;
	uint32_t		buffer;
	int				count;
	bool			overrun;
};

static int
GetBits(InBits* in, int n)
{
	while (in->count < n)
	{
		uint32_t byte = 0;
		if (in->pos < in->size)
			byte = in->data[in->pos++];
		else
			in->overrun = true;
		in->buffer |= byte << in->count;
		in->count += 8;
	}
	int value = (int) (in->buffer & ((1u << n) - 1));
	in->buffer >>= n;
	in->count -= n;
	return value;
}

struct Huffman
{
	short	count[16];		// codes of each length
	short	symbol[320];	// the symbols, by code
};

// ==> 0, or -1 when the lengths over-subscribe the codes
static int
BuildHuffman(Huffman* h, const short* lengths, int n)
{
	memset(h->count, 0, sizeof(h->count));
	for (int i = 0; i < n; i++)
		h->count[lengths[i]]++;
	if (h->count[0] == n)
		return 0;
	int left = 1;
	for (int len = 1; len < 16; len++)
	{
		left <<= 1;
		left -= h->count[len];
		if (left < 0)
			return -1;
	}
	short offs[16];
	offs[1] = 0;
	for (int len = 1; len < 15; len++)
		offs[len + 1] = (short) (offs[len] + h->count[len]);
	for (int i = 0; i < n; i++)
		if (lengths[i] != 0)
			h->symbol[offs[lengths[i]]++] = (short) i;
	return 0;
}

static int
Decode(InBits* in, const Huffman* h)
{
	int code = 0, first = 0, index = 0;
	for (int len = 1; len < 16; len++)
	{
		code |= GetBits(in, 1);
		int count = h->count[len];
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
		if (in->overrun)
			return -1;
	}
	return -1;
}

static const short kLengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int
InflateCodes(InBits* in, uint8_t* out, size_t outSize, size_t* outPos, const Huffman* lengths, const Huffman* distances)
{
	for (;;)
	{
		int symbol = Decode(in, lengths);
		if (symbol < 0)
			return -1;
		if (symbol < 256)
		{
			if (*outPos >= outSize)
				return -1;
			out[(*outPos)++] = (uint8_t) symbol;
		}
		else if (symbol == 256)
			return 0;
		else
		{
			symbol -= 257;
			if (symbol >= 29)
				return -1;
			int length = kLengthBase[symbol] + GetBits(in, kLengthExtra[symbol]);
			int d = Decode(in, distances);
			if (d < 0 || d >= 30)
				return -1;
			size_t distance = (size_t) (kDistBase[d] + GetBits(in, kDistExtra[d]));
			if (distance > *outPos || *outPos + (size_t) length > outSize)
				return -1;
			for (int i = 0; i < length; i++, (*outPos)++)
				out[*outPos] = out[*outPos - distance];
		}
	}
}

// A zlib stream (RFC 1950) inflated into exactly `outSize` bytes at most.
// ==> how many, or -1.
static long
Inflate(const uint8_t* data, size_t size, uint8_t* out, size_t outSize)
{
	if (size < 2 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20))
		return -1;
	InBits in = { data + 2, size - 2, 0, 0, 0, false };
	size_t outPos = 0;
	int last;
	do
	{
		last = GetBits(&in, 1);
		int type = GetBits(&in, 2);
		if (type == 0)
		{
			in.buffer = 0;
			in.count = 0;
			if (in.pos + 4 > in.size)
				return -1;
			unsigned len = in.data[in.pos] | (in.data[in.pos + 1] << 8);
			unsigned nlen = in.data[in.pos + 2] | (in.data[in.pos + 3] << 8);
			in.pos += 4;
			if (len != (~nlen & 0xFFFF) || in.pos + len > in.size || outPos + len > outSize)
				return -1;
			memcpy(out + outPos, in.data + in.pos, len);
			in.pos += len;
			outPos += len;
		}
		else if (type == 1)
		{
			static Huffman sLengths, sDistances;
			static bool sMade = false;
			if (!sMade)
			{
				short lengths[288];
				int i = 0;
				for (; i < 144; i++) lengths[i] = 8;
				for (; i < 256; i++) lengths[i] = 9;
				for (; i < 280; i++) lengths[i] = 7;
				for (; i < 288; i++) lengths[i] = 8;
				BuildHuffman(&sLengths, lengths, 288);
				for (i = 0; i < 30; i++) lengths[i] = 5;
				BuildHuffman(&sDistances, lengths, 30);
				sMade = true;
			}
			if (InflateCodes(&in, out, outSize, &outPos, &sLengths, &sDistances) != 0)
				return -1;
		}
		else if (type == 2)
		{
			static const short kOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
			int nlen = GetBits(&in, 5) + 257;
			int ndist = GetBits(&in, 5) + 1;
			int ncode = GetBits(&in, 4) + 4;
			if (nlen > 286 || ndist > 30)
				return -1;
			short lengths[320];
			memset(lengths, 0, sizeof(lengths));
			for (int i = 0; i < ncode; i++)
				lengths[kOrder[i]] = (short) GetBits(&in, 3);
			Huffman codes, lengthCodes, distanceCodes;
			if (BuildHuffman(&codes, lengths, 19) != 0)
				return -1;
			int index = 0;
			while (index < nlen + ndist)
			{
				int symbol = Decode(&in, &codes);
				if (symbol < 0)
					return -1;
				if (symbol < 16)
					lengths[index++] = (short) symbol;
				else
				{
					short repeat = 0;
					int times;
					if (symbol == 16)
					{
						if (index == 0)
							return -1;
						repeat = lengths[index - 1];
						times = 3 + GetBits(&in, 2);
					}
					else if (symbol == 17)
						times = 3 + GetBits(&in, 3);
					else
						times = 11 + GetBits(&in, 7);
					if (index + times > nlen + ndist)
						return -1;
					while (times-- > 0)
						lengths[index++] = repeat;
				}
			}
			if (lengths[256] == 0)
				return -1;
			if (BuildHuffman(&lengthCodes, lengths, nlen) != 0 || BuildHuffman(&distanceCodes, lengths + nlen, ndist) != 0)
				return -1;
			if (InflateCodes(&in, out, outSize, &outPos, &lengthCodes, &distanceCodes) != 0)
				return -1;
		}
		else
			return -1;
		if (in.overrun)
			return -1;
	}
	while (!last);
	return (long) outPos;
}


// ---------------------------------------------------------------------
// PNG
// ---------------------------------------------------------------------

static uint32_t
Big32(const uint8_t* p)
{
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static const uint8_t kPNGSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

static int
PaethOf(int a, int b, int c)
{
	int p = a + b - c;
	int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	if (pa <= pb && pa <= pc)
		return a;
	return pb <= pc ? b : c;
}

// one sample (raw, at the file's depth) of a row
static unsigned
SampleOf(const uint8_t* row, long index, int depth)
{
	if (depth == 8)
		return row[index];
	if (depth == 16)
		return (unsigned) ((row[index * 2] << 8) | row[index * 2 + 1]);
	long bit = index * depth;
	return (unsigned) ((row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1));
}

static int
DecodePNG(const uint8_t* data, size_t size, HostImage* out, const char** why)
{
	if (size < 33 || memcmp(data, kPNGSignature, 8) != 0 || memcmp(data + 12, "IHDR", 4) != 0)
		return *why = "not a PNG", -1;
	uint32_t width = Big32(data + 16), height = Big32(data + 20);
	int depth = data[24], colourType = data[25], interlace = data[28];
	int channels = colourType == 0 ? 1 : colourType == 2 ? 3 : colourType == 3 ? 1 : colourType == 4 ? 2 : colourType == 6 ? 4 : 0;
	if (channels == 0 || width == 0 || height == 0 || width > 30000 || height > 30000 || data[26] != 0 || data[27] != 0 || interlace > 1)
		return *why = "a PNG this decoder does not read", -1;
	if (!(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) || (colourType == 3 && depth == 16)
		|| ((colourType == 2 || colourType == 4 || colourType == 6) && depth < 8))
		return *why = "a PNG with a bad depth", -1;

	// the chunks: the palette, transparency, the image data joined
	uint8_t palette[256][4];
	for (int i = 0; i < 256; i++)
		palette[i][0] = palette[i][1] = palette[i][2] = 0, palette[i][3] = 255;
	bool hasKey = false;
	unsigned key[3] = { 0, 0, 0 };
	size_t idatSize = 0;
	for (size_t pos = 8; pos + 12 <= size; )
	{
		uint32_t length = Big32(data + pos);
		if (length > size - pos - 12)
			return *why = "a PNG cut short", -1;
		const uint8_t* type = data + pos + 4;
		const uint8_t* body = data + pos + 8;
		if (memcmp(type, "IDAT", 4) == 0)
			idatSize += length;
		else if (memcmp(type, "PLTE", 4) == 0)
			for (uint32_t i = 0; i < length / 3 && i < 256; i++)
				palette[i][0] = body[i * 3], palette[i][1] = body[i * 3 + 1], palette[i][2] = body[i * 3 + 2];
		else if (memcmp(type, "tRNS", 4) == 0)
		{
			if (colourType == 3)
				for (uint32_t i = 0; i < length && i < 256; i++)
					palette[i][3] = body[i];
			else if (colourType == 0 && length >= 2)
				hasKey = true, key[0] = (unsigned) ((body[0] << 8) | body[1]);
			else if (colourType == 2 && length >= 6)
			{
				hasKey = true;
				for (int c = 0; c < 3; c++)
					key[c] = (unsigned) ((body[c * 2] << 8) | body[c * 2 + 1]);
			}
		}
		else if (memcmp(type, "IEND", 4) == 0)
			break;
		pos += 12 + length;
	}
	uint8_t* compressed = (uint8_t*) malloc(idatSize + 1);
	size_t at = 0;
	for (size_t pos = 8; pos + 12 <= size; )
	{
		uint32_t length = Big32(data + pos);
		if (memcmp(data + pos + 4, "IDAT", 4) == 0)
		{
			memcpy(compressed + at, data + pos + 8, length);
			at += length;
		}
		if (memcmp(data + pos + 4, "IEND", 4) == 0)
			break;
		pos += 12 + length;
	}

	// the passes (one, or Adam7's seven)
	static const int kX0[7] = { 0, 4, 0, 2, 0, 1, 0 }, kY0[7] = { 0, 0, 4, 0, 2, 0, 1 };
	static const int kDX[7] = { 8, 8, 4, 4, 2, 2, 1 }, kDY[7] = { 8, 8, 8, 4, 4, 2, 2 };
	int passes = interlace ? 7 : 1;
	int bitsPerPixel = channels * depth;
	int filterBytes = bitsPerPixel >= 8 ? bitsPerPixel / 8 : 1;
	size_t expected = 0;
	for (int p = 0; p < passes; p++)
	{
		long pw = interlace ? ((long) width - kX0[p] + kDX[p] - 1) / kDX[p] : (long) width;
		long ph = interlace ? ((long) height - kY0[p] + kDY[p] - 1) / kDY[p] : (long) height;
		if (pw > 0 && ph > 0)
			expected += (size_t) ph * (1 + ((size_t) pw * bitsPerPixel + 7) / 8);
	}
	uint8_t* raw = (uint8_t*) malloc(expected + 1);
	long got = Inflate(compressed, idatSize, raw, expected);
	free(compressed);
	if (got != (long) expected)
	{
		free(raw);
		return *why = "a PNG whose data is damaged", -1;
	}

	out->width = (int) width;
	out->height = (int) height;
	out->rgb = (uint8_t*) malloc((size_t) width * height * 3);
	unsigned maxValue = (1u << depth) - 1;
	uint8_t* cursor = raw;
	for (int p = 0; p < passes; p++)
	{
		long pw = interlace ? ((long) width - kX0[p] + kDX[p] - 1) / kDX[p] : (long) width;
		long ph = interlace ? ((long) height - kY0[p] + kDY[p] - 1) / kDY[p] : (long) height;
		if (pw <= 0 || ph <= 0)
			continue;
		size_t rowBytes = ((size_t) pw * bitsPerPixel + 7) / 8;
		uint8_t* previous = (uint8_t*) calloc(rowBytes, 1);
		for (long y = 0; y < ph; y++)
		{
			int filter = *cursor++;
			uint8_t* row = cursor;
			for (size_t i = 0; i < rowBytes; i++)
			{
				int a = i >= (size_t) filterBytes ? row[i - filterBytes] : 0;
				int b = previous[i];
				int c = i >= (size_t) filterBytes ? previous[i - filterBytes] : 0;
				switch (filter)
				{
					case 1: row[i] = (uint8_t) (row[i] + a); break;
					case 2: row[i] = (uint8_t) (row[i] + b); break;
					case 3: row[i] = (uint8_t) (row[i] + ((a + b) >> 1)); break;
					case 4: row[i] = (uint8_t) (row[i] + PaethOf(a, b, c)); break;
					default: break;
				}
			}
			memcpy(previous, row, rowBytes);
			cursor += rowBytes;
			long iy = interlace ? kY0[p] + y * kDY[p] : y;
			for (long x = 0; x < pw; x++)
			{
				long ix = interlace ? kX0[p] + x * kDX[p] : x;
				unsigned r, g, b, alpha = 255;
				bool keyed = false;
				if (colourType == 3)
				{
					unsigned index = SampleOf(row, x, depth);
					r = palette[index][0], g = palette[index][1], b = palette[index][2], alpha = palette[index][3];
				}
				else if (colourType == 0 || colourType == 4)
				{
					unsigned v = SampleOf(row, x * channels, depth);
					keyed = hasKey && v == key[0];
					r = g = b = v * 255 / maxValue;
					if (colourType == 4)
						alpha = SampleOf(row, x * channels + 1, depth) * 255 / maxValue;
				}
				else
				{
					unsigned rv = SampleOf(row, x * channels, depth), gv = SampleOf(row, x * channels + 1, depth),
						bv = SampleOf(row, x * channels + 2, depth);
					keyed = hasKey && rv == key[0] && gv == key[1] && bv == key[2];
					r = rv * 255 / maxValue, g = gv * 255 / maxValue, b = bv * 255 / maxValue;
					if (colourType == 6)
						alpha = SampleOf(row, x * channels + 3, depth) * 255 / maxValue;
				}
				if (keyed)
					alpha = 0;
				if (alpha != 255)
				{
					// (over white: the Newton's paper)
					r = (r * alpha + 255 * (255 - alpha) + 127) / 255;
					g = (g * alpha + 255 * (255 - alpha) + 127) / 255;
					b = (b * alpha + 255 * (255 - alpha) + 127) / 255;
				}
				uint8_t* px = out->rgb + ((size_t) iy * width + ix) * 3;
				px[0] = (uint8_t) r, px[1] = (uint8_t) g, px[2] = (uint8_t) b;
			}
		}
		free(previous);
	}
	free(raw);
	return 0;
}


// ---------------------------------------------------------------------
// JPEG (baseline)
// ---------------------------------------------------------------------

static const uint8_t kNatural[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

struct JHuffman
{
	bool		defined;
	int			maxCode[18];	// the largest code of each length, -1 none
	int			valPtr[17];
	int			minCode[17];
	uint8_t		values[256];
};

struct JComponent
{
	int			id, h, v, tq, td, ta;
	int			blocksWide, blocksHigh;		// in the plane
	uint8_t*	plane;
	int			predictor;
};

struct JReader
{
	const uint8_t*	data;
	size_t			size;
	size_t			pos;
	uint32_t		buffer;
	int				count;
	bool			marker;		// a marker reached: zeros from here
};

static int
JBit(JReader* r)
{
	if (r->count == 0)
	{
		uint8_t byte = 0;
		if (!r->marker && r->pos < r->size)
		{
			byte = r->data[r->pos];
			if (byte == 0xFF)
			{
				uint8_t next = r->pos + 1 < r->size ? r->data[r->pos + 1] : 0xD9;
				if (next == 0x00)
					r->pos += 2;
				else
				{
					r->marker = true;
					byte = 0;
				}
			}
			else
				r->pos++;
		}
		r->buffer = byte;
		r->count = 8;
	}
	r->count--;
	return (r->buffer >> r->count) & 1;
}

static int
JBits(JReader* r, int n)
{
	int v = 0;
	while (n-- > 0)
		v = (v << 1) | JBit(r);
	return v;
}

static int
JDecode(JReader* r, const JHuffman* h)
{
	int code = JBit(r);
	int length = 1;
	while (length <= 16 && code > h->maxCode[length])
	{
		code = (code << 1) | JBit(r);
		length++;
	}
	if (length > 16)
		return 0;		// (damaged: carry on with nothing)
	return h->values[h->valPtr[length] + code - h->minCode[length]];
}

static int
JExtend(int v, int s)
{
	return s == 0 ? 0 : v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}

static void
IDCT(const int* coefficients, uint8_t* out, int stride)
{
	static float sCos[8][8];
	static bool sMade = false;
	if (!sMade)
	{
		for (int x = 0; x < 8; x++)
			for (int u = 0; u < 8; u++)
				sCos[x][u] = (float) ((u == 0 ? sqrt(0.5) : 1.0) * cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0) / 2.0);
		sMade = true;
	}
	float rows[64];
	for (int v = 0; v < 8; v++)
		for (int x = 0; x < 8; x++)
		{
			float sum = 0;
			for (int u = 0; u < 8; u++)
				sum += sCos[x][u] * (float) coefficients[v * 8 + u];
			rows[v * 8 + x] = sum;
		}
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
		{
			float sum = 0;
			for (int v = 0; v < 8; v++)
				sum += sCos[y][v] * rows[v * 8 + x];
			int value = (int) floorf(sum + 128.5f);
			out[y * stride + x] = (uint8_t) (value < 0 ? 0 : value > 255 ? 255 : value);
		}
}

static int
DecodeJPEG(const uint8_t* data, size_t size, HostImage* out, const char** why)
{
	if (size < 4 || data[0] != 0xFF || data[1] != 0xD8)
		return *why = "not a JPEG", -1;
	uint16_t quant[4][64];
	JHuffman dc[4], ac[4];
	memset(quant, 0, sizeof(quant));
	for (int i = 0; i < 4; i++)
		dc[i].defined = ac[i].defined = false;
	JComponent comps[3];
	int ncomp = 0, width = 0, height = 0, interval = 0;
	bool framed = false, done = false;
	size_t pos = 2;
	while (!done && pos + 4 <= size)
	{
		if (data[pos] != 0xFF)
		{
			pos++;
			continue;
		}
		uint8_t marker = data[pos + 1];
		if (marker == 0xFF || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
		{
			pos++;
			continue;
		}
		if (marker == 0xD9)
			break;
		size_t length = (size_t) ((data[pos + 2] << 8) | data[pos + 3]);
		const uint8_t* body = data + pos + 4;
		if (length < 2 || pos + 2 + length > size)
			return *why = "a JPEG cut short", -1;
		size_t bodySize = length - 2;
		if (marker == 0xC0 || marker == 0xC1)
		{
			if (body[0] != 8)
				return *why = "a 12-bit JPEG: NOT YET", -1;
			height = (body[1] << 8) | body[2];
			width = (body[3] << 8) | body[4];
			ncomp = body[5];
			if (!(ncomp == 1 || ncomp == 3) || width == 0 || height == 0)
				return *why = ncomp == 4 ? "a CMYK JPEG: NOT YET" : "a JPEG this decoder does not read", -1;
			for (int i = 0; i < ncomp; i++)
			{
				comps[i].id = body[6 + i * 3];
				comps[i].h = body[7 + i * 3] >> 4;
				comps[i].v = body[7 + i * 3] & 15;
				comps[i].tq = body[8 + i * 3] & 3;
				comps[i].plane = NULL;
				if (comps[i].h < 1 || comps[i].h > 4 || comps[i].v < 1 || comps[i].v > 4)
					return *why = "a JPEG with bad sampling", -1;
			}
			if (ncomp == 1)
				comps[0].h = comps[0].v = 1;
			framed = true;
		}
		else if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE)
			return *why = "progressive JPEG: NOT YET", -1;
		else if ((marker >= 0xC3 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
			return *why = "a lossless or arithmetic JPEG: NOT YET", -1;
		else if (marker == 0xC4)
		{
			size_t p = 0;
			while (p + 17 <= bodySize)
			{
				int tc = body[p] >> 4, th = body[p] & 3;
				JHuffman* h = tc == 0 ? &dc[th] : &ac[th];
				int total = 0;
				const uint8_t* counts = body + p + 1;
				for (int l = 0; l < 16; l++)
					total += counts[l];
				if (total > 256 || p + 17 + (size_t) total > bodySize)
					return *why = "a JPEG with a bad table", -1;
				memcpy(h->values, body + p + 17, (size_t) total);
				int code = 0, k = 0;
				for (int l = 1; l <= 16; l++)
				{
					h->valPtr[l] = k;
					h->minCode[l] = code;
					code += counts[l - 1];
					k += counts[l - 1];
					h->maxCode[l] = counts[l - 1] ? code - 1 : -1;
					code <<= 1;
				}
				h->maxCode[17] = 0x7FFFFFFF;
				h->defined = true;
				p += 17 + (size_t) total;
			}
		}
		else if (marker == 0xDB)
		{
			size_t p = 0;
			while (p < bodySize)
			{
				int pq = body[p] >> 4, tq = body[p] & 3;
				p++;
				for (int k = 0; k < 64 && p < bodySize; k++)
				{
					quant[tq][k] = pq ? (uint16_t) ((body[p] << 8) | body[p + 1]) : body[p];
					p += pq ? 2 : 1;
				}
			}
		}
		else if (marker == 0xDD)
			interval = (body[0] << 8) | body[1];
		else if (marker == 0xDA)
		{
			if (!framed)
				return *why = "a JPEG scan before its frame", -1;
			int ns = body[0];
			if (ns != ncomp)
				return *why = "a JPEG in several scans: NOT YET", -1;
			for (int i = 0; i < ns; i++)
			{
				int id = body[1 + i * 2];
				for (int c = 0; c < ncomp; c++)
					if (comps[c].id == id)
						comps[c].td = body[2 + i * 2] >> 4, comps[c].ta = body[2 + i * 2] & 3;
			}
			for (int c = 0; c < ncomp; c++)
				if (!dc[comps[c].td].defined || !ac[comps[c].ta].defined)
					return *why = "a JPEG without its tables", -1;
			int hmax = 1, vmax = 1;
			for (int c = 0; c < ncomp; c++)
				hmax = comps[c].h > hmax ? comps[c].h : hmax, vmax = comps[c].v > vmax ? comps[c].v : vmax;
			int mcusWide = (width + 8 * hmax - 1) / (8 * hmax);
			int mcusHigh = (height + 8 * vmax - 1) / (8 * vmax);
			for (int c = 0; c < ncomp; c++)
			{
				comps[c].blocksWide = mcusWide * comps[c].h;
				comps[c].blocksHigh = mcusHigh * comps[c].v;
				comps[c].plane = (uint8_t*) malloc((size_t) comps[c].blocksWide * 8 * comps[c].blocksHigh * 8);
				comps[c].predictor = 0;
			}
			JReader reader = { data, size, pos + 2 + length, 0, 0, false };
			int coefficients[64];
			int mcus = 0;
			for (int my = 0; my < mcusHigh; my++)
				for (int mx = 0; mx < mcusWide; mx++)
				{
					if (interval && mcus > 0 && mcus % interval == 0)
					{
						// the restart marker: the bits and predictors begin again
						reader.count = 0;
						reader.marker = false;
						while (reader.pos + 1 < size && !(data[reader.pos] == 0xFF && data[reader.pos + 1] >= 0xD0 && data[reader.pos + 1] <= 0xD7))
							reader.pos++;
						reader.pos += 2;
						for (int c = 0; c < ncomp; c++)
							comps[c].predictor = 0;
					}
					for (int c = 0; c < ncomp; c++)
					{
						JComponent* comp = &comps[c];
						for (int by = 0; by < comp->v; by++)
							for (int bx = 0; bx < comp->h; bx++)
							{
								memset(coefficients, 0, sizeof(coefficients));
								int t = JDecode(&reader, &dc[comp->td]);
								int diff = t ? JExtend(JBits(&reader, t), t) : 0;
								comp->predictor += diff;
								coefficients[0] = comp->predictor * quant[comp->tq][0];
								for (int k = 1; k < 64; )
								{
									int rs = JDecode(&reader, &ac[comp->ta]);
									int run = rs >> 4, s = rs & 15;
									if (s == 0)
									{
										if (run != 15)
											break;
										k += 16;
										continue;
									}
									k += run;
									if (k > 63)
										break;
									coefficients[kNatural[k]] = JExtend(JBits(&reader, s), s) * quant[comp->tq][k];
									k++;
								}
								int stride = comp->blocksWide * 8;
								IDCT(coefficients, comp->plane + ((size_t) (my * comp->v + by) * 8) * stride + (mx * comp->h + bx) * 8, stride);
							}
					}
					mcus++;
				}
			// the colours: a plane sampled less often than the picture is
			// filled in between its samples (centred, bilinear - what
			// libjpeg's "fancy upsampling" does for twice), out to the
			// plane's own edge
			out->width = width;
			out->height = height;
			out->rgb = (uint8_t*) malloc((size_t) width * height * 3);
			for (int y = 0; y < height; y++)
				for (int x = 0; x < width; x++)
				{
					uint8_t* px = out->rgb + ((size_t) y * width + x) * 3;
					float values[3];
					for (int c = 0; c < ncomp; c++)
					{
						const JComponent* comp = &comps[c];
						size_t stride = (size_t) comp->blocksWide * 8;
						if (comp->h == hmax && comp->v == vmax)
						{
							values[c] = comp->plane[(size_t) y * stride + x];
							continue;
						}
						int lastX = (width * comp->h + hmax - 1) / hmax - 1;
						int lastY = (height * comp->v + vmax - 1) / vmax - 1;
						float fx = (x + 0.5f) * comp->h / hmax - 0.5f;
						float fy = (y + 0.5f) * comp->v / vmax - 0.5f;
						int x0 = (int) floorf(fx), y0 = (int) floorf(fy);
						float ax = fx - x0, ay = fy - y0;
						int x1 = x0 + 1, y1 = y0 + 1;
						x0 = x0 < 0 ? 0 : x0 > lastX ? lastX : x0;
						x1 = x1 < 0 ? 0 : x1 > lastX ? lastX : x1;
						y0 = y0 < 0 ? 0 : y0 > lastY ? lastY : y0;
						y1 = y1 < 0 ? 0 : y1 > lastY ? lastY : y1;
						float top = comp->plane[(size_t) y0 * stride + x0] * (1 - ax) + comp->plane[(size_t) y0 * stride + x1] * ax;
						float bottom = comp->plane[(size_t) y1 * stride + x0] * (1 - ax) + comp->plane[(size_t) y1 * stride + x1] * ax;
						values[c] = top * (1 - ay) + bottom * ay;
					}
					int Y = (int) floorf(values[0] + 0.5f);
					if (ncomp == 1)
					{
						px[0] = px[1] = px[2] = (uint8_t) Y;
						continue;
					}
					float cb = values[1] - 128.0f;
					float cr = values[2] - 128.0f;
					float Yf = values[0];
					float rgb[3] = { Yf + 1.402f * cr, Yf - 0.344136f * cb - 0.714136f * cr, Yf + 1.772f * cb };
					for (int k = 0; k < 3; k++)
					{
						int v = (int) floorf(rgb[k] + 0.5f);
						px[k] = (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v);
					}
				}
			for (int c = 0; c < ncomp; c++)
				free(comps[c].plane);
			return 0;
		}
		pos += 2 + length;
	}
	return *why = "a JPEG without a picture", -1;
}

} // namespace


// ---------------------------------------------------------------------

HostImageKind
HostImageKindOf(const uint8_t* data, size_t size)
{
	if (size >= 8 && memcmp(data, kPNGSignature, 8) == 0)
		return kHostImagePNG;
	if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
		return kHostImageJPEG;
	return kHostImageUnknown;
}

int
HostImageSize(const uint8_t* data, size_t size, int* width, int* height, const char** why)
{
	switch (HostImageKindOf(data, size))
	{
		case kHostImagePNG:
			if (size < 24)
				break;
			*width = (int) Big32(data + 16);
			*height = (int) Big32(data + 20);
			return 0;
		case kHostImageJPEG:
			for (size_t pos = 2; pos + 9 < size; )
			{
				if (data[pos] != 0xFF)
				{
					pos++;
					continue;
				}
				uint8_t marker = data[pos + 1];
				if (marker == 0xFF || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8))
				{
					pos++;
					continue;
				}
				if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
				{
					*height = (data[pos + 5] << 8) | data[pos + 6];
					*width = (data[pos + 7] << 8) | data[pos + 8];
					return 0;
				}
				pos += 2 + (size_t) ((data[pos + 2] << 8) | data[pos + 3]);
			}
			break;
		default:
			return *why = "not a picture this host reads", -1;
	}
	return *why = "a picture cut short", -1;
}

int
HostImageDecode(const uint8_t* data, size_t size, HostImage* out, const char** why)
{
	out->width = out->height = 0;
	out->rgb = NULL;
	const char* ignored;
	if (why == NULL)
		why = &ignored;
	switch (HostImageKindOf(data, size))
	{
		case kHostImagePNG: return DecodePNG(data, size, out, why);
		case kHostImageJPEG: return DecodeJPEG(data, size, out, why);
		default: return *why = "not a picture this host reads", -1;
	}
}

void
HostImageFree(HostImage* image)
{
	free(image->rgb);
	image->rgb = NULL;
}

void
HostImageFit(const HostImage* image, int boxWidth, int boxHeight, int enlarge, int* width, int* height)
{
	if (!enlarge && image->width <= boxWidth && image->height <= boxHeight)
	{
		*width = image->width;
		*height = image->height;
		return;
	}
	// the smaller of the two scales
	if ((long long) boxWidth * image->height <= (long long) boxHeight * image->width)
	{
		*width = boxWidth;
		*height = (int) (((long long) image->height * boxWidth + image->width / 2) / image->width);
	}
	else
	{
		*height = boxHeight;
		*width = (int) (((long long) image->width * boxHeight + image->height / 2) / image->height);
	}
	if (*width < 1) *width = 1;
	if (*height < 1) *height = 1;
}

void
HostImageGrays(const HostImage* image, int width, int height, uint8_t* gray)
{
	for (int ty = 0; ty < height; ty++)
	{
		long y0 = (long) ty * image->height / height;
		long y1 = (long) (ty + 1) * image->height / height;
		if (y1 <= y0) y1 = y0 + 1;
		for (int tx = 0; tx < width; tx++)
		{
			long x0 = (long) tx * image->width / width;
			long x1 = (long) (tx + 1) * image->width / width;
			if (x1 <= x0) x1 = x0 + 1;
			unsigned long long sum = 0;
			for (long y = y0; y < y1; y++)
			{
				const uint8_t* px = image->rgb + ((size_t) y * image->width + x0) * 3;
				for (long x = x0; x < x1; x++, px += 3)
					sum += 299u * px[0] + 587u * px[1] + 114u * px[2];
			}
			unsigned long long n = (unsigned long long) (x1 - x0) * (y1 - y0) * 1000u;
			unsigned luminance = (unsigned) ((sum + n / 2) / n);
			gray[(size_t) ty * width + tx] = (uint8_t) (255 - luminance);
		}
	}
}

void
HostImagePack(const uint8_t* gray, int width, int height, int depth, uint8_t* rows, long rowBytes)
{
	memset(rows, 0, (size_t) rowBytes * height);
	if (depth == 1)
	{
		// Floyd-Steinberg: the error carried right and down
		int* errors = (int*) calloc((size_t) (width + 2) * 2, sizeof(int));
		int* here = errors + 1;
		int* below = errors + width + 3;
		for (int y = 0; y < height; y++)
		{
			for (int x = 0; x < width; x++)
			{
				int want = gray[(size_t) y * width + x] + here[x] / 16;
				int black = want >= 128;
				int error = want - (black ? 255 : 0);
				if (black)
					rows[(size_t) y * rowBytes + (x >> 3)] |= (uint8_t) (0x80 >> (x & 7));
				here[x + 1] += error * 7;
				below[x - 1] += error * 3;
				below[x] += error * 5;
				below[x + 1] += error;
			}
			int* t = here;
			here = below;
			below = t;
			memset(below - 1, 0, sizeof(int) * (size_t) (width + 2));
		}
		free(errors);
		return;
	}
	int levels = (1 << depth) - 1;
	int perByte = 8 / depth;
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++)
		{
			int level = (gray[(size_t) y * width + x] * levels + 127) / 255;
			int shift = 8 - depth * (x % perByte + 1);
			rows[(size_t) y * rowBytes + x / perByte] |= (uint8_t) (level << shift);
		}
}
