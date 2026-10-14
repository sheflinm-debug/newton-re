/*
	File:		recognition/ArabicNet.cpp

	Contains:	The host's Arabic word reader (ArabicNet.h): the .anet
				file read, the features made as tools/arabic-ink/train_net.py
				makes them, the GRU run as ONNX defines it, the greedy
				decoding.

	DEVIATION: all of it.
*/

#include "ArabicNet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ArabicNet
{
	long		features;
	long		hidden;
	long		classes;
	float		step;
	UniChar*	alphabet;		// classes
	float*		weights;		// every array, one after another
	// the arrays, pointers into weights
	const float* inpW;		// [features, hidden]
	const float* inpB;		// [hidden]
	const float* gru1W;		// [2, 3H, hidden]
	const float* gru1R;		// [2, 3H, H]
	const float* gru1B;		// [2, 6H]
	const float* gru2W;		// [2, 3H, 2H]
	const float* gru2R;		// [2, 3H, H]
	const float* gru2B;		// [2, 6H]
	const float* outW;		// [2H, classes]
	const float* outB;		// [classes]
};


/*------------------------------------------------------------------------------
	T h e   f i l e
------------------------------------------------------------------------------*/

static bool
ReadU32(FILE* f, unsigned long* out)
{
	unsigned char b[4];
	if (fread(b, 1, 4, f) != 4)
		return false;
	*out = (unsigned long) b[0] | ((unsigned long) b[1] << 8) | ((unsigned long) b[2] << 16) | ((unsigned long) b[3] << 24);
	return true;
}


static bool
ReadFloats(FILE* f, float* out, long count)
{
	// little-endian float32, as the host writes them
	unsigned char* bytes = (unsigned char*) out;
	if (fread(bytes, 4, count, f) != (size_t) count)
		return false;
	// (a big-endian host would swap here; every host this runs on is little-endian)
	return true;
}


ArabicNet*
ArabicNetLoad(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (f == nil)
	{
		fprintf(stderr, "[arabic] cannot open the network %s\n", path);
		return nil;
	}
	char magic[4];
	unsigned long version, features, hidden, classes;
	float step;
	unsigned char stepBytes[4];
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ANET", 4) != 0
	 || !ReadU32(f, &version) || version != 1
	 || !ReadU32(f, &features) || !ReadU32(f, &hidden) || !ReadU32(f, &classes)
	 || fread(stepBytes, 1, 4, f) != 4)
	{
		fprintf(stderr, "[arabic] %s is not a network file\n", path);
		fclose(f);
		return nil;
	}
	memcpy(&step, stepBytes, 4);
	ArabicNet* net = (ArabicNet*) calloc(1, sizeof(ArabicNet));
	net->features = (long) features;
	net->hidden = (long) hidden;
	net->classes = (long) classes;
	net->step = step;
	net->alphabet = (UniChar*) calloc(classes, sizeof(UniChar));
	for (unsigned long i = 0; i < classes; i++)
	{
		unsigned char b[2];
		if (fread(b, 1, 2, f) != 2)
			break;
		net->alphabet[i] = (UniChar) (b[0] | (b[1] << 8));
	}
	long H = net->hidden, F = net->features, C = net->classes;
	long sizes[10] = { F * H, H, 2 * 3 * H * H, 2 * 3 * H * H, 2 * 6 * H, 2 * 3 * H * 2 * H, 2 * 3 * H * H, 2 * 6 * H, 2 * H * C, C };
	long total = 0;
	for (int i = 0; i < 10; i++)
		total += sizes[i];
	net->weights = (float*) malloc(total * sizeof(float));
	if (net->weights == nil || !ReadFloats(f, net->weights, total))
	{
		fprintf(stderr, "[arabic] %s is cut short\n", path);
		fclose(f);
		ArabicNetFree(net);
		return nil;
	}
	fclose(f);
	const float* p = net->weights;
	const float** slots[10] = { &net->inpW, &net->inpB, &net->gru1W, &net->gru1R, &net->gru1B, &net->gru2W, &net->gru2R, &net->gru2B, &net->outW, &net->outB };
	for (int i = 0; i < 10; i++)
	{
		*slots[i] = p;
		p += sizes[i];
	}
	return net;
}


void
ArabicNetFree(ArabicNet* net)
{
	if (net == nil)
		return;
	free(net->alphabet);
	free(net->weights);
	free(net);
}


long	ArabicNetFeatureCount(const ArabicNet* net)		{ return net->features; }
long	ArabicNetClassCount(const ArabicNet* net)		{ return net->classes; }
UniChar	ArabicNetClassChar(const ArabicNet* net, long cls)	{ return cls > 0 && cls < net->classes ? net->alphabet[cls] : 0; }


