#include <platform/native_host_texture_store.h>
#include <platform/native_presentation_asset_loader.h>

#include <limits.h>
#include <string.h>

static void NativeHostTextureStore_Reset(struct NativeHostTextureStore *store,
	enum NativeHostTextureStoreError error)
{
	unsigned int index;

	if (store == NULL)
		return;
	memset(store, 0, sizeof(*store));
	for (index = 0u; index < NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES; ++index)
		NativeHostTextureAsset_Init(&store->entries[index].asset);
	store->lastError = error;
}

void NativeHostTextureStore_Init(struct NativeHostTextureStore *store)
{
	NativeHostTextureStore_Reset(store, NATIVE_HOST_TEXTURE_STORE_ERROR_NONE);
}

void NativeHostTextureStore_Free(struct NativeHostTextureStore *store)
{
	unsigned int index;

	if (store == NULL)
		return;
	for (index = 0u; index < NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES; ++index)
		NativeHostTextureAsset_Free(&store->entries[index].asset);
	NativeHostTextureStore_Reset(store, NATIVE_HOST_TEXTURE_STORE_ERROR_NONE);
}

static int NativeHostTextureStore_AssetIsDecoderValid(const struct NativeHostTextureAsset *asset)
{
	size_t pixelCount;
	size_t expectedBytes;

	if ((asset == NULL) || (asset->lastError != NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE) ||
		(asset->width == 0u) || (asset->height == 0u) ||
		(asset->width > NATIVE_HOST_TEXTURE_ASSET_MAX_WIDTH) ||
		(asset->height > NATIVE_HOST_TEXTURE_ASSET_MAX_HEIGHT) || (asset->rgbaBytes == NULL) ||
		((size_t)asset->width > SIZE_MAX / (size_t)asset->height))
	{
		return 0;
	}
	pixelCount = (size_t)asset->width * (size_t)asset->height;
	if ((pixelCount > SIZE_MAX / 4u) || (pixelCount * 4u > NATIVE_HOST_TEXTURE_ASSET_MAX_RGBA_BYTES))
		return 0;
	expectedBytes = pixelCount * 4u;
	return asset->rgbaByteCount == expectedBytes;
}

int NativeHostTextureStore_Preload(struct NativeHostTextureStore *store,
	const struct NativePresentationRegistry *registry)
{
	struct NativeHostTextureStore candidate;
	unsigned int index;
	enum NativeHostTextureStoreError error;

	if ((store == NULL) || (registry == NULL))
	{
		if (store != NULL)
			store->lastError = NATIVE_HOST_TEXTURE_STORE_ERROR_ARGUMENT;
		return 0;
	}
	if (!NativePresentationRegistry_IsEnabled(registry) ||
		(registry->entryCount > NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES))
	{
		store->lastError = NATIVE_HOST_TEXTURE_STORE_ERROR_REGISTRY_DISABLED;
		return 0;
	}

	NativeHostTextureStore_Init(&candidate);
	candidate.registry = registry;
	candidate.entryCount = registry->entryCount;
	memcpy(candidate.registryAssetRoot, registry->assetRoot, sizeof(candidate.registryAssetRoot));
	memcpy(candidate.registryManifestFingerprint, registry->manifestFingerprint,
		sizeof(candidate.registryManifestFingerprint));
	for (index = 0u; index < registry->entryCount; ++index)
	{
		struct NativeHostTextureStoreEntry *destination = &candidate.entries[index];
		const struct NativePresentationRegistryEntry *source = &registry->entries[index];

		destination->registryEntry = source;
		destination->registryEntrySnapshot = *source;
		if (!NativePresentationAssetLoader_Load(NULL, registry, source, &destination->asset))
		{
			error = NATIVE_HOST_TEXTURE_STORE_ERROR_LOAD;
			goto fail;
		}
		if (!NativeHostTextureStore_AssetIsDecoderValid(&destination->asset))
		{
			error = NATIVE_HOST_TEXTURE_STORE_ERROR_ASSET;
			goto fail;
		}
	}

	NativeHostTextureStore_Free(store);
	*store = candidate;
	store->lastError = NATIVE_HOST_TEXTURE_STORE_ERROR_NONE;
	return 1;

fail:
	NativeHostTextureStore_Free(&candidate);
	store->lastError = error;
	return 0;
}

