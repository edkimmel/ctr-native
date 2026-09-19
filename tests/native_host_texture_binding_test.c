/* Unit coverage for the renderer-free host-texture binding seam. */
#include <platform/native_host_texture_binding.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

struct LookupFixture
{
	int result;
	unsigned int calls;
	struct NativePresentationSourceKey lastKey;
};

static int Lookup(void *context, const struct NativePresentationSourceKey *sourceKey)
{
	struct LookupFixture *fixture = (struct LookupFixture *)context;
	fixture->calls++;
	fixture->lastKey = *sourceKey;
	return fixture->result;
}

static struct NativeHostTextureBindingInput ValidFontGt4(struct LookupFixture *fixture)
{
	struct NativeHostTextureBindingInput input;

	memset(&input, 0, sizeof(input));
	/* A normal 12x14 4-bit font glyph rectangle, backed by three VRAM words. */
	input.source.textureMode = NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT;
	input.source.tpage = 0x13u; /* X page 3, Y page 1, raw 4-bit mode. */
	input.source.clut = 0x345u;
	input.source.uvMinU = 8u;
	input.source.uvMinV = 10u;
	input.source.uvMaxU = 19u;
	input.source.uvMaxV = 23u;
	input.source.assetClass = NATIVE_PRESENTATION_ASSET_CLASS_FONT;
	input.flags.primitive = NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_QUAD;
	input.flags.mode = NATIVE_TEXTURE_OVERRIDE_MODE_OPAQUE;
	input.flags.overrideIsWellFormed = 1;
	input.lookup = Lookup;
	input.lookupContext = fixture;
	return input;
}

static int CheckNative(const struct NativeHostTextureBindingResult *result,
	enum NativeTextureOverrideFallbackReason reason)
{
	CHECK(result->error == NATIVE_HOST_TEXTURE_BINDING_ERROR_NONE);
	CHECK(result->decision.selection == NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE);
	CHECK(result->decision.fallbackReason == reason);
	return 0;
}

