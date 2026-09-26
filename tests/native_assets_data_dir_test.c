/*
 * NativeAssets_InitWithAssetDir (docs/PACKAGING.md PK-6) over the fabricated
 * folders that tests/native_assets_data_dir_test.cmake creates under argv[1]
 * (tiny dummy files, never game data):
 *
 *   <work>/exe/data/ctr-u.bin
 *   <work>/exe/data with space/ctr-u.bin
 *   <work>/exe/big/bigfile.big
 *   <work>/exe/empty/
 *   <work>/abs/CTR-U.BIN
 */
#include "../platform/native_assets.c"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

static char s_exeDir[1024];
static char s_absDir[1024];

static int Expect(const char *exeBase, const char *assetDir, int expectedResult, const char *expectedAssetDir)
{
	char resolved[1024];
	const int result = NativeAssets_InitWithAssetDir(exeBase, assetDir, resolved, sizeof(resolved));

	if (result != expectedResult)
	{
		fprintf(stderr, "InitWithAssetDir(%s, %s) returned %d, expected %d\n", exeBase, assetDir != NULL ? assetDir : "(null)", result,
		        expectedResult);
		return 0;
	}
	if ((expectedAssetDir != NULL) && (strcmp(resolved, expectedAssetDir) != 0))
	{
		fprintf(stderr, "InitWithAssetDir(%s, %s) resolved '%s', expected '%s'\n", exeBase, assetDir, resolved, expectedAssetDir);
		return 0;
	}
	if (expectedResult)
	{
		if ((strcmp(NativeAssets_GetAssetDir(), expectedAssetDir) != 0) || (strcmp(NativeAssets_GetBaseDir(), s_exeDir) != 0))
		{
			fprintf(stderr, "after InitWithAssetDir(%s, %s): base '%s' assets '%s'\n", exeBase, assetDir, NativeAssets_GetBaseDir(),
			        NativeAssets_GetAssetDir());
			return 0;
		}
	}
	return 1;
}

int main(int argc, char *argv[])
{
	char expected[1024];
	char exeWithSlash[1024];
	char absWithBackslashes[1024];

	CHECK(argc == 2);
	CHECK(snprintf(s_exeDir, sizeof(s_exeDir), "%s/exe", argv[1]) < (int)sizeof(s_exeDir));
	CHECK(snprintf(s_absDir, sizeof(s_absDir), "%s/abs", argv[1]) < (int)sizeof(s_absDir));
	CHECK(snprintf(exeWithSlash, sizeof(exeWithSlash), "%s/", s_exeDir) < (int)sizeof(exeWithSlash));

	/* A relative data dir joins the exe directory; the base stays the exe directory. */
	CHECK(snprintf(expected, sizeof(expected), "%s/data", s_exeDir) < (int)sizeof(expected));
	CHECK(Expect(s_exeDir, "data", 1, expected));
	/* Trailing separators on either side are trimmed. */
	CHECK(Expect(exeWithSlash, "data/", 1, expected));
	CHECK(Expect(exeWithSlash, "data\\", 1, expected));

	/* Spaces in the path. */
	CHECK(snprintf(expected, sizeof(expected), "%s/data with space", s_exeDir) < (int)sizeof(expected));
	CHECK(Expect(s_exeDir, "data with space", 1, expected));

	/* An extracted tree: BIGFILE.BIG in any case. */
	CHECK(snprintf(expected, sizeof(expected), "%s/big", s_exeDir) < (int)sizeof(expected));
	CHECK(Expect(s_exeDir, "big", 1, expected));

	/* An absolute path is used as given (slashes normalized), with a case-insensitive disc image name. */
	CHECK(Expect(s_exeDir, s_absDir, 1, s_absDir));
	CHECK(snprintf(absWithBackslashes, sizeof(absWithBackslashes), "%s", s_absDir) < (int)sizeof(absWithBackslashes));
	for (char *cursor = absWithBackslashes; *cursor != '\0'; cursor++)
	{
		if (*cursor == '/')
		{
			*cursor = '\\';
		}
	}
	CHECK(Expect(s_exeDir, absWithBackslashes, 1, s_absDir));

	/* A relative path may climb out of the exe directory. */
	CHECK(snprintf(expected, sizeof(expected), "%s/../abs", s_exeDir) < (int)sizeof(expected));
	CHECK(Expect(s_exeDir, "..\\abs", 1, expected));

	/* A folder without the file, or no folder at all, fails and names the resolved path; the paths stay unchanged. */
	CHECK(snprintf(expected, sizeof(expected), "%s/empty", s_exeDir) < (int)sizeof(expected));
	CHECK(Expect(s_exeDir, "empty", 0, expected));
	CHECK(strcmp(NativeAssets_GetAssetDir(), "") != 0);
	{
		char before[1024];

		CHECK(snprintf(before, sizeof(before), "%s", NativeAssets_GetAssetDir()) < (int)sizeof(before));
		CHECK(snprintf(expected, sizeof(expected), "%s/missing", s_exeDir) < (int)sizeof(expected));
		CHECK(Expect(s_exeDir, "missing", 0, expected));
		CHECK(strcmp(NativeAssets_GetAssetDir(), before) == 0);
	}

	/* Drive-relative and (on Windows) root-relative paths would resolve against
	 * the launch directory before the chdir and the base directory after it:
	 * rejected before any resolution, with the asset paths unchanged. */
	{
		char before[1024];
		static const char *const rejected[] = {
			"C:", "C:data", "c:..\\abs", "Z:ctr data",
#if defined(_WIN32)
			"\\", "/", "\\data", "/data", "\\ctr-data\\x",
#endif
		};
		static const char *const accepted[] = { "data", "..\\abs", ".\\data", "C:\\", "C:/ctr-data", "c:\\ctr data", "\\\\server\\share", "//server/share", "", NULL };

		CHECK(snprintf(before, sizeof(before), "%s", NativeAssets_GetAssetDir()) < (int)sizeof(before));
		for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
		{
			CHECK(NativeAssets_IsDriveOrRootRelativePath(rejected[i]) == 1);
			CHECK(Expect(s_exeDir, rejected[i], 0, ""));
			CHECK(strcmp(NativeAssets_GetAssetDir(), before) == 0);
		}
		for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++)
		{
			CHECK(NativeAssets_IsDriveOrRootRelativePath(accepted[i]) == 0);
		}
	}

	/* No data dir. */
	CHECK(Expect(s_exeDir, "", 0, ""));
	CHECK(Expect(s_exeDir, NULL, 0, ""));
	CHECK(NativeAssets_InitWithAssetDir(s_exeDir, "data", NULL, 0) == 1);

	NativeDiscImage_Shutdown();
	puts("native_assets_data_dir_test: ok");
	return 0;
}
