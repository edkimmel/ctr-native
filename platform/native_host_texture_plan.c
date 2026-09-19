#include <platform/native_host_texture_plan.h>

#include <string.h>

static void NativeHostTexturePlan_SetNativeDecision(struct NativeHostTexturePlan *plan)
{
	plan->decision.selection = NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE;
	plan->decision.fallbackReason = NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE;
}

void NativeHostTexturePlan_Init(struct NativeHostTexturePlan *plan)
{
	if (plan == NULL)
		return;
	memset(plan, 0, sizeof(*plan));
	NativeHostTextureAsset_Init(&plan->asset);
	NativeHostTexturePlan_SetNativeDecision(plan);
	plan->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_ARGUMENT;
}

void NativeHostTexturePlan_Free(struct NativeHostTexturePlan *plan)
{
	if (plan == NULL)
		return;
	NativeHostTextureAsset_Free(&plan->asset);
	NativeHostTexturePlan_Init(plan);
}

static int NativeHostTexturePlan_RegistryLookup(void *context,
	const struct NativePresentationSourceKey *sourceKey)
{
	const struct NativePresentationRegistry *registry =
		(const struct NativePresentationRegistry *)context;
	return NativePresentationRegistry_Lookup(registry, sourceKey) != NULL;
}

static int NativeHostTexturePlan_AssetIsValid(const struct NativeHostTextureAsset *asset)
{
	size_t expectedBytes;

	if ((asset == NULL) || (asset->width == 0u) || (asset->height == 0u) ||
		(asset->width > NATIVE_HOST_TEXTURE_ASSET_MAX_WIDTH) ||
		(asset->height > NATIVE_HOST_TEXTURE_ASSET_MAX_HEIGHT) ||
		(asset->rgbaBytes == NULL))
	{
		return 0;
	}
	if ((size_t)asset->width > SIZE_MAX / (size_t)asset->height ||
		((size_t)asset->width * (size_t)asset->height) > SIZE_MAX / 4u)
	{
		return 0;
	}
	expectedBytes = (size_t)asset->width * (size_t)asset->height * 4u;
	return (expectedBytes <= NATIVE_HOST_TEXTURE_ASSET_MAX_RGBA_BYTES) &&
		(asset->rgbaByteCount == expectedBytes);
}

static int NativeHostTexturePlan_ValidateUpscale(const struct NativeHostTextureAsset *asset,
	const struct NativeHostTextureUvRemapFacts *remap, unsigned int *upscaleOut)
{
	unsigned int upscale;

	if ((asset == NULL) || (remap == NULL) || (upscaleOut == NULL) ||
		(remap->sourceUExtent == 0u) || (remap->sourceVExtent == 0u) ||
		((asset->width % remap->sourceUExtent) != 0u) ||
		((asset->height % remap->sourceVExtent) != 0u))
	{
		return 0;
	}
	upscale = asset->width / remap->sourceUExtent;
	if ((upscale == 0u) || (asset->height / remap->sourceVExtent != upscale))
		return 0;
	*upscaleOut = upscale;
	return 1;
}

static void NativeHostTexturePlan_InitSelection(struct NativeHostTexturePlanSelection *selection)
{
	memset(selection, 0, sizeof(*selection));
	selection->decision.selection = NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE;
	selection->decision.fallbackReason = NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE;
	selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_ARGUMENT;
}

int NativeHostTexturePlan_Select(const struct NativeHostTexturePlanInput *input,
	struct NativeHostTexturePlanSelection *selection)
{
	static const unsigned int assetClasses[] = {
		NATIVE_PRESENTATION_ASSET_CLASS_FONT,
		NATIVE_PRESENTATION_ASSET_CLASS_UI_ICON,
		NATIVE_PRESENTATION_ASSET_CLASS_UI_STATIC,
		NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE,
	};
	struct NativeHostTextureBindingInput bindingInput;
	struct NativeHostTextureBindingResult bindingResult;
	const struct NativePresentationRegistryEntry *matchedEntry = NULL;
	unsigned int matchCount = 0u;
	unsigned int index;

	if (selection == NULL)
		return 0;
	NativeHostTexturePlan_InitSelection(selection);
	if ((input == NULL) || (input->registry == NULL))
		return 0;
	if (!NativePresentationRegistry_IsEnabled(input->registry))
	{
		selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_REGISTRY_DISABLED;
		return 0;
	}

	memset(&bindingInput, 0, sizeof(bindingInput));
	bindingInput.source = input->source;
	bindingInput.flags = input->flags;
	bindingInput.lookup = NativeHostTexturePlan_RegistryLookup;
	bindingInput.lookupContext = (void *)input->registry;