/*------------------------------------------------------------------------------
	T h e   f e a t u r e s

	As train_net.py's featurize: the word's height is the 10th to 90th
	percentile of y, its base line the 75th; x runs right to left from the
	word's right edge (the mirror), y up from the base line, both in
	heights; each stroke is resampled to an even step along its length;
	a step's features are dx, dy, 1, first-of-stroke, last-of-stroke,
	-y.  The percentile is Python's: the sorted values indexed at
	round(q * (n - 1)) - with round half to even, as Python rounds.
------------------------------------------------------------------------------*/

static int
CompareFloats(const void* a, const void* b)
{
	float x = *(const float*) a, y = *(const float*) b;
	return x < y ? -1 : (x > y ? 1 : 0);
}


static long
RoundHalfEven(double v)
{
	double f = floor(v);
	double d = v - f;
	if (d > 0.5)
		return (long) f + 1;
	if (d < 0.5)
		return (long) f;
	return ((long) f % 2 == 0) ? (long) f : (long) f + 1;
}


static float
Percentile(float* sorted, long n, double q)
{
	long i = RoundHalfEven(q * (n - 1));
	if (i < 0) i = 0;
	if (i > n - 1) i = n - 1;
	return sorted[i];
}


long
ArabicNetStepsFor(const float* xy, const long* counts, long nStrokes, float step)
{
	// each stroke: its first point, one a step along its length, its last
	long steps = 0;
	for (long s = 0, k = 0; s < nStrokes; k += counts[s], s++)
		steps += counts[s] * 2 + 2;		// (more than enough: a point is at most a step and a bit)
	(void) xy; (void) step;
	return steps;
}


long
ArabicNetFeaturize(const ArabicNet* net, const float* xy, const long* counts, long nStrokes, float* out, long room)
{
	long total = 0;
	for (long s = 0; s < nStrokes; s++)
		total += counts[s];
	if (total < 2)
		return 0;
	float* ys = (float*) malloc(total * sizeof(float));
	float right = -1e30f;
	for (long i = 0; i < total; i++)
	{
		ys[i] = xy[i * 2 + 1];
		if (xy[i * 2] > right)
			right = xy[i * 2];
	}
	qsort(ys, total, sizeof(float), CompareFloats);
	float top = Percentile(ys, total, 0.1), base = Percentile(ys, total, 0.9), baseline = Percentile(ys, total, 0.75);
	free(ys);
	float height = base - top;
	if (height < 1.0f)
		height = 1.0f;
	const float STEP = net->step;
	long F = net->features;
	long n = 0;
	bool havePrev = false;
	float prevX = 0, prevY = 0;
	const float* p = xy;
	for (long s = 0; s < nStrokes; p += counts[s] * 2, s++)
	{
		long count = counts[s];
		if (count == 0)
			continue;
		// the stroke's points, mirrored and scaled, resampled along its length
		// (emitted as they are made: first the first point, then one a step,
		// then the last if it was not the last emitted)
		float ax = (right - p[0]) / height, ay = (p[1] - baseline) / height;
		// gather the resampled points of this stroke first, to know the last
		long cap = count * 2 + 2;
		float* rx = (float*) malloc(cap * sizeof(float));
		float* ry = (float*) malloc(cap * sizeof(float));
		long m = 0;
		rx[m] = ax; ry[m] = ay; m++;
		double acc = 0.0;
		for (long i = 1; i < count; i++)
		{
			float bx = (right - p[i * 2]) / height, by = (p[i * 2 + 1] - baseline) / height;
			double d = hypot(bx - ax, by - ay);
			if (d == 0.0)
			{
				continue;
			}
			acc += d;
			while (acc >= STEP)
			{
				acc -= STEP;
				double t = 1.0 - acc / d;
				if (m < cap)
				{
					rx[m] = (float) (ax + (bx - ax) * t);
					ry[m] = (float) (ay + (by - ay) * t);
					m++;
				}
			}
			ax = bx; ay = by;
		}
		float lx = (right - p[(count - 1) * 2]) / height, ly = (p[(count - 1) * 2 + 1] - baseline) / height;
		if (m == 0 || rx[m - 1] != lx || ry[m - 1] != ly)
		{
			if (m < cap)
			{
				rx[m] = lx; ry[m] = ly; m++;
			}
		}
		for (long k = 0; k < m; k++)
		{
			if (n >= room)
			{
				free(rx); free(ry);
				return room + 1;
			}
			float* o = out + n * F;
			float dx = havePrev ? rx[k] - prevX : 0.0f;
			float dy = havePrev ? ry[k] - prevY : 0.0f;
			o[0] = dx;
			o[1] = dy;
			o[2] = 1.0f;
			o[3] = k == 0 ? 1.0f : 0.0f;
			o[4] = k == m - 1 ? 1.0f : 0.0f;
			o[5] = -ry[k];
			prevX = rx[k]; prevY = ry[k];
			havePrev = true;
			n++;
		}
		free(rx); free(ry);
	}
	return n;
}


