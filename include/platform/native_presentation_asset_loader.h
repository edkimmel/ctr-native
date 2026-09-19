#ifndef PLATFORM_NATIVE_PRESENTATION_ASSET_LOADER_H
#define PLATFORM_NATIVE_PRESENTATION_ASSET_LOADER_H

/*
 * Production, GL-free loader callback for NativeHostTexturePlan.
 *
 * It is intentionally the narrow filesystem bridge between a selected
 * registry entry and the bounded CTRH decoder.  The registry repeats its
 * containment and reparse-point checks immediately before this callback can
 * open a file; a loader failure remains a normal native-render fallback.
 */

#include <platform/native_host_texture_asset.h>
#include <platform/native_presentation_registry.h>

/*
 * NativeHostTexturePlanLoadFn-compatible callback. `context` is reserved and
 * must be NULL. `assetOut` must have been initialized by the caller. On a
 * resolution failure it is freed/left empty and no file is opened.
 */
int NativePresentationAssetLoader_Load(void *context,
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry,
	struct NativeHostTextureAsset *assetOut);

#endif
