/*
	File:		recognition/ArabicRecognizer.cpp

	Contains:	`TArabicRecognizer`, the host's Arabic word engine
				(ArabicRecognizer.h).

	DEVIATION: all of it.
*/

#include "ArabicRecognizer.h"
#include "ArabicNet.h"
#include "WordEngines.h"		// kHostWordEngineInterface
#include "WordUnit.h"
#include "Words.h"				// FindBaseline
#include "Stroke.h"
#include "Unit.h"
#include "Protocols.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PROTOCOL_IMPL_SOURCE_MACRO(TArabicRecognizer)
PROTOCOL_CLASSINFO(TArabicRecognizer, kHostWordEngineInterface, "", 0, 0, nil)

// the engine's reading is a word, not typing (WordEngines.h's kHostTypedLabel)
const ULong kArabicWordLabel = 0;
// a reading less sure than this is left as ink (0..100)
const long kArabicGoodConfidence = 20;


#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* module, char* name, unsigned long size);
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

// The directory the running program is in, with its separator; false
// when it cannot be told.  (As host/HostObjectsFile.cpp finds it; this
// library sits below the host's, so it asks for itself.)
static bool
ProgramDirectory(char* dir, size_t size)
{
	dir[0] = 0;
#if defined(_WIN32)
	unsigned long n = GetModuleFileNameA(NULL, dir, (unsigned long) size);
	if (n == 0 || n >= size)
		dir[0] = 0;
#elif defined(__APPLE__)
	uint32_t n = (uint32_t) size;
	if (_NSGetExecutablePath(dir, &n) != 0)
		dir[0] = 0;
#else
	ssize_t n = readlink("/proc/self/exe", dir, size - 1);
	dir[n > 0 ? n : 0] = 0;
#endif
	char* slash = NULL;
	for (char* p = dir; *p; p++)
		if (*p == '/' || *p == '\\')
			slash = p;
	if (slash == NULL)
	{
		dir[0] = 0;
		return false;
	}
	slash[1] = 0;
	return true;
}


static Boolean
Tracing(void)
{
	static int sTracing = -1;
	if (sTracing < 0)
	{
		const char* value = getenv("NEWTON_TRACE_ARABIC");
		sTracing = value != nil && *value == '1';
	}
	return sTracing != 0;
}


// The network, read once: the file NEWTON_ARABIC_NET names, else
// arabic-net.anet beside the program, else build/arabic-ink/arabic-net.anet
// under the working directory (where tools/arabic-ink/export_weights.py
// writes it); nil when there is none (said once).
static ArabicNet*
Network(void)
{
	static ArabicNet* sNet = nil;
	static Boolean sLooked = false;
	if (!sLooked)
	{
		sLooked = true;
		const char* path = getenv("NEWTON_ARABIC_NET");
		if (path != nil && *path != 0)
			sNet = ArabicNetLoad(path);
		else
		{
			const char* tries[] = { "arabic-net.anet", "build/arabic-ink/arabic-net.anet", "../build/arabic-ink/arabic-net.anet" };
			char beside[1024];
			if (ProgramDirectory(beside, sizeof(beside)) && strlen(beside) + 20 < sizeof(beside))
			{
				strcat(beside, "arabic-net.anet");
				FILE* f = fopen(beside, "rb");
				if (f != nil)
				{
					fclose(f);
					sNet = ArabicNetLoad(beside);
				}
			}
			for (unsigned i = 0; sNet == nil && i < sizeof(tries) / sizeof(tries[0]); i++)
			{
				FILE* f = fopen(tries[i], "rb");
				if (f != nil)
				{
					fclose(f);
					sNet = ArabicNetLoad(tries[i]);
				}
			}
			if (sNet == nil)
				fprintf(stderr, "[arabic] no network: NEWTON_ARABIC_NET names none and no arabic-net.anet is beside the program or under build/arabic-ink (the engine reads nothing, so writing stays ink)\n");
		}
		if (sNet != nil)
			fprintf(stderr, "[arabic] the network is loaded\n");
	}
	return sNet;
}


void
RegisterArabicRecognizer(void)
{
	if (gProtocolRegistry != nil)
		TArabicRecognizer::ClassInfo()->Register();
}


TArabicRecognizer*
TArabicRecognizer::New(void)
{
	fConfidence = 0;
	return this;
}


void
TArabicRecognizer::Delete(void)
{
}


void
TArabicRecognizer::Initialize(void)
{
	fConfidence = 0;
	Network();
}


// A stroke offered: it joins the word being built when the pen came down
// again soon enough after that word's last stroke (the domain's delay,
// as the ink-only engine has it); otherwise that word is closed and this
// stroke starts the next.
void
TArabicRecognizer::Group(TStrokeUnit* stroke)
{
	UChar found = 0;
	TWRecUnit* group = (TWRecUnit*) GetPartialGroup(&found);
	if (found != 0 && group != nil)
	{
		ULong gap = GetStartTime(stroke) - GetEndTime(group);
		if (GetStartTime(stroke) >= GetEndTime(group) && gap <= (ULong) fDomain->fDelay)
		{
			AddSub(group, stroke);
			return;
		}
		EndSubs(group);
	}
	MakeNewGroupFromStroke(stroke);
}


