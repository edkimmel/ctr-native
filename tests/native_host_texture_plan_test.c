/* Unit coverage for the GL-free local host-texture asset plan. */
#include <platform/native_host_texture_plan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

struct LoaderFixture
{
	int result;
	unsigned int calls;
	unsigned int width;
	unsigned int height;
	size_t byteCount;
	const struct NativePresentationRegistryEntry *entry;
};

static int LoadFixture(void *context, const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry, struct NativeHostTextureAsset *asset)
{
	struct LoaderFixture *fixture = (struct LoaderFixture *)context;
	size_t allocationBytes;
	(void)registry;
	fixture->calls++;
	fixture->entry = entry;
	if (fixture->result == 0)
		return 0;
	NativeHostTextureAsset_Init(asset);
	asset->width = fixture->width;
	asset->height = fixture->height;
	asset->rgbaByteCount = fixture->byteCount;
	allocationBytes = fixture->byteCount != 0u ? fixture->byteCount : 1u;
	asset->rgbaBytes = (uint8_t *)malloc(allocationBytes);
	if (asset->rgbaBytes == NULL)
		return 0;
	memset(asset->rgbaBytes, 0x7f, allocationBytes);
	return 1;
}

static struct NativeHostTexturePlanInput ValidInput(struct NativePresentationRegistry *registry,
	struct LoaderFixture *fixture)
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
	/* Deliberately invalid: Build must search all permitted classes itself. */
	input.source.assetClass = 99u;
	input.flags.primitive = NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_QUAD;
	input.flags.mode = NATIVE_TEXTURE_OVERRIDE_MODE_OPAQUE;
	input.flags.overrideIsWellFormed = 1;
	input.load = LoadFixture;
	input.loadContext = fixture;
	return input;
}

static int AddClassEntry(struct NativePresentationRegistry *registry,
	const struct NativeHostTexturePlanInput *input, unsigned int assetClass)
{
	struct NativeHostTexturePrimitiveSourceFacts source = input->source;
	struct NativeHostTextureUvRemapFacts remap;
	enum NativeHostTextureBindingError error;

	if (registry->entryCount >= NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES)
		return 0;
	source.assetClass = assetClass;
	if (!NativeHostTextureBinding_BuildSourceKey(&source,
		&registry->entries[registry->entryCount].source, &remap, &error))
	{
		return 0;
	}
	strcpy(registry->entries[registry->entryCount].assetPath, "fixture.ctrh");
	registry->entryCount++;
	return 1;
}

static void InitFixture(struct NativePresentationRegistry *registry,
	struct LoaderFixture *loader)
{
	NativePresentationRegistry_Init(registry);
	memset(loader, 0, sizeof(*loader));
	loader->result = 1;
	loader->width = 48u;
	loader->height = 56u;
	loader->byteCount = (size_t)loader->width * loader->height * 4u;
}

