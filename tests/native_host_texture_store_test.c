#include <platform/native_host_texture_store.h>

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

static const char *const s_root = "native_host_texture_store_test_pack";
static const char *const s_fontPath = "native_host_texture_store_test_pack/font.ctrh";
static const char *const s_spritePath = "native_host_texture_store_test_pack/sprite.ctrh";
static const char *const s_manifest =
	"ctr-native-presentation-manifest\t1\n"
	"entry\t4\t19\t837\t194\t266\t3\t14\tfont\tfont.ctrh\n"
	"entry\t16\t42\t0\t64\t90\t16\t8\tcharacter-sprite\tsprite.ctrh\n";

static void WriteU32LE(FILE *file, unsigned int value)
{
	unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8u),
		(unsigned char)(value >> 16u), (unsigned char)(value >> 24u)};
	(void)fwrite(bytes, 1u, sizeof(bytes), file);
}

static int WriteCtrh(const char *path, unsigned int width, unsigned int height, unsigned char value)
{
	unsigned char pixels[128];
	size_t remaining = (size_t)width * (size_t)height * 4u;
	FILE *file;

#if defined(_WIN32)
	if (fopen_s(&file, path, "wb") != 0)
		file = NULL;
#else
	file = fopen(path, "wb");
#endif
	if (file == NULL)
		return 0;
	memset(pixels, value, sizeof(pixels));
	(void)fwrite("CTRH", 1u, 4u, file);
	WriteU32LE(file, 1u);
	WriteU32LE(file, width);
	WriteU32LE(file, height);
	while (remaining != 0u)
	{
		size_t chunk = remaining < sizeof(pixels) ? remaining : sizeof(pixels);
		if (fwrite(pixels, 1u, chunk, file) != chunk)
		{
			fclose(file);
			return 0;
		}
		remaining -= chunk;
	}
	return fclose(file) == 0;
}

static int WriteMalformedCtrh(const char *path)
{
	FILE *file;

#if defined(_WIN32)
	if (fopen_s(&file, path, "wb") != 0)
		file = NULL;
#else
	file = fopen(path, "wb");
#endif
	if (file == NULL)
		return 0;
	/* Valid-looking header with no required RGBA payload. */
	(void)fwrite("CTRH", 1u, 4u, file);
	WriteU32LE(file, 1u);
	WriteU32LE(file, 64u);
	WriteU32LE(file, 32u);
	return fclose(file) == 0;
}

static void RemovePack(void)
{
	(void)remove(s_fontPath);
	(void)remove(s_spritePath);
	(void)REMOVE_DIR(s_root);
}

static int MakePack(void)
{
	RemovePack();
	return (MAKE_DIR(s_root) == 0) && WriteCtrh(s_fontPath, 12u, 56u, 0x31u) &&
		WriteCtrh(s_spritePath, 64u, 32u, 0x72u);
}

int main(void)
{
	struct NativePresentationRegistry registry;
	struct NativeHostTextureStore store;
	struct NativePresentationSourceKey font = {4, 19, 837, 194, 266, 3, 14,
		NATIVE_PRESENTATION_ASSET_CLASS_FONT};
	struct NativePresentationSourceKey sprite = {16, 42, 0, 64, 90, 16, 8,
		NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE};
	const struct NativePresentationRegistryEntry *fontEntry;
	const struct NativeHostTextureAsset *asset;
	struct NativePresentationRegistryEntry copiedEntry;
	struct NativePresentationRegistryEntry originalEntry;

	CHECK(MakePack());
	NativePresentationRegistry_Init(&registry);
	NativeHostTextureStore_Init(&store);
	CHECK(!NativeHostTextureStore_Preload(&store, &registry));
	CHECK(store.lastError == NATIVE_HOST_TEXTURE_STORE_ERROR_REGISTRY_DISABLED && store.entryCount == 0u);
	CHECK(NativePresentationRegistry_LoadManifest(&registry, s_root, s_manifest, strlen(s_manifest)));
	NativePresentationRegistry_SetEnabled(&registry, 1);
	CHECK(NativeHostTextureStore_Preload(&store, &registry));
	CHECK(store.entryCount == 2u && store.lastError == NATIVE_HOST_TEXTURE_STORE_ERROR_NONE);
	asset = NativeHostTextureStore_FindExact(&store, &font);
	CHECK(asset != NULL && asset->width == 12u && asset->height == 56u && asset->rgbaBytes[0] == 0x31u);
	asset = NativeHostTextureStore_FindExact(&store, &sprite);
	CHECK(asset != NULL && asset->width == 64u && asset->height == 32u && asset->rgbaBytes[0] == 0x72u);
	fontEntry = NativePresentationRegistry_FindExact(&registry, &font);
	CHECK(fontEntry != NULL && NativeHostTextureStore_FindOwned(&store, fontEntry) != NULL);
	copiedEntry = *fontEntry;
	CHECK(NativeHostTextureStore_FindOwned(&store, &copiedEntry) == NULL);

	/* Lookup remains memory-only but refuses disabled or mutated registry state. */
	NativePresentationRegistry_SetEnabled(&registry, 0);
	CHECK(NativeHostTextureStore_FindExact(&store, &font) == NULL);
	NativePresentationRegistry_SetEnabled(&registry, 1);
	CHECK(NativeHostTextureStore_FindExact(&store, &font) != NULL);
	originalEntry = registry.entries[0];
	strcpy(registry.entries[0].assetPath, "sprite.ctrh");
	CHECK(NativeHostTextureStore_FindOwned(&store, &registry.entries[0]) == NULL);
	registry.entries[0] = originalEntry;
	CHECK(NativeHostTextureStore_FindOwned(&store, &registry.entries[0]) != NULL);

	/* Failed refresh frees its candidate but never publishes a partial store. */
	(void)remove(s_spritePath);
	CHECK(!NativeHostTextureStore_Preload(&store, &registry));
	CHECK(store.lastError == NATIVE_HOST_TEXTURE_STORE_ERROR_LOAD && store.entryCount == 2u);
	CHECK(NativeHostTextureStore_FindExact(&store, &font) != NULL);
	CHECK(WriteMalformedCtrh(s_spritePath));
	CHECK(!NativeHostTextureStore_Preload(&store, &registry));
	CHECK(store.lastError == NATIVE_HOST_TEXTURE_STORE_ERROR_LOAD && store.entryCount == 2u);
	CHECK(WriteCtrh(s_spritePath, 64u, 32u, 0x72u));
	CHECK(NativeHostTextureStore_Preload(&store, &registry));

	NativeHostTextureStore_Free(&store);
	CHECK(store.entryCount == 0u && NativeHostTextureStore_FindExact(&store, &font) == NULL);
	NativeHostTextureStore_Free(&store);
	RemovePack();
	puts("native_host_texture_store_test: PASS");
	return 0;
}
