/*
 * Integration-style validation fixture for the two pure M2 seams.  This is
 * intentionally not a renderer test: the registry performs an exact-key
 * lookup and the policy turns only that lookup result plus command facts into
 * an override/native decision.
 */
#include <platform/native_presentation_registry.h>
#include <platform/native_texture_override_policy.h>

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

static const char *const s_root = "native_presentation_override_policy_integration_pack";
static const char *const s_asset = "native_presentation_override_policy_integration_pack/font.rgba";
static const char *const s_manifest =
	"ctr-native-presentation-manifest\t1\n"
	"entry\t4\t32\t64\t8\t16\t12\t14\tfont\tfont.rgba\n";

static int MakePack(void)
{
	FILE *file;

	(void)remove(s_asset);
	(void)REMOVE_DIR(s_root);
	if (MAKE_DIR(s_root) != 0)
		return 0;
#if defined(_WIN32)
	if (fopen_s(&file, s_asset, "wb") != 0)
		file = NULL;
#else
	file = fopen(s_asset, "wb");
#endif
	if (file == NULL)
		return 0;
	fputs("fixture", file);
	fclose(file);
	return 1;
}

static void RemovePack(void)
{
	(void)remove(s_asset);
	(void)REMOVE_DIR(s_root);
}

static struct NativeTextureOverrideRequest RequestFromLookup(
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationSourceKey *source)
{
	struct NativeTextureOverrideRequest request;

	request.hasExactMatch = NativePresentationRegistry_Lookup(registry, source) != NULL;
	request.primitive = NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_QUAD;
	request.mode = NATIVE_TEXTURE_OVERRIDE_MODE_OPAQUE;
	request.semitransparent = 0;
	request.activeDrawPageOverlap = 0;
	request.pageWrittenHazard = 0;
	request.pageCopyHazard = 0;
	request.pageReadbackHazard = 0;
	request.uvIsValid = 1;
	request.overrideIsWellFormed = 1;
	return request;
}

static int CheckDecision(const struct NativePresentationRegistry *registry,
	const struct NativePresentationSourceKey *source,
	const struct NativeTextureOverrideRequest *request,
	enum NativeTextureOverrideSelection expectedSelection,
	enum NativeTextureOverrideFallbackReason expectedReason)
{
	struct NativeTextureOverrideRequest candidate = *request;
	struct NativeTextureOverrideDecision decision;

	candidate.hasExactMatch = NativePresentationRegistry_Lookup(registry, source) != NULL;
	decision = NativeTextureOverridePolicy_Select(&candidate);
	CHECK(decision.selection == expectedSelection);
	CHECK(decision.fallbackReason == expectedReason);
	return 0;
}

int main(void)
{
	struct NativePresentationRegistry registry;
	struct NativePresentationSourceKey exact = {4, 32, 64, 8, 16, 12, 14,
		NATIVE_PRESENTATION_ASSET_CLASS_FONT};
	struct NativePresentationSourceKey nonExact = {4, 32, 64, 9, 16, 12, 14,
		NATIVE_PRESENTATION_ASSET_CLASS_FONT};
	struct NativeTextureOverrideRequest request;

	CHECK(MakePack());
	NativePresentationRegistry_Init(&registry);
	CHECK(NativePresentationRegistry_LoadManifest(&registry, s_root, s_manifest, strlen(s_manifest)));
	CHECK(NativePresentationRegistry_FindExact(&registry, &exact) != NULL);
	CHECK(NativePresentationRegistry_Lookup(&registry, &exact) == NULL);

	/* A loaded-but-disabled registry cannot select a replacement. */
	request = RequestFromLookup(&registry, &exact);
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH) == 0);

	NativePresentationRegistry_SetEnabled(&registry, 1);
	CHECK(NativePresentationRegistry_Lookup(&registry, &exact) != NULL);
	request = RequestFromLookup(&registry, &exact);
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE) == 0);

	/* A one-pixel source-key difference must remain native even when enabled. */
	CHECK(NativePresentationRegistry_Lookup(&registry, &nonExact) == NULL);
	CHECK(CheckDecision(&registry, &nonExact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH) == 0);

	request.semitransparent = 1;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY) == 0);
	request.semitransparent = 0;
	request.activeDrawPageOverlap = 1;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_ACTIVE_DRAW_PAGE_OVERLAP) == 0);
	request.activeDrawPageOverlap = 0;
	request.pageWrittenHazard = 1;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_WRITTEN_HAZARD) == 0);
	request.pageWrittenHazard = 0;
	request.pageCopyHazard = 1;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD) == 0);
	request.pageCopyHazard = 0;
	request.pageReadbackHazard = 1;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_READBACK_HAZARD) == 0);
	request.pageReadbackHazard = 0;
	request.uvIsValid = 0;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_BAD_UV) == 0);
	request.uvIsValid = 1;
	request.overrideIsWellFormed = 0;
	CHECK(CheckDecision(&registry, &exact, &request,
		NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE) == 0);

	RemovePack();
	puts("native_presentation_override_policy_integration_test: PASS");
	return 0;
}