int main(void)
{
	struct NativePresentationRegistry registry;
	struct LoaderFixture loader;
	struct NativeHostTexturePlanInput input;
	struct NativeHostTexturePlan plan;
	struct NativeHostTexturePlanSelection selection;

	InitFixture(&registry, &loader);
	input = ValidInput(&registry, &loader);
	CHECK(AddClassEntry(&registry, &input, NATIVE_PRESENTATION_ASSET_CLASS_FONT));
	NativePresentationRegistry_SetEnabled(&registry, 1);
	/* Runtime selection must be allocation/I/O free and not require a loader. */
	input.load = NULL;
	CHECK(NativeHostTexturePlan_Select(&input, &selection));
	CHECK(selection.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE);
	CHECK(selection.entry == &registry.entries[0] && loader.calls == 0u);
	CHECK(selection.sourceKey.assetClass == NATIVE_PRESENTATION_ASSET_CLASS_FONT);
	CHECK(selection.uvRemap.sourceUExtent == 12u && selection.uvRemap.sourceVExtent == 14u);
	input.load = LoadFixture;
	NativeHostTexturePlan_Init(&plan);
	CHECK(NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_NONE);
	CHECK(plan.entry == &registry.entries[0] && loader.entry == plan.entry && loader.calls == 1u);
	CHECK(plan.sourceKey.assetClass == NATIVE_PRESENTATION_ASSET_CLASS_FONT);
	CHECK(plan.sourceKey.x == 194u && plan.sourceKey.y == 266u);
	CHECK(plan.sourceKey.width == 3u && plan.sourceKey.height == 14u);
	CHECK(plan.uvRemap.sourceUExtent == 12u && plan.uvRemap.sourceVExtent == 14u);
	CHECK(plan.upscale == 4u && plan.asset.width == 48u && plan.asset.height == 56u);
	CHECK(plan.decision.selection == NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE);
	NativeHostTexturePlan_Free(&plan);

	/* No enabled exact class match never calls the loader. */
	InitFixture(&registry, &loader);
	input = ValidInput(&registry, &loader);
	CHECK(AddClassEntry(&registry, &input, NATIVE_PRESENTATION_ASSET_CLASS_FONT));
	registry.entries[0].source.tpage++;
	NativePresentationRegistry_SetEnabled(&registry, 1);
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_NO_EXACT_MATCH && loader.calls == 0u);
	NativeHostTexturePlan_Free(&plan);

	/* Multiple class identities for one primitive are ambiguous and rejected. */
	InitFixture(&registry, &loader);
	input = ValidInput(&registry, &loader);
	CHECK(AddClassEntry(&registry, &input, NATIVE_PRESENTATION_ASSET_CLASS_FONT));
	CHECK(AddClassEntry(&registry, &input, NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE));
	NativePresentationRegistry_SetEnabled(&registry, 1);
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_AMBIGUOUS_CLASS_MATCH && loader.calls == 0u);
	NativeHostTexturePlan_Free(&plan);

	/* Disabled registry and hazard-policy rejection are both before loading. */
	InitFixture(&registry, &loader);
	input = ValidInput(&registry, &loader);
	CHECK(AddClassEntry(&registry, &input, NATIVE_PRESENTATION_ASSET_CLASS_UI_STATIC));
	NativePresentationRegistry_SetEnabled(&registry, 1);
	NativePresentationRegistry_SetEnabled(&registry, 0);
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_REGISTRY_DISABLED && loader.calls == 0u);
	NativeHostTexturePlan_Free(&plan);
	NativePresentationRegistry_SetEnabled(&registry, 1);
	input.flags.semitransparent = 1;
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_POLICY && loader.calls == 0u);
	CHECK(plan.decision.fallbackReason == NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY);
	NativeHostTexturePlan_Free(&plan);

	/* Loader failure, malformed callback output, and non-uniform/noninteger dimensions fail closed. */
	InitFixture(&registry, &loader);
	input = ValidInput(&registry, &loader);
	CHECK(AddClassEntry(&registry, &input, NATIVE_PRESENTATION_ASSET_CLASS_UI_ICON));
	NativePresentationRegistry_SetEnabled(&registry, 1);
	loader.result = 0;
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_LOADER && loader.calls == 1u);
	NativeHostTexturePlan_Free(&plan);
	loader.result = 1;
	loader.width = 48u;
	loader.height = 56u;
	loader.byteCount = 1u;
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_ASSET);
	NativeHostTexturePlan_Free(&plan);
	loader.width = 36u; /* three times wide */
	loader.height = 56u; /* four times high */
	loader.byteCount = (size_t)loader.width * loader.height * 4u;
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_UPSCALE_DIMENSIONS);
	NativeHostTexturePlan_Free(&plan);
	loader.width = 49u; /* not an integer source-width multiple */
	loader.height = 56u;
	loader.byteCount = (size_t)loader.width * loader.height * 4u;
	NativeHostTexturePlan_Init(&plan);
	CHECK(!NativeHostTexturePlan_Build(&input, &plan));
	CHECK(plan.error == NATIVE_HOST_TEXTURE_PLAN_ERROR_UPSCALE_DIMENSIONS);
	NativeHostTexturePlan_Free(&plan);

	CHECK(strcmp(NativeHostTexturePlan_ErrorName(NATIVE_HOST_TEXTURE_PLAN_ERROR_UPSCALE_DIMENSIONS),
		"upscale-dimensions") == 0);
	CHECK(strcmp(NativeHostTexturePlan_ErrorName((enum NativeHostTexturePlanError)99),
		"invalid-plan-error") == 0);
	puts("native_host_texture_plan_test: PASS");
	return 0;
}