/*------------------------------------------------------------------------------
	T h e   n e t w o r k
------------------------------------------------------------------------------*/

static inline float	Sigmoid(float x)	{ return 1.0f / (1.0f + expf(-x)); }


// One direction of one GRU layer over `steps` steps of `in` (inWidth
// each): h written for each step into out (steps x 2H, this direction's
// half).  W [3H, inWidth], R [3H, H], B [6H]; gates z, r, h.
static void
RunGRU(const float* W, const float* R, const float* B, long inWidth, long H, const float* in, long steps, float* out, long outWidth, long outOffset, bool backward, float* gates, float* h)
{
	memset(h, 0, H * sizeof(float));
	for (long k = 0; k < steps; k++)
	{
		long t = backward ? steps - 1 - k : k;
		const float* x = in + t * inWidth;
		// gates[0..3H): W x + Wb;  gates[3H..6H): R h + Rb
		for (long g = 0; g < 3 * H; g++)
		{
			const float* w = W + g * inWidth;
			float acc = B[g];
			for (long i = 0; i < inWidth; i++)
				acc += w[i] * x[i];
			gates[g] = acc;
			const float* r = R + g * H;
			float acc2 = B[3 * H + g];
			for (long i = 0; i < H; i++)
				acc2 += r[i] * h[i];
			gates[3 * H + g] = acc2;
		}
		float* o = out + t * outWidth + outOffset;
		for (long i = 0; i < H; i++)
		{
			float z = Sigmoid(gates[i] + gates[3 * H + i]);
			float r = Sigmoid(gates[H + i] + gates[3 * H + H + i]);
			float n = tanhf(gates[2 * H + i] + r * gates[3 * H + 2 * H + i]);
			o[i] = (1.0f - z) * n + z * h[i];
		}
		memcpy(h, o, H * sizeof(float));
	}
}


long
ArabicNetRun(const ArabicNet* net, const float* features, long steps, float* logits)
{
	long F = net->features, H = net->hidden, C = net->classes;
	float* a = (float*) malloc(steps * H * sizeof(float));			// after inp
	float* b = (float*) malloc(steps * 2 * H * sizeof(float));		// after gru1
	float* c = (float*) malloc(steps * 2 * H * sizeof(float));		// after gru2
	float* gates = (float*) malloc(6 * H * sizeof(float));
	float* h = (float*) malloc(H * sizeof(float));
	if (a == nil || b == nil || c == nil || gates == nil || h == nil)
	{
		free(a); free(b); free(c); free(gates); free(h);
		return -1;
	}
	for (long t = 0; t < steps; t++)
	{
		const float* x = features + t * F;
		float* o = a + t * H;
		for (long j = 0; j < H; j++)
		{
			float acc = net->inpB[j];
			for (long i = 0; i < F; i++)
				acc += x[i] * net->inpW[i * H + j];
			o[j] = acc > 0.0f ? acc : 0.0f;
		}
	}
	RunGRU(net->gru1W, net->gru1R, net->gru1B, H, H, a, steps, b, 2 * H, 0, false, gates, h);
	RunGRU(net->gru1W + 3 * H * H, net->gru1R + 3 * H * H, net->gru1B + 6 * H, H, H, a, steps, b, 2 * H, H, true, gates, h);
	RunGRU(net->gru2W, net->gru2R, net->gru2B, 2 * H, H, b, steps, c, 2 * H, 0, false, gates, h);
	RunGRU(net->gru2W + 3 * H * 2 * H, net->gru2R + 3 * H * H, net->gru2B + 6 * H, 2 * H, H, b, steps, c, 2 * H, H, true, gates, h);
	for (long t = 0; t < steps; t++)
	{
		const float* x = c + t * 2 * H;
		float* o = logits + t * C;
		for (long j = 0; j < C; j++)
		{
			float acc = net->outB[j];
			for (long i = 0; i < 2 * H; i++)
				acc += x[i] * net->outW[i * C + j];
			o[j] = acc;
		}
	}
	free(a); free(b); free(c); free(gates); free(h);
	return 0;
}


long
ArabicNetGreedy(const ArabicNet* net, const float* logits, long steps, UniChar* out, long room)
{
	long C = net->classes;
	long n = 0;
	long prev = 0;
	for (long t = 0; t < steps; t++)
	{
		const float* row = logits + t * C;
		long best = 0;
		for (long j = 1; j < C; j++)
			if (row[j] > row[best])
				best = j;
		if (best != prev && best != 0 && n < room - 1)
			out[n++] = net->alphabet[best];
		prev = best;
	}
	if (room > 0)
		out[n < room ? n : room - 1] = 0;
	return n;
}
