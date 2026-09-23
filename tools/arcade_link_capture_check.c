/* Offline check of an arcade-link preview capture (RGB only).
 *
 *   ctr_native_arcade_link_capture_check <capture.bmp> <screen-name>
 *
 * Exit status: 0 pass, 1 check failed, 2 usage, I/O or BMP parse error. */
#include "platform/native_capture_check.h"

#include <stdio.h>
#include <stdlib.h>

static void Usage(void)
{
	unsigned i;

	fprintf(stderr, "usage: ctr_native_arcade_link_capture_check <capture.bmp> <screen-name>\n");
	fprintf(stderr, "screens:");
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_SCREEN_COUNT; i++)
	{
		fprintf(stderr, " %s", NativeCaptureCheck_ScreenName((enum NativeCaptureScreen)i));
	}
	fprintf(stderr, "\n");
}

static uint8_t *ReadFile(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	long length;
	uint8_t *data;

	if (f == NULL)
		return NULL;
	if ((fseek(f, 0, SEEK_END) != 0) || ((length = ftell(f)) < 0) || (fseek(f, 0, SEEK_SET) != 0))
	{
		fclose(f);
		return NULL;
	}
	data = (uint8_t *)malloc(length > 0 ? (size_t)length : 1u);
	if ((data == NULL) || (fread(data, 1u, (size_t)length, f) != (size_t)length))
	{
		free(data);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*size = (size_t)length;
	return data;
}

int main(int argc, char **argv)
{
	enum NativeCaptureScreen screen;
	enum NativeCaptureBmpStatus status;
	struct NativeCaptureImage image;
	struct NativeCaptureReport report;
	uint8_t *data;
	size_t size = 0u;
	unsigned i;

	if ((argc != 3) || !NativeCaptureCheck_ScreenFromName(argv[2], &screen))
	{
		Usage();
		return 2;
	}
	data = ReadFile(argv[1], &size);
	if (data == NULL)
	{
		fprintf(stderr, "error: cannot read %s\n", argv[1]);
		return 2;
	}
	status = NativeCaptureCheck_ParseBmp(data, size, &image);
	if (status != NATIVE_CAPTURE_BMP_OK)
	{
		fprintf(stderr, "error: %s: %s\n", argv[1], NativeCaptureCheck_BmpStatusName(status));
		free(data);
		return 2;
	}
	if (!NativeCaptureCheck_Run(&image, screen, &report))
	{
		fprintf(stderr, "error: %s: %ux%u is smaller than the 512x216 layout\n", argv[1], (unsigned)image.width, (unsigned)image.height);
		free(data);
		return 2;
	}

	printf("capture %s (%ux%u %u bpp) as %s\n", argv[1], (unsigned)image.width, (unsigned)image.height, (unsigned)(image.bytesPerPixel * 8u),
	       NativeCaptureCheck_ScreenName(screen));
	for (i = 0u; i < (unsigned)NATIVE_CAPTURE_CHECK_COUNT; i++)
	{
		const struct NativeCaptureSubCheck *check = &report.checks[i];
		const char *verdict = check->passed ? "PASS" : "FAIL";
		const char *op = "";

		/* Checks the screen's layout has no region for are not printed. */
		if (check->expect == NATIVE_CAPTURE_EXPECT_NONE)
			continue;
		if (check->expect == NATIVE_CAPTURE_EXPECT_AT_LEAST)
			op = ">=";
		else if (check->expect == NATIVE_CAPTURE_EXPECT_AT_MOST)
			op = "<=";
		else
			verdict = "INFO";
		if (check->expect == NATIVE_CAPTURE_EXPECT_INFO)
			printf("  %-18s %s measured=%ld (optional) [%s]\n", NativeCaptureCheck_CheckName((enum NativeCaptureCheckId)i), verdict, (long)check->measured,
			       NativeCaptureCheck_CheckUnit((enum NativeCaptureCheckId)i));
		else
			printf("  %-18s %s measured=%ld need %s %ld [%s]\n", NativeCaptureCheck_CheckName((enum NativeCaptureCheckId)i), verdict, (long)check->measured, op,
			       (long)check->threshold, NativeCaptureCheck_CheckUnit((enum NativeCaptureCheckId)i));
	}
	if (report.passed)
		printf("result PASS\n");
	else
		printf("result FAIL (first failed: %s)\n", NativeCaptureCheck_CheckName((enum NativeCaptureCheckId)report.firstFailed));
	free(data);
	return report.passed ? 0 : 1;
}
