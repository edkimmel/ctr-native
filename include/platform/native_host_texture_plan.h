#ifndef PLATFORM_NATIVE_HOST_TEXTURE_PLAN_H
#define PLATFORM_NATIVE_HOST_TEXTURE_PLAN_H

/*
 * GL-free, local-presentation planning for a single retail textured primitive.
 *
 * A manifest must name one, and only one, of the permitted presentation
 * classes (font, UI icon, static UI, or character sprite) for the primitive's exact retail
 * source.  This prevents an asset-class guess from silently choosing a
 * different replacement for the same source rectangle.  The selected CTRH is
 * obtained only through an injected loader; this module does no filesystem
 * work and does not alter renderer, replay, or simulation state.
 */

#include <platform/native_host_texture_asset.h>
#include <platform/native_host_texture_binding.h>

enum NativeHostTexturePlanError
{
	NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE = 0,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_ARGUMENT,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_REGISTRY_DISABLED,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_SOURCE_KEY,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_NO_EXACT_MATCH,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_AMBIGUOUS_CLASS_MATCH,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_POLICY,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_LOADER,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_ASSET,
	NATIVE_HOST_TEXTURE_PLAN_ERROR_UPSCALE_DIMENSIONS,
};

/*
 * The callback is the sole asset-loading authority.  A production callback
 * may resolve the entry below the already-validated pack root and call
 * NativeHostTextureAsset_LoadFile; unit tests can provide a bounded in-memory
 * fixture instead.  On success it must initialize a valid, owned asset in
 * `assetOut`, which NativeHostTexturePlan_Free will release.
 */
typedef int (*NativeHostTexturePlanLoadFn)(void *context,
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry,
	struct NativeHostTextureAsset *assetOut);

struct NativeHostTexturePlanInput
{
	const struct NativePresentationRegistry *registry;
	/* assetClass is ignored: every allowed class is checked explicitly. */
	struct NativeHostTexturePrimitiveSourceFacts source;
	struct NativeHostTextureBindingFlags flags;
	NativeHostTexturePlanLoadFn load;
	void *loadContext;
};

struct NativeHostTexturePlan
{
	struct NativePresentationSourceKey sourceKey;
	struct NativeHostTextureUvRemapFacts uvRemap;
	const struct NativePresentationRegistryEntry *entry;
	struct NativeHostTextureAsset asset;
	unsigned int upscale;
	struct NativeTextureOverrideDecision decision;
	enum NativeHostTexturePlanError error;
};

/* Initialize once before the first Build call; Free is safe after Init. */
void NativeHostTexturePlan_Init(struct NativeHostTexturePlan *plan);
void NativeHostTexturePlan_Free(struct NativeHostTexturePlan *plan);

/*
 * Builds one fail-closed host-texture plan.  A successful return means the
 * primitive passed the existing override policy, exactly one enabled registry
 * class matched, its injected load succeeded, and the CTRH dimensions are a
 * uniform positive integer scale of the original logical texel extent.
 *
 * Build releases a prior asset in an initialized plan before replacing it.
 */
int NativeHostTexturePlan_Build(const struct NativeHostTexturePlanInput *input,
	struct NativeHostTexturePlan *plan);

const char *NativeHostTexturePlan_ErrorName(enum NativeHostTexturePlanError error);

#endif