static int NativeHostTextureStore_SourceKeysEqual(const struct NativePresentationSourceKey *left,
	const struct NativePresentationSourceKey *right)
{
	return (left != NULL) && (right != NULL) &&
		(left->textureMode == right->textureMode) && (left->tpage == right->tpage) &&
		(left->clut == right->clut) && (left->x == right->x) && (left->y == right->y) &&
		(left->width == right->width) && (left->height == right->height) &&
		(left->assetClass == right->assetClass);
}

static int NativeHostTextureStore_EntryIsCurrent(const struct NativeHostTextureStore *store,
	unsigned int index, const struct NativePresentationRegistryEntry *entry)
{
	const struct NativeHostTextureStoreEntry *stored;

	if ((store == NULL) || (entry == NULL) || (store->registry == NULL) ||
		!NativePresentationRegistry_IsEnabled(store->registry) || (index >= store->entryCount) ||
		(index >= store->registry->entryCount))
	{
		return 0;
	}
	stored = &store->entries[index];
	return (stored->registryEntry == entry) && (entry == &store->registry->entries[index]) &&
		(memcmp(store->registryAssetRoot, store->registry->assetRoot,
			sizeof(store->registryAssetRoot)) == 0) &&
		(memcmp(store->registryManifestFingerprint, store->registry->manifestFingerprint,
			sizeof(store->registryManifestFingerprint)) == 0) &&
		NativeHostTextureStore_SourceKeysEqual(&stored->registryEntrySnapshot.source, &entry->source) &&
		(memcmp(stored->registryEntrySnapshot.assetPath, entry->assetPath,
			sizeof(stored->registryEntrySnapshot.assetPath)) == 0) &&
		NativeHostTextureStore_AssetIsDecoderValid(&stored->asset);
}

const struct NativeHostTextureAsset *NativeHostTextureStore_FindOwned(
	const struct NativeHostTextureStore *store,
	const struct NativePresentationRegistryEntry *entry)
{
	unsigned int index;

	if ((store == NULL) || (entry == NULL))
		return NULL;
	for (index = 0u; index < store->entryCount; ++index)
	{
		if ((store->entries[index].registryEntry == entry) &&
			NativeHostTextureStore_EntryIsCurrent(store, index, entry))
			return &store->entries[index].asset;
	}
	return NULL;
}

const struct NativeHostTextureAsset *NativeHostTextureStore_FindExact(
	const struct NativeHostTextureStore *store,
	const struct NativePresentationSourceKey *source)
{
	unsigned int index;

	if ((store == NULL) || (source == NULL))
		return NULL;
	for (index = 0u; index < store->entryCount; ++index)
	{
		const struct NativeHostTextureStoreEntry *entry = &store->entries[index];
		if (NativeHostTextureStore_SourceKeysEqual(&entry->registryEntrySnapshot.source, source))
			return NativeHostTextureStore_FindOwned(store, entry->registryEntry);
	}
	return NULL;
}

const char *NativeHostTextureStore_ErrorString(enum NativeHostTextureStoreError error)
{
	switch (error)
	{
	case NATIVE_HOST_TEXTURE_STORE_ERROR_NONE: return "none";
	case NATIVE_HOST_TEXTURE_STORE_ERROR_ARGUMENT: return "invalid argument";
	case NATIVE_HOST_TEXTURE_STORE_ERROR_REGISTRY_DISABLED: return "registry is disabled or invalid";
	case NATIVE_HOST_TEXTURE_STORE_ERROR_LOAD: return "asset preload failed";
	case NATIVE_HOST_TEXTURE_STORE_ERROR_ASSET: return "decoded asset is invalid";
	default: return "unknown store error";
	}
}
