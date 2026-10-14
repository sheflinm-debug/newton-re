/*
	test_ArabicNet: the host's Arabic network (recognition/ArabicNet.h)
	held to the Python model it came from.

	Wants a network and its check sample, as tools/arabic-ink/export_weights.py
	writes them (an .anet and a .sample.bin): the sample's strokes made
	features by the C++ must match the features Python made, the logits
	the C++ network gives for Python's features must match onnxruntime's,
	and the greedy reading must be the same text.

	  test_ArabicNet <model>.anet <model>.sample.bin
*/

#include "ArabicNet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static unsigned long
U32(FILE* f)
{
	unsigned char b[4];
	if (fread(b, 1, 4, f) != 4)
		return 0;
	return (unsigned long) b[0] | ((unsigned long) b[1] << 8) | ((unsigned long) b[2] << 16) | ((unsigned long) b[3] << 24);
}


static float
F32(FILE* f)
{
	float v = 0;
	if (fread(&v, 4, 1, f) != 1)
		return 0;
	return v;
}


int
main(int argc, char** argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: test_ArabicNet <model>.anet <model>.sample.bin\n");
		return 2;
	}
	ArabicNet* net = ArabicNetLoad(argv[1]);
	EXPECT(net != nil);
	if (net == nil)
		return 1;
	long F = ArabicNetFeatureCount(net);
	long C = ArabicNetClassCount(net);
	fprintf(stderr, "network: %ld features, %ld classes\n", F, C);

	FILE* f = fopen(argv[2], "rb");
	EXPECT(f != nil);
	if (f == nil)
		return 1;
	// the strokes
	long nStrokes = (long) U32(f);
	long* counts = (long*) malloc(nStrokes * sizeof(long));
	long total = 0;
	float* xy = nil;
	for (long s = 0; s < nStrokes; s++)
	{
		counts[s] = (long) U32(f);
		xy = (float*) realloc(xy, (total + counts[s]) * 2 * sizeof(float));
		for (long i = 0; i < counts[s]; i++)
		{
			xy[(total + i) * 2] = F32(f);
			xy[(total + i) * 2 + 1] = F32(f);
		}
		total += counts[s];
	}
	// Python's features
	long T = (long) U32(f);
	long F2 = (long) U32(f);
	EXPECT(F2 == F);
	float* pyFeats = (float*) malloc(T * F * sizeof(float));
	for (long i = 0; i < T * F; i++)
		pyFeats[i] = F32(f);
	// Python's logits
	long T2 = (long) U32(f);
	long C2 = (long) U32(f);
	EXPECT(T2 == T && C2 == C);
	float* pyLogits = (float*) malloc(T * C * sizeof(float));
	for (long i = 0; i < T * C; i++)
		pyLogits[i] = F32(f);
	long n = (long) U32(f);
	UniChar* expected = (UniChar*) calloc(n + 1, sizeof(UniChar));
	for (long i = 0; i < n; i++)
	{
		unsigned char b[2];
		if (fread(b, 1, 2, f) == 2)
			expected[i] = (UniChar) (b[0] | (b[1] << 8));
	}
	fclose(f);

	// 1. the features
	long room = ArabicNetStepsFor(xy, counts, nStrokes, 0);
	float* feats = (float*) malloc(room * F * sizeof(float));
	long steps = ArabicNetFeaturize(net, xy, counts, nStrokes, feats, room);
	fprintf(stderr, "features: C++ %ld steps, Python %ld\n", steps, T);
	EXPECT(steps == T);
	double worst = 0;
	if (steps == T)
		for (long i = 0; i < T * F; i++)
		{
			double d = fabs((double) feats[i] - pyFeats[i]);
			if (d > worst)
				worst = d;
		}
	fprintf(stderr, "features: the largest difference %g\n", worst);
	EXPECT(worst < 1e-4);

	// 2. the network over Python's features
	float* logits = (float*) malloc(T * C * sizeof(float));
	EXPECT(ArabicNetRun(net, pyFeats, T, logits) == 0);
	worst = 0;
	for (long i = 0; i < T * C; i++)
	{
		double d = fabs((double) logits[i] - pyLogits[i]);
		if (d > worst)
			worst = d;
	}
	fprintf(stderr, "logits: the largest difference %g\n", worst);
	EXPECT(worst < 1e-3);

	// 3. the reading
	UniChar text[64];
	long len = ArabicNetGreedy(net, logits, T, text, 64);
	fprintf(stderr, "reading: %ld characters, Python's %ld\n", len, n);
	EXPECT(len == n);
	bool same = len == n;
	for (long i = 0; same && i < n; i++)
		same = text[i] == expected[i];
	EXPECT(same);
	// and the same from the C++'s own features
	if (steps == T)
	{
		EXPECT(ArabicNetRun(net, feats, steps, logits) == 0);
		len = ArabicNetGreedy(net, logits, steps, text, 64);
		same = len == n;
		for (long i = 0; same && i < n; i++)
			same = text[i] == expected[i];
		EXPECT(same);
	}

	ArabicNetFree(net);
	fprintf(stderr, failures == 0 ? "test_ArabicNet: ok\n" : "test_ArabicNet: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