// The word's strokes read by the network: its greedy reading put on the
// unit as its one interpretation, scored by how sure the network was
// (the mean, over the steps that read a character, of that character's
// share of the step), 0 the surest.
void
TArabicRecognizer::Read(TWRecUnit* unit)
{
	ArabicNet* net = Network();
	long nStrokes = SubCount(unit);
	if (net == nil || nStrokes < 1)
		return;
	long* counts = (long*) malloc(nStrokes * sizeof(long));
	long total = 0;
	for (long s = 0; s < nStrokes; s++)
	{
		TStrokeUnit* stroke = (TStrokeUnit*) GetSub(unit, s);
		counts[s] = StrokeSize(stroke);
		total += counts[s];
	}
	float* xy = (float*) malloc((total > 0 ? total : 1) * 2 * sizeof(float));
	long at = 0;
	for (long s = 0; s < nStrokes; s++)
	{
		TStrokeUnit* stroke = (TStrokeUnit*) GetSub(unit, s);
		for (long i = 0; i < counts[s]; i++, at++)
		{
			SamplePt* pt = GetSamplePtAddress(stroke, (ULong) i);
			xy[at * 2] = (float) (StrokeSampleX(pt) / 65536.0);
			xy[at * 2 + 1] = (float) (StrokeSampleY(pt) / 65536.0);
		}
	}
	long F = ArabicNetFeatureCount(net);
	long C = ArabicNetClassCount(net);
	long room = ArabicNetStepsFor(xy, counts, nStrokes, 0);
	float* feats = (float*) malloc(room * F * sizeof(float));
	long steps = ArabicNetFeaturize(net, xy, counts, nStrokes, feats, room);
	UniChar text[64];
	long len = 0;
	if (steps > 0 && steps <= room)
	{
		float* logits = (float*) malloc(steps * C * sizeof(float));
		if (ArabicNetRun(net, feats, steps, logits) == 0)
		{
			len = ArabicNetGreedy(net, logits, steps, text, 64);
			// how sure: the softmax share of the best class, over the steps
			// whose best class is a character
			double sum = 0;
			long n = 0;
			for (long t = 0; t < steps; t++)
			{
				const float* row = logits + t * C;
				long best = 0;
				for (long j = 1; j < C; j++)
					if (row[j] > row[best])
						best = j;
				if (best == 0)
					continue;
				double denom = 0;
				for (long j = 0; j < C; j++)
					denom += exp((double) row[j] - row[best]);
				sum += 1.0 / denom;
				n++;
			}
			fConfidence = n > 0 ? (long) (100.0 * sum / n) : 0;
		}
		free(logits);
	}
	free(feats);
	free(xy);
	free(counts);
	if (len > 0)
	{
		long slot = AddWordInterpretation(unit);
		if (slot >= 0)
		{
			SetWordString(unit, (ULong) slot, text);
			SetLabel(unit, (ULong) slot, kArabicWordLabel);
			SetScore(unit, (ULong) slot, (ULong) (1000 - 10 * fConfidence));
		}
	}
	if (Tracing())
	{
		fprintf(stderr, "[arabic] %ld strokes, %ld points, %ld steps: read %ld characters (", nStrokes, total, steps, len);
		for (long i = 0; i < len; i++)
			fprintf(stderr, "%s%04x", i == 0 ? "" : " ", (unsigned) text[i]);
		fprintf(stderr, "), confidence %ld\n", fConfidence);
	}
}


long
TArabicRecognizer::Classify(TWRecUnit* unit)
{
	if (InterpretationCount(unit) == 0)
		Read(unit);
	return 0;
}


long
TArabicRecognizer::Reclassify(TWRecUnit* unit)
{
	Read(unit);
	return 0;
}


// where the writing stands: the word recogniser's own baseline finder
long
TArabicRecognizer::FindBaseline(TStroke** strokes, Point* out)
{
	return ::FindBaseline(strokes, out);
}


void	TArabicRecognizer::GroupInkStroke(TStrokeUnit*, ULong, ULong, Boolean)	{ }
long	TArabicRecognizer::AreaInfoGetSize(void)			{ return 0; }
void	TArabicRecognizer::AreaInfoFillDefaults(Handle)		{ }
void	TArabicRecognizer::AreaInfoConfigure(Handle, RefArg)	{ }
void	TArabicRecognizer::AreaInfoFreeDependents(Handle)	{ }
void	TArabicRecognizer::AreaInfoSetParameters(Handle)	{ }
void	TArabicRecognizer::UnitInfoFreePtr(char*)			{ }
Boolean	TArabicRecognizer::VerifyWordSymbols(UniChar*)		{ return true; }


// a word read surely enough is a word; anything else is left as ink
long
TArabicRecognizer::UnitConfidence(TWRecUnit* unit)
{
	if (InterpretationCount(unit) == 0)
		return kWRecInk;
	return (long) (1000 - GetScore(unit, 0)) / 10 >= kArabicGoodConfidence ? kWRecWord : kWRecInk;
}


void	TArabicRecognizer::Sleep(void)						{ }
void	TArabicRecognizer::WakeUp(void)						{ }
