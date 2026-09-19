#include <platform/native_presentation_registry.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <platform/native_win32.h>
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1u
#endif
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#endif

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static const char *const s_root = "native_presentation_registry_test_pack";
static const char *const s_asset = "native_presentation_registry_test_pack/font.rgba";
static const char *const s_escapeLink = "native_presentation_registry_test_pack/escape";
static const char *const s_outsideRoot = "native_presentation_registry_test_outside";
static const char *const s_outsideAsset = "native_presentation_registry_test_outside/outside.rgba";
static const char *const s_validManifest =
	"ctr-native-presentation-manifest\t1\n"
	"entry\t4\t32\t64\t8\t16\t12\t14\tfont\tfont.rgba\n";

static int MakePack(void)
{
	FILE *file;
	(void)REMOVE_DIR(s_escapeLink);
	(void)remove(s_escapeLink);
	(void)remove(s_outsideAsset);
	(void)REMOVE_DIR(s_outsideRoot);
	(void)REMOVE_DIR(s_root);
	(void)remove(s_asset);
	if (MAKE_DIR(s_root) != 0)
		return 0;
	#if defined(_WIN32)
	if (fopen_s(&file, s_asset, "wb") != 0)
		file = NULL;
	#else
	file = fopen(s_asset, "wb");
	#endif
	if (file == NULL)
		return 0;
	fputs("test", file);
	fclose(file);
	return 1;
}

