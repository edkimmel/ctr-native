#include <platform/native_presentation_asset_loader.h>

int NativePresentationAssetLoader_Load(void *context,
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry,
	struct NativeHostTextureAsset *assetOut)
{
	char path[NATIVE_PRESENTATION_REGISTRY_MAX_RESOLVED_ASSET_PATH];

	/* No ambient/global path state belongs in a local presentation callback. */
	if ((context != NULL) || (assetOut == NULL) ||
		!NativePresentationRegistry_ResolveEnabledAssetPath(registry, entry, path,
			sizeof(path)))
	{
		if (assetOut != NULL)
		{
			NativeHostTextureAsset_Free(assetOut);
		}
		return 0;
	}
	return NativeHostTextureAsset_LoadFile(assetOut, path);
}
