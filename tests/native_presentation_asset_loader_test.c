/* Production callback coverage: registry authority, use-time path checks, and CTRH decode. */
#include <platform/native_host_texture_plan.h>
#include <platform/native_presentation_asset_loader.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <platform/native_win32.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#endif

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static const char *const s_root = "native_presentation_asset_loader_test_pack";
static const char *const s_asset = "native_presentation_asset_loader_test_pack/font.ctrh";
static const char *const s_outsideRoot = "native_presentation_asset_loader_test_outside";
static const char *const s_outsideAsset = "native_presentation_asset_loader_test_outside/outside.ctrh";
static const char *const s_manifest =
	"ctr-native-presentation-manifest\t1\n"
	"entry\t4\t19\t837\t194\t266\t3\t14\tfont\tfont.ctrh\n";

static void WriteU32LE(FILE *file, unsigned int value)
{
	unsigned char bytes[4];
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8u);
	bytes[2] = (unsigned char)(value >> 16u);
	bytes[3] = (unsigned char)(value >> 24u);
	(void)fwrite(bytes, 1u, sizeof(bytes), file);
}

static int WriteCtrh(const char *path)
{
	unsigned char pixels[256];
	size_t remaining = 48u * 56u * 4u;
	size_t pixelIndex;
	FILE *file;

#if defined(_WIN32)
	if (fopen_s(&file, path, "wb") != 0)
		file = NULL;
#else
	file = fopen(path, "wb");
#endif
	if (file == NULL)
		return 0;
	(void)fwrite("CTRH", 1u, 4u, file);
	WriteU32LE(file, 1u);
	WriteU32LE(file, 48u);
	WriteU32LE(file, 56u);
	for (pixelIndex = 0u; pixelIndex < sizeof(pixels); ++pixelIndex)
		pixels[pixelIndex] = (unsigned char)(pixelIndex ^ 0x5au);
	while (remaining != 0u)
	{
		size_t chunkSize = remaining < sizeof(pixels) ? remaining : sizeof(pixels);
		if (fwrite(pixels, 1u, chunkSize, file) != chunkSize)
		{
			fclose(file);
			return 0;
		}
		remaining -= chunkSize;
	}
	return fclose(file) == 0;
}

static void RemovePack(void)
{
	(void)remove(s_asset);
	(void)REMOVE_DIR(s_root);
	(void)remove(s_outsideAsset);
	(void)REMOVE_DIR(s_outsideRoot);
}

static int MakePack(void)
{
	RemovePack();
	return (MAKE_DIR(s_root) == 0) && WriteCtrh(s_asset);
}

static int ReplaceWithEscapeLink(void)
{
	if ((MAKE_DIR(s_outsideRoot) != 0) || !WriteCtrh(s_outsideAsset))
		return 0;
	(void)remove(s_asset);
#if defined(_WIN32)
	return CreateSymbolicLinkA(s_asset, s_outsideAsset, 0) != 0;
#else
	return symlink("../native_presentation_asset_loader_test_outside/outside.ctrh", s_asset) == 0;
#endif
}

static struct NativeHostTexturePlanInput ValidPlanInput(struct NativePresentationRegistry *registry)
{
	struct NativeHostTexturePlanInput input;
	memset(&input, 0, sizeof(input));
	input.registry = registry;
	input.source.textureMode = NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT;
	input.source.tpage = 0x13u;
	input.source.clut = 0x345u;
	input.source.uvMinU = 8u;
	input.source.uvMinV = 10u;
	input.source.uvMaxU = 19u;
	input.source.uvMaxV = 23u;
	input.flags.primitive = NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_QUAD;
	input.flags.mode = NATIVE_TEXTURE_OVERRIDE_MODE_OPAQUE;
	input.flags.overrideIsWellFormed = 1;
	input.load = NativePresentationAssetLoader_Load;
	return input;
}

int main(void)
{
	struct NativePresentationRegistry registry;
	struct NativePresentationSourceKey key = {4, 19, 837, 194, 266, 3, 14,
		NATIVE_PRESENTATION_ASSET_CLASS_FONT};
	const struct NativePresentationRegistryEntry *entry;
	struct NativePresentationRegistryEntry copiedEntry;
	struct NativeHostTextureAsset asset;
	struct NativeHostTexturePlanInput input;
	struct NativeHostTexturePlan plan;
	char path[NATIVE_PRESENTATION_REGISTRY_MAX_RESOLVED_ASSET_PATH];

	CHECK(MakePack());
	NativePresentationRegistry_Init(&registry);
	CHECK(NativePresentationRegistry_LoadManifest(&registry, s_root, s_manifest, strlen(s_manifest)));
	entry = NativePresentationRegistry_FindExact(&registry, &key);
	CHECK(entry != NULL);

	/* Disabled packs and copied/non-registry entries can never authorize opens. */
	memset(path, 0x7f, sizeof(path));
	CHECK(!NativePresentationRegistry_ResolveEnabledAssetPath(&registry, entry, path, sizeof(path)) && path[0] == '\0');
	NativeHostTextureAsset_Init(&asset);
	CHECK(!NativePresentationAssetLoader_Load(NULL, &registry, entry, &asset));
	CHECK(asset.rgbaBytes == NULL);
	NativePresentationRegistry_SetEnabled(&registry, 1);
	CHECK(NativePresentationRegistry_ResolveEnabledAssetPath(&registry, entry, path, sizeof(path)));
	CHECK(strcmp(path, s_asset) == 0);
	copiedEntry = *entry;
	CHECK(!NativePresentationRegistry_ResolveEnabledAssetPath(&registry, &copiedEntry, path, sizeof(path)));
	CHECK(!NativePresentationAssetLoader_Load((void *)1, &registry, entry, &asset));

	CHECK(NativePresentationAssetLoader_Load(NULL, &registry, entry, &asset));
	CHECK(asset.width == 48u && asset.height == 56u && asset.rgbaByteCount == 48u * 56u * 4u);
	CHECK(asset.rgbaBytes[0] == 0x5au && asset.rgbaBytes[255] == 0xa5u);
	NativeHostTextureAsset_Free(&asset);

	/* Revalidate mutable storage and filesystem state at use time. */
	strcpy(registry.entries[0].assetPath, "../outside.ctrh");
	CHECK(!NativePresentationRegistry_ResolveEnabledAssetPath(&registry, &registry.entries[0], path, sizeof(path)));
	strcpy(registry.entries[0].assetPath, "font.ctrh");
	(void)remove(s_asset);
	CHECK(!NativePresentationRegistry_ResolveEnabledAssetPath(&registry, &registry.entries[0], path, sizeof(path)));
	CHECK(WriteCtrh(s_asset));
	if (ReplaceWithEscapeLink())
	{
		CHECK(!NativePresentationRegistry_ResolveEnabledAssetPath(&registry, &registry.entries[0], path, sizeof(path)));
	}
	/* Symlink creation can be unavailable on locked-down Windows accounts; it
	 * removes the original either way, so restore the ordinary fixture. */
	(void)remove(s_asset);
	CHECK(WriteCtrh(s_asset));

	/* The callback is directly usable by the plan and yields uniform 4x facts. */
	input = ValidPlanInput(&registry);
	NativeHostTexturePlan_Init(&plan);
	CHECK(NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE && plan.upscale == 4u);
	NativeHostTexturePlan_Free(&plan);

	RemovePack();
	puts("native_presentation_asset_loader_test: PASS");
	return 0;
}