	for (index = 0u; index < sizeof(assetClasses) / sizeof(assetClasses[0]); ++index)
	{
		struct NativePresentationSourceKey key;
		struct NativeHostTextureUvRemapFacts remap;
		enum NativeHostTextureBindingError bindingError;

		bindingInput.source.assetClass = assetClasses[index];
		if (!NativeHostTextureBinding_BuildSourceKey(&bindingInput.source, &key, &remap,
			&bindingError))
		{
			selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_SOURCE_KEY;
			return 0;
		}
		if (NativePresentationRegistry_Lookup(input->registry, &key) != NULL)
		{
			matchedEntry = NativePresentationRegistry_Lookup(input->registry, &key);
			++matchCount;
		}
	}
	if (matchCount == 0u)
	{
		selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_NO_EXACT_MATCH;
		return 0;
	}
	if (matchCount != 1u)
	{
		selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_AMBIGUOUS_CLASS_MATCH;
		return 0;
	}

	bindingInput.source.assetClass = matchedEntry->source.assetClass;
	if (!NativeHostTextureBinding_Select(&bindingInput, &bindingResult))
	{
		selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_SOURCE_KEY;
		return 0;
	}
	selection->sourceKey = bindingResult.sourceKey;
	selection->uvRemap = bindingResult.uvRemap;
	selection->decision = bindingResult.decision;
	if (bindingResult.decision.selection != NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE)
	{
		selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_POLICY;
		return 0;
	}
	selection->entry = matchedEntry;
	selection->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE;
	return 1;
}

int NativeHostTexturePlan_ValidateAsset(const struct NativeHostTexturePlanSelection *selection,
	const struct NativeHostTextureAsset *asset, unsigned int *upscaleOut)
{
	if ((selection == NULL) || (selection->entry == NULL) ||
		(selection->decision.selection != NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE) ||
		!NativeHostTexturePlan_AssetIsValid(asset))
	{
		return 0;
	}
	return NativeHostTexturePlan_ValidateUpscale(asset, &selection->uvRemap, upscaleOut);
}

int NativeHostTexturePlan_Build(const struct NativeHostTexturePlanInput *input,
	struct NativeHostTexturePlan *plan)
{
	struct NativeHostTexturePlanSelection selection;

	if (plan == NULL)
		return 0;
	/* The public contract requires Init before Build, making reuse leak-free. */
	NativeHostTexturePlan_Free(plan);
	if ((input == NULL) || (input->load == NULL))
		return 0;
	if (!NativeHostTexturePlan_Select(input, &selection))
	{
		plan->sourceKey = selection.sourceKey;
		plan->uvRemap = selection.uvRemap;
		plan->decision = selection.decision;
		plan->error = selection.error;
		return 0;
	}
	plan->sourceKey = selection.sourceKey;
	plan->uvRemap = selection.uvRemap;
	plan->entry = selection.entry;
	plan->decision = selection.decision;
	if (!input->load(input->loadContext, input->registry, plan->entry, &plan->asset))
	{
		NativeHostTextureAsset_Free(&plan->asset);
		plan->entry = NULL;
		plan->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_LOADER;
		return 0;
	}
	if (!NativeHostTexturePlan_AssetIsValid(&plan->asset))
	{
		NativeHostTextureAsset_Free(&plan->asset);
		plan->entry = NULL;
		plan->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_ASSET;
		return 0;
	}
	if (!NativeHostTexturePlan_ValidateAsset(&selection, &plan->asset, &plan->upscale))
	{
		NativeHostTextureAsset_Free(&plan->asset);
		plan->entry = NULL;
		plan->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_UPSCALE_DIMENSIONS;
		return 0;
	}
	plan->error = NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE;
	return 1;
}

const char *NativeHostTexturePlan_ErrorName(enum NativeHostTexturePlanError error)
{
	switch (error)
	{
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE: return "none";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_ARGUMENT: return "argument";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_REGISTRY_DISABLED: return "registry-disabled";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_SOURCE_KEY: return "source-key";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_NO_EXACT_MATCH: return "no-exact-match";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_AMBIGUOUS_CLASS_MATCH: return "ambiguous-class-match";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_POLICY: return "policy";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_LOADER: return "loader";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_ASSET: return "asset";
	case NATIVE_HOST_TEXTURE_PLAN_ERROR_UPSCALE_DIMENSIONS: return "upscale-dimensions";
	default: return "invalid-plan-error";
	}
}