int main(void)
{
	struct LookupFixture fixture;
	struct NativeHostTextureBindingInput input;
	struct NativeHostTextureBindingResult result;
	struct NativePresentationSourceKey key;
	struct NativeHostTextureUvRemapFacts remap;
	enum NativeHostTextureBindingError error;

	memset(&fixture, 0, sizeof(fixture));
	fixture.result = 1;
	input = ValidFontGt4(&fixture);
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_NONE);
	CHECK(result.decision.selection == NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE);
	CHECK(result.decision.fallbackReason == NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE);
	CHECK(fixture.calls == 1u);
	CHECK(result.sourceKey.textureMode == NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT);
	CHECK(result.sourceKey.tpage == 0x13u && result.sourceKey.clut == 0x345u);
	CHECK(result.sourceKey.x == 194u && result.sourceKey.y == 266u);
	CHECK(result.sourceKey.width == 3u && result.sourceKey.height == 14u);
	CHECK(result.sourceKey.assetClass == NATIVE_PRESENTATION_ASSET_CLASS_FONT);
	CHECK(memcmp(&fixture.lastKey, &result.sourceKey, sizeof(result.sourceKey)) == 0);
	CHECK(result.uvRemap.sourceU == 8u && result.uvRemap.sourceV == 10u);
	CHECK(result.uvRemap.sourceUExtent == 12u && result.uvRemap.sourceVExtent == 14u);

	/* Exact source-key math for every raw retail texture format. */
	input.source.textureMode = NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT;
	input.source.tpage = 0x81u;
	input.source.uvMinU = 6u;
	input.source.uvMaxU = 21u;
	input.source.uvMinV = 4u;
	input.source.uvMaxV = 8u;
	input.source.assetClass = NATIVE_PRESENTATION_ASSET_CLASS_UI_ICON;
	CHECK(NativeHostTextureBinding_BuildSourceKey(&input.source, &key, &remap, &error));
	CHECK(key.x == 67u && key.y == 4u && key.width == 8u && key.height == 5u);
	CHECK(remap.sourceU == 6u && remap.sourceUExtent == 16u);
	input.source.textureMode = NATIVE_PRESENTATION_TEXTURE_MODE_16_BIT;
	input.source.tpage = 0x102u; /* raw mode 2 and X page 2 */
	input.source.uvMinU = 13u;
	input.source.uvMaxU = 25u;
	input.source.uvMinV = 1u;
	input.source.uvMaxV = 2u;
	input.source.assetClass = NATIVE_PRESENTATION_ASSET_CLASS_UI_STATIC;
	CHECK(NativeHostTextureBinding_BuildSourceKey(&input.source, &key, &remap, &error));
	CHECK(key.x == 141u && key.y == 1u && key.width == 13u && key.height == 2u);
	/* The native retail parser intentionally canonicalizes raw TPAGE mode 3 to 16-bit. */
	input.source.tpage = 0x182u;
	CHECK(NativeHostTextureBinding_BuildSourceKey(&input.source, &key, &remap, &error));

	/* Invalid primitive source facts never invoke the registry lookup. */
	memset(&fixture, 0, sizeof(fixture));
	fixture.result = 1;
	input = ValidFontGt4(&fixture);
	input.source.textureMode = 3u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_TEXTURE_MODE && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.tpage = 0xa00u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.tpage = 0x93u; /* raw 8-bit mode conflicts with explicit 4-bit fact. */
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE_MODE && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.assetClass = 99u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_ASSET_CLASS && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.uvMaxV = 256u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_RANGE && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.uvMinU = 20u;
	input.source.uvMaxU = 19u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WRAP && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.uvMinU = 9u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WORD_ALIGNMENT && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.uvMaxU = 18u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WORD_ALIGNMENT && fixture.calls == 0u);
	input = ValidFontGt4(&fixture);
	input.source.textureMode = NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT;
	input.source.tpage = 0x93u;
	input.source.uvMinU = 7u;
	input.source.uvMaxU = 20u;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WORD_ALIGNMENT && fixture.calls == 0u);

	/* Explicit build failures and NULL output/input are fail-closed too. */
	input = ValidFontGt4(&fixture);
	CHECK(!NativeHostTextureBinding_BuildSourceKey(NULL, &key, &remap, &error));
	CHECK(error == NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT);
	CHECK(!NativeHostTextureBinding_BuildSourceKey(&input.source, NULL, &remap, &error));
	CHECK(error == NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT);
	CHECK(!NativeHostTextureBinding_Select(NULL, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT);
	CHECK(!NativeHostTextureBinding_Select(&input, NULL));

	/* Lookup is optional and exact-only; malformed callback results fail closed. */
	input = ValidFontGt4(&fixture);
	input.lookup = NULL;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH) == 0);
	fixture.result = 0;
	input = ValidFontGt4(&fixture);
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH) == 0);
	fixture.result = 2;
	CHECK(!NativeHostTextureBinding_Select(&input, &result));
	CHECK(result.error == NATIVE_HOST_TEXTURE_BINDING_ERROR_LOOKUP_RESULT);

	/* Every policy rejection continues to select the retail/native path. */
	fixture.result = 1;
	input = ValidFontGt4(&fixture);
	input.flags.primitive = (enum NativeTextureOverridePrimitive)99;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_PRIMITIVE) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.mode = (enum NativeTextureOverrideMode)99;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_MODE) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.semitransparent = 1;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.activeDrawPageOverlap = 1;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_ACTIVE_DRAW_PAGE_OVERLAP) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.pageWrittenHazard = 1;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_WRITTEN_HAZARD) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.pageCopyHazard = 1;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.pageReadbackHazard = 1;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_READBACK_HAZARD) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.overrideIsWellFormed = 0;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE) == 0);
	input = ValidFontGt4(&fixture);
	input.flags.pageWrittenHazard = 2;
	CHECK(NativeHostTextureBinding_Select(&input, &result));
	CHECK(CheckNative(&result, NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE) == 0);
	CHECK(strcmp(NativeHostTextureBinding_ErrorName(NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WRAP), "uv-wrap") == 0);
	CHECK(strcmp(NativeHostTextureBinding_ErrorName((enum NativeHostTextureBindingError)99), "invalid-binding-error") == 0);

	puts("native_host_texture_binding_test: PASS");
	return 0;
}
