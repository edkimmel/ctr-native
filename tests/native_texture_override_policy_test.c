#include <platform/native_texture_override_policy.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static struct NativeTextureOverrideRequest ValidRequest(void)
{
	struct NativeTextureOverrideRequest request;

	request.hasExactMatch = 1;
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

static int CheckDecision(const struct NativeTextureOverrideRequest *request,
	enum NativeTextureOverrideSelection selection,
	enum NativeTextureOverrideFallbackReason reason)
{
	struct NativeTextureOverrideDecision decision = NativeTextureOverridePolicy_Select(request);

	CHECK(decision.selection == selection);
	CHECK(decision.fallbackReason == reason);
	return 0;
}

int main(void)
{
	struct NativeTextureOverrideRequest request = ValidRequest();

	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE) == 0);
	request.primitive = NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_TRIANGLE;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE) == 0);
	request = ValidRequest();

	request.hasExactMatch = 0;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH) == 0);
	request = ValidRequest();
	request.primitive = (enum NativeTextureOverridePrimitive)99;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_PRIMITIVE) == 0);
	request = ValidRequest();
	request.mode = (enum NativeTextureOverrideMode)99;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_MODE) == 0);
	request = ValidRequest();
	request.semitransparent = 1;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY) == 0);
	request = ValidRequest();
	request.activeDrawPageOverlap = 1;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_ACTIVE_DRAW_PAGE_OVERLAP) == 0);
	request = ValidRequest();
	request.pageWrittenHazard = 1;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_WRITTEN_HAZARD) == 0);
	request = ValidRequest();
	request.pageCopyHazard = 1;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD) == 0);
	request = ValidRequest();
	request.pageReadbackHazard = 1;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_READBACK_HAZARD) == 0);
	request = ValidRequest();
	request.uvIsValid = 0;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_BAD_UV) == 0);
	request = ValidRequest();
	request.overrideIsWellFormed = 0;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE) == 0);
	request = ValidRequest();
	request.pageWrittenHazard = 2;
	CHECK(CheckDecision(&request, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE) == 0);
	CHECK(CheckDecision(0, NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE,
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE) == 0);

	CHECK(strcmp(NativeTextureOverridePolicy_FallbackReasonName(
		NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD), "page-copy-hazard") == 0);
	CHECK(strcmp(NativeTextureOverridePolicy_FallbackReasonName(
		(enum NativeTextureOverrideFallbackReason)99), "invalid-fallback-reason") == 0);

	puts("native_texture_override_policy_test: PASS");
	return 0;
}