static int MakeSymlinkEscape(void)
{
	FILE *file;
	if (MAKE_DIR(s_outsideRoot) != 0)
		return 0;
#if defined(_WIN32)
	if (fopen_s(&file, s_outsideAsset, "wb") != 0)
		file = NULL;
#else
	file = fopen(s_outsideAsset, "wb");
#endif
	if (file == NULL)
		return 0;
	fputs("outside", file);
	fclose(file);
#if defined(_WIN32)
	return CreateSymbolicLinkA(s_escapeLink, s_outsideRoot, SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
#else
	return symlink("../native_presentation_registry_test_outside", s_escapeLink) == 0;
#endif
}

static void RemovePack(void)
{
	(void)REMOVE_DIR(s_escapeLink);
	(void)remove(s_escapeLink);
	(void)remove(s_asset);
	(void)REMOVE_DIR(s_root);
	(void)remove(s_outsideAsset);
	(void)REMOVE_DIR(s_outsideRoot);
}

int main(void)
{
	struct NativePresentationRegistry registry;
	struct NativePresentationSourceKey key = {4, 32, 64, 8, 16, 12, 14, NATIVE_PRESENTATION_ASSET_CLASS_FONT};
	struct NativePresentationSourceKey wrongKey = {4, 32, 64, 9, 16, 12, 14, NATIVE_PRESENTATION_ASSET_CLASS_FONT};
	struct NativePresentationSourceKey characterSpriteKey = {4, 32, 64, 20, 16, 12, 14, NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE};
	char duplicate[512];
	char traversal[256];
	char tooMany[8192];
	static char oversized[NATIVE_PRESENTATION_REGISTRY_MAX_MANIFEST_BYTES + 1u];

	CHECK(MakePack());
	NativePresentationRegistry_Init(&registry);
	CHECK(!NativePresentationRegistry_IsEnabled(&registry));
	CHECK(registry.entryCount == 0u);
	CHECK(strcmp(NativePresentationRegistry_GetManifestFingerprint(&registry), "0000000000000000") == 0);

	CHECK(NativePresentationRegistry_LoadManifest(&registry, s_root, s_validManifest, strlen(s_validManifest)));
	CHECK(registry.entryCount == 1u);
	CHECK(!NativePresentationRegistry_IsEnabled(&registry));
	CHECK(NativePresentationRegistry_FindExact(&registry, &key) != NULL);
	CHECK(NativePresentationRegistry_Lookup(&registry, &key) == NULL);
	CHECK(NativePresentationRegistry_FindExact(&registry, &wrongKey) == NULL);
	CHECK(strcmp(NativePresentationRegistry_GetManifestFingerprint(&registry), "0000000000000000") != 0);
	NativePresentationRegistry_SetEnabled(&registry, 1);
	CHECK(NativePresentationRegistry_IsEnabled(&registry));
	CHECK(NativePresentationRegistry_Lookup(&registry, &key) != NULL);
	NativePresentationRegistry_SetEnabled(&registry, 0);
	CHECK(NativePresentationRegistry_Lookup(&registry, &key) == NULL);
	{
		const char *characterSpriteManifest =
			"ctr-native-presentation-manifest\t1\n"
			"entry\t4\t32\t64\t20\t16\t12\t14\tcharacter-sprite\tfont.rgba\n";
		CHECK(NativePresentationRegistry_LoadManifest(&registry, s_root, characterSpriteManifest,
			strlen(characterSpriteManifest)));
		CHECK(NativePresentationRegistry_FindExact(&registry, &characterSpriteKey) != NULL);
		NativePresentationRegistry_SetEnabled(&registry, 1);
		CHECK(NativePresentationRegistry_Lookup(&registry, &characterSpriteKey) != NULL);
	}

	CHECK(!NativePresentationRegistry_LoadManifest(&registry, "missing-presentation-pack", s_validManifest, strlen(s_validManifest)));
	CHECK(registry.entryCount == 0u && !NativePresentationRegistry_IsEnabled(&registry));
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_MISSING);
	{
		const char *wrongVersion = "ctr-native-presentation-manifest\t2\nentry\t4\t32\t64\t8\t16\t12\t14\tfont\tfont.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, wrongVersion, strlen(wrongVersion)));
	}
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT);

	snprintf(duplicate, sizeof(duplicate), "%sentry\t4\t32\t64\t8\t16\t12\t14\tfont\tfont.rgba\n", s_validManifest);
	CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, duplicate, strlen(duplicate)));
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_DUPLICATE_KEY);

	snprintf(traversal, sizeof(traversal),
		"ctr-native-presentation-manifest\t1\nentry\t8\t32\t64\t8\t16\t12\t14\tui-icon\t../font.rgba\n");
	CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, traversal, strlen(traversal)));
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_PATH);
	{
		const char *wildcard = "ctr-native-presentation-manifest\t1\nentry\t4\t*\t64\t8\t16\t12\t14\tfont\tfont.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, wildcard, strlen(wildcard)));
		CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT);
	}
	{
		const char *unsupportedMode = "ctr-native-presentation-manifest\t1\nentry\t3\t32\t64\t8\t16\t12\t14\tfont\tfont.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, unsupportedMode, strlen(unsupportedMode)));
	}
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT);
	{
		const char *unsupportedClass = "ctr-native-presentation-manifest\t1\nentry\t4\t32\t64\t8\t16\t12\t14\tcharacter\tfont.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, unsupportedClass, strlen(unsupportedClass)));
		CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT);
	}
	{
		const char *badBounds = "ctr-native-presentation-manifest\t1\nentry\t4\t32\t64\t1020\t16\t12\t14\tfont\tfont.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, badBounds, strlen(badBounds)));
	}
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT);
	{
		const char *missingAsset = "ctr-native-presentation-manifest\t1\nentry\t4\t32\t64\t8\t16\t12\t14\tfont\tmissing.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, missingAsset, strlen(missingAsset)));
	}
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_MISSING);

	if (MakeSymlinkEscape())
	{
		const char *symlinkEscape = "ctr-native-presentation-manifest\t1\nentry\t4\t32\t64\t8\t16\t12\t14\tfont\tescape/outside.rgba\n";
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, symlinkEscape, strlen(symlinkEscape)));
		CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_REPARSE_POINT);
	}

	memset(oversized, 'x', sizeof(oversized));
	CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, oversized, sizeof(oversized)));
	CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_TOO_LARGE);

	{
		const char *header = "ctr-native-presentation-manifest\t1\n";
		size_t tooManyLength = strlen(header);
		memcpy(tooMany, header, tooManyLength);
		for (unsigned int index = 0; index <= NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES; index++)
		{
			char line[128];
			int lineLength = snprintf(line, sizeof(line), "entry\t4\t32\t64\t%u\t16\t1\t1\tfont\tfont.rgba\n", index);
			CHECK((lineLength > 0) && ((size_t)lineLength < sizeof(line)) && (tooManyLength + (size_t)lineLength < sizeof(tooMany)));
			memcpy(tooMany + tooManyLength, line, (size_t)lineLength);
			tooManyLength += (size_t)lineLength;
		}
		tooMany[tooManyLength] = '\0';
		CHECK(!NativePresentationRegistry_LoadManifest(&registry, s_root, tooMany, tooManyLength));
		CHECK(NativePresentationRegistry_GetLastError(&registry) == NATIVE_PRESENTATION_REGISTRY_ERROR_ENTRY_LIMIT);
	}

	RemovePack();
	puts("native_presentation_registry_test: PASS");
	return 0;
}
