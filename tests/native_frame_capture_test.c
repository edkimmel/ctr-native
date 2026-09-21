#include <platform/native_frame_capture.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int Apply(int argc, char *argv[], struct NativeFrameCaptureConfig *config)
{
	return NativeFrameCapture_ApplyArgs(argc, argv, config);
}

static int PathMatches(const struct NativeFrameCaptureConfig *config, int frame, const char *expected)
{
	const char *path = NativeFrameCapture_PathForFrame(config, frame);

	return (path != NULL) && (strcmp(path, expected) == 0);
}

int main(void)
{
	struct NativeFrameCaptureConfig config;
	struct NativeFrameCaptureConfig snapshot;
	char *noArgs[] = {"ctr_native"};
	char *spaceForm[] = {"ctr_native", "--capture-frame", "30=out.bmp"};
	char *equalsForm[] = {"ctr_native", "--capture-frame=30=out.bmp"};
	char *multiple[] = {"ctr_native", "--capture-frame", "30=a.bmp", "--capture-frame=90=b.bmp", "--capture-frame", "700=c.bmp"};
	char *duplicate[] = {"ctr_native", "--capture-frame", "30=first.bmp", "--capture-frame", "30=second.bmp"};
	char *exitSpace[] = {"ctr_native", "--exit-after-frame", "700"};
	char *exitEquals[] = {"ctr_native", "--exit-after-frame=700"};
	char *combined[] = {"ctr_native", "--render-scale", "8", "--windowed", "--capture-frame", "30=shots/frame30.bmp", "--perf", "--exit-after-frame", "31"};
	char *missingValue[] = {"ctr_native", "--capture-frame"};
	char *optionInsteadOfValue[] = {"ctr_native", "--capture-frame", "--perf"};
	char *noSeparator[] = {"ctr_native", "--capture-frame", "30"};
	char *emptyFrame[] = {"ctr_native", "--capture-frame", "=out.bmp"};
	char *emptyPath[] = {"ctr_native", "--capture-frame", "30="};
	char *zeroFrame[] = {"ctr_native", "--capture-frame", "0=out.bmp"};
	char *negativeFrame[] = {"ctr_native", "--capture-frame", "-5=out.bmp"};
	char *alphaFrame[] = {"ctr_native", "--capture-frame", "abc=out.bmp"};
	char *overflowFrame[] = {"ctr_native", "--capture-frame", "999999999999999999999=out.bmp"};
	char *exitMissing[] = {"ctr_native", "--exit-after-frame"};
	char *exitZero[] = {"ctr_native", "--exit-after-frame", "0"};
	char *exitAlpha[] = {"ctr_native", "--exit-after-frame", "xyz"};
	char tooManyStorage[NATIVE_FRAME_CAPTURE_MAX_REQUESTS + 1][32];
	char *tooMany[1 + ((NATIVE_FRAME_CAPTURE_MAX_REQUESTS + 1) * 2)];
	char longPathValue[NATIVE_FRAME_CAPTURE_MAX_PATH + 16];
	char *longPath[] = {"ctr_native", "--capture-frame", longPathValue};
	int tooManyArgc = 1;

	for (int index = 0; index < (NATIVE_FRAME_CAPTURE_MAX_REQUESTS + 1); index++)
	{
		snprintf(tooManyStorage[index], sizeof(tooManyStorage[index]), "%d=f%d.bmp", index + 1, index + 1);
		tooMany[tooManyArgc++] = "--capture-frame";
		tooMany[tooManyArgc++] = tooManyStorage[index];
	}
	tooMany[0] = "ctr_native";

	memcpy(longPathValue, "5=", 3);
	for (int index = 0; index < NATIVE_FRAME_CAPTURE_MAX_PATH; index++)
	{
		longPathValue[2 + index] = 'p';
	}
	longPathValue[2 + NATIVE_FRAME_CAPTURE_MAX_PATH] = '\0';

	/* Defaults. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(config.requestCount == 0);
	CHECK(config.exitAfterFrame == 0);
	CHECK(NativeFrameCapture_PathForFrame(&config, 1) == NULL);
	CHECK(!NativeFrameCapture_ShouldExitAfterFrame(&config, 1));
	CHECK(Apply(1, noArgs, &config));
	CHECK(config.requestCount == 0);
	CHECK(config.exitAfterFrame == 0);

	/* Single request, both syntaxes. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(3, spaceForm, &config));
	CHECK(config.requestCount == 1);
	CHECK(PathMatches(&config, 30, "out.bmp"));
	CHECK(NativeFrameCapture_PathForFrame(&config, 29) == NULL);

	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(2, equalsForm, &config));
	CHECK(config.requestCount == 1);
	CHECK(PathMatches(&config, 30, "out.bmp"));

	/* Repeatable requests. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(6, multiple, &config));
	CHECK(config.requestCount == 3);
	CHECK(PathMatches(&config, 30, "a.bmp"));
	CHECK(PathMatches(&config, 90, "b.bmp"));
	CHECK(PathMatches(&config, 700, "c.bmp"));
	CHECK(NativeFrameCapture_PathForFrame(&config, 31) == NULL);
	CHECK(NativeFrameCapture_PathForFrame(&config, 0) == NULL);

	/* Duplicate frame number: last wins. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(5, duplicate, &config));
	CHECK(config.requestCount == 1);
	CHECK(PathMatches(&config, 30, "second.bmp"));

	/* Exit-after-frame, both syntaxes, "at or after" semantic. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(3, exitSpace, &config));
	CHECK(config.exitAfterFrame == 700);
	CHECK(NativeFrameCapture_ShouldExitAfterFrame(&config, 700));
	CHECK(NativeFrameCapture_ShouldExitAfterFrame(&config, 701));
	CHECK(!NativeFrameCapture_ShouldExitAfterFrame(&config, 1));
	CHECK(!NativeFrameCapture_ShouldExitAfterFrame(&config, 699));

	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(2, exitEquals, &config));
	CHECK(config.exitAfterFrame == 700);
	CHECK(NativeFrameCapture_ShouldExitAfterFrame(&config, 700));

	/* Combined with unrelated options. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(9, combined, &config));
	CHECK(config.requestCount == 1);
	CHECK(PathMatches(&config, 30, "shots/frame30.bmp"));
	CHECK(config.exitAfterFrame == 31);

	/* Failure cases must leave a pre-populated config bit-for-bit unchanged. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(6, multiple, &config));
	CHECK(Apply(3, exitSpace, &config));
	snapshot = config;

#define CHECK_REJECTED(argc_, argv_) do { \
		CHECK(!Apply((argc_), (argv_), &config)); \
		CHECK(memcmp(&config, &snapshot, sizeof(config)) == 0); \
	} while (0)

	CHECK_REJECTED(2, missingValue);
	CHECK_REJECTED(3, optionInsteadOfValue);
	CHECK_REJECTED(3, noSeparator);
	CHECK_REJECTED(3, emptyFrame);
	CHECK_REJECTED(3, emptyPath);
	CHECK_REJECTED(3, zeroFrame);
	CHECK_REJECTED(3, negativeFrame);
	CHECK_REJECTED(3, alphaFrame);
	CHECK_REJECTED(3, overflowFrame);
	CHECK_REJECTED(2, exitMissing);
	CHECK_REJECTED(3, exitZero);
	CHECK_REJECTED(3, exitAlpha);
	CHECK_REJECTED(3, longPath);
	CHECK_REJECTED(tooManyArgc, tooMany);

#undef CHECK_REJECTED

	/* The surviving config still answers queries from before the failures. */
	CHECK(config.requestCount == 3);
	CHECK(PathMatches(&config, 30, "a.bmp"));
	CHECK(config.exitAfterFrame == 700);

	/* Exactly MAX_REQUESTS distinct frames is accepted. */
	NativeFrameCapture_SetDefaults(&config);
	CHECK(Apply(tooManyArgc - 2, tooMany, &config));
	CHECK(config.requestCount == NATIVE_FRAME_CAPTURE_MAX_REQUESTS);

	CHECK(!NativeFrameCapture_ApplyArgs(1, noArgs, NULL));
	CHECK(NativeFrameCapture_PathForFrame(NULL, 1) == NULL);
	CHECK(!NativeFrameCapture_ShouldExitAfterFrame(NULL, 1));

	/* An out-of-range pre-populated config is rejected rather than trusted. */
	NativeFrameCapture_SetDefaults(&config);
	config.requestCount = NATIVE_FRAME_CAPTURE_MAX_REQUESTS + 1;
	CHECK(!Apply(1, noArgs, &config));
	CHECK(config.requestCount == NATIVE_FRAME_CAPTURE_MAX_REQUESTS + 1);

	puts("native_frame_capture_test: PASS");
	return 0;
}
