#include <platform/native_texture_override_policy.h>

static int NativeTextureOverridePolicy_IsBoolean(int value)
{
	return (value == 0) || (value == 1);
}

static struct NativeTextureOverrideDecision NativeTextureOverridePolicy_Native(
	enum NativeTextureOverrideFallbackReason reason)
{
	struct NativeTextureOverrideDecision decision;

	decision.selection = NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE;
	decision.fallbackReason = reason;
	return decision;
}

static int NativeTextureOverridePolicy_IsSupportedPrimitive(
	enum NativeTextureOverridePrimitive primitive)
{
	return (primitive == NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_TRIANGLE) ||
	       (primitive == NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_QUAD);
}

static int NativeTextureOverridePolicy_IsSupportedMode(enum NativeTextureOverrideMode mode)
{
	return mode == NATIVE_TEXTURE_OVERRIDE_MODE_OPAQUE;
}

static int NativeTextureOverridePolicy_IsWellFormedRequest(
	const struct NativeTextureOverrideRequest *request)
{
	return (request != 0) &&
	       NativeTextureOverridePolicy_IsBoolean(request->hasExactMatch) &&
	       NativeTextureOverridePolicy_IsBoolean(request->semitransparent) &&
	       NativeTextureOverridePolicy_IsBoolean(request->activeDrawPageOverlap) &&
	       NativeTextureOverridePolicy_IsBoolean(request->pageWrittenHazard) &&
	       NativeTextureOverridePolicy_IsBoolean(request->pageCopyHazard) &&
	       NativeTextureOverridePolicy_IsBoolean(request->pageReadbackHazard) &&
	       NativeTextureOverridePolicy_IsBoolean(request->uvIsValid) &&
	       NativeTextureOverridePolicy_IsBoolean(request->overrideIsWellFormed);
}

struct NativeTextureOverrideDecision NativeTextureOverridePolicy_Select(
	const struct NativeTextureOverrideRequest *request)
{
	struct NativeTextureOverrideDecision selected;

	if (!NativeTextureOverridePolicy_IsWellFormedRequest(request) ||
	    (request->overrideIsWellFormed == 0))
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE);
	}

	if (request->hasExactMatch == 0)
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH);
	}

	if (!NativeTextureOverridePolicy_IsSupportedPrimitive(request->primitive))
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_PRIMITIVE);
	}

	if (!NativeTextureOverridePolicy_IsSupportedMode(request->mode))
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_MODE);
	}

	if (request->semitransparent != 0)
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY);
	}

	if (request->activeDrawPageOverlap != 0)
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_ACTIVE_DRAW_PAGE_OVERLAP);
	}

	if (request->pageWrittenHazard != 0)
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_WRITTEN_HAZARD);
	}

	if (request->pageCopyHazard != 0)
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD);
	}

	if (request->pageReadbackHazard != 0)
	{
		return NativeTextureOverridePolicy_Native(
			NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_READBACK_HAZARD);
	}

	if (request->uvIsValid == 0)
	{
		return NativeTextureOverridePolicy_Native(NATIVE_TEXTURE_OVERRIDE_FALLBACK_BAD_UV);
	}

	selected.selection = NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE;
	selected.fallbackReason = NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE;
	return selected;
}

const char *NativeTextureOverridePolicy_FallbackReasonName(
	enum NativeTextureOverrideFallbackReason reason)
{
	switch (reason)
	{
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE:
		return "none";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE:
		return "malformed-override";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH:
		return "no-exact-match";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_PRIMITIVE:
		return "unsupported-primitive";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_MODE:
		return "unsupported-mode";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY:
		return "semitransparency";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_ACTIVE_DRAW_PAGE_OVERLAP:
		return "active-draw-page-overlap";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_WRITTEN_HAZARD:
		return "page-written-hazard";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD:
		return "page-copy-hazard";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_READBACK_HAZARD:
		return "page-readback-hazard";
	case NATIVE_TEXTURE_OVERRIDE_FALLBACK_BAD_UV:
		return "bad-uv";
	default:
		return "invalid-fallback-reason";
	}
}
