#include <platform/native_presentation_pack.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#endif

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static const char *const s_root = "native_presentation_pack_test_pack";
static const char *const s_asset = "native_presentation_pack_test_pack/font.ctrh";
static const char *const s_manifest = "native_presentation_pack_test_pack/presentation.manifest";
static const char *const s_validManifest =
	"ctr-native-presentation-manifest\t1\n"
	"entry\t4\t32\t64\t8\t16\t12\t14\tfont\tfont.ctrh\n";

static int WriteFile(const char *path, const char *contents)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL)
		return 0;
	if (fputs(contents, file) == EOF || fclose(file) != 0)
		return 0;
	return 1;
}

static void RemovePack(void)
{
	(void)remove(s_manifest);
	(void)remove(s_asset);
	(void)REMOVE_DIR(s_root);
}

int main(void)
{
	struct NativePresentationPack pack;
	struct NativePresentationOverrideConfig config;

	RemovePack();
	NativePresentationOverrideConfig_SetDefaults(&config);
	config.packDirectory = "missing-presentation-pack";
	CHECK(NativePresentationPack_Load(&config, &pack));
	CHECK(!NativePresentationRegistry_IsEnabled(&pack.registry));
	CHECK(pack.registry.entryCount == 0u);
	CHECK(NativePresentationPack_GetLastError(&pack) == NATIVE_PRESENTATION_PACK_ERROR_NONE);

	config.enabled = 1;
	CHECK(!NativePresentationPack_Load(&config, &pack));
	CHECK(!NativePresentationRegistry_IsEnabled(&pack.registry));
	CHECK(NativePresentationPack_GetLastError(&pack) == NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_MISSING);

	CHECK(MAKE_DIR(s_root) == 0);
	CHECK(WriteFile(s_asset, "placeholder"));
	config.packDirectory = s_root;
	CHECK(!NativePresentationPack_Load(&config, &pack));
	CHECK(NativePresentationPack_GetLastError(&pack) == NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_MISSING);
	CHECK(WriteFile(s_manifest, s_validManifest));
	CHECK(NativePresentationPack_Load(&config, &pack));
	CHECK(NativePresentationRegistry_IsEnabled(&pack.registry));
	CHECK(pack.registry.entryCount == 1u);
	CHECK(strcmp(NativePresentationRegistry_GetManifestFingerprint(&pack.registry), "0000000000000000") != 0);

	CHECK(WriteFile(s_manifest, "bad\n"));
	CHECK(!NativePresentationPack_Load(&config, &pack));
	CHECK(!NativePresentationRegistry_IsEnabled(&pack.registry));
	CHECK(pack.registry.entryCount == 0u);
	CHECK(NativePresentationPack_GetLastError(&pack) == NATIVE_PRESENTATION_PACK_ERROR_REGISTRY);
	CHECK(NativePresentationPack_GetLastError(NULL) == NATIVE_PRESENTATION_PACK_ERROR_ARGUMENT);
	CHECK(strcmp(NativePresentationPack_ErrorString(NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_MISSING), "presentation.manifest is missing") == 0);

	RemovePack();
	puts("native_presentation_pack_test: PASS");
	return 0;
}
