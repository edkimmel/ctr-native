#ifndef PLATFORM_NATIVE_HOST_TEXTURE_STORE_H
#define PLATFORM_NATIVE_HOST_TEXTURE_STORE_H

/*
 * Bounded, GL-free cache of decoded local presentation assets.
 *
 * A store is populated once from every entry in an explicitly enabled
 * registry.  The only file opens happen in Preload, through the production
 * NativePresentationAssetLoader_Load bridge; lookup is memory-only.  A
 * failed refresh never exposes a partially decoded pack: an already-complete
 * store remains intact, while an empty store remains empty.
 *
 * This layer deliberately establishes neither primitive eligibility nor UV
 * mapping or scale.  NativeHostTexturePlan remains the per-primitive gate.
 */

#include <platform/native_host_texture_asset.h>
#include <platform/native_presentation_registry.h>

enum NativeHostTextureStoreError
{
	NATIVE_HOST_TEXTURE_STORE_ERROR_NONE = 0,
	NATIVE_HOST_TEXTURE_STORE_ERROR_ARGUMENT,
	NATIVE_HOST_TEXTURE_STORE_ERROR_REGISTRY_DISABLED,
	NATIVE_HOST_TEXTURE_STORE_ERROR_LOAD,
	NATIVE_HOST_TEXTURE_STORE_ERROR_ASSET,
};

struct NativeHostTextureStoreEntry
{
	/* Points only at the exact registry-owned entry that was preloaded. */
	const struct NativePresentationRegistryEntry *registryEntry;
	/* Detects a later in-place registry reload or mutation without I/O. */
	struct NativePresentationRegistryEntry registryEntrySnapshot;
	struct NativeHostTextureAsset asset;
};

struct NativeHostTextureStore
{
	const struct NativePresentationRegistry *registry;
	unsigned int entryCount;
	enum NativeHostTextureStoreError lastError;
	/* These snapshots invalidate lookup after a pack-root/manifest swap. */
	char registryAssetRoot[NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT];
	char registryManifestFingerprint[NATIVE_PRESENTATION_REGISTRY_FINGERPRINT_BYTES];
	struct NativeHostTextureStoreEntry entries[NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES];
};

void NativeHostTextureStore_Init(struct NativeHostTextureStore *store);
void NativeHostTextureStore_Free(struct NativeHostTextureStore *store);

/*
 * Decodes every enabled registry entry before changing `store`.  A successful
 * call replaces any prior complete snapshot; failure frees its private
 * candidate and preserves any prior complete snapshot.  Decoder-valid CTRH
 * assets are the only acceptance criterion here.
 */
int NativeHostTextureStore_Preload(struct NativeHostTextureStore *store,
	const struct NativePresentationRegistry *registry);

/*
 * Returns a decoded asset only for the precise registry entry object owned by
 * this unchanged, currently enabled registry.  A copied entry, disabled pack,
 * registry reload, or in-place mutation fails closed.  No filesystem access
 * occurs in this function.
 */
const struct NativeHostTextureAsset *NativeHostTextureStore_FindOwned(
	const struct NativeHostTextureStore *store,
	const struct NativePresentationRegistryEntry *entry);

/* Exact source-key convenience lookup with the same ownership checks. */
const struct NativeHostTextureAsset *NativeHostTextureStore_FindExact(
	const struct NativeHostTextureStore *store,
	const struct NativePresentationSourceKey *source);

const char *NativeHostTextureStore_ErrorString(enum NativeHostTextureStoreError error);

#endif
