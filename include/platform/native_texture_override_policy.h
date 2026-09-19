#ifndef PLATFORM_NATIVE_TEXTURE_OVERRIDE_POLICY_H
#define PLATFORM_NATIVE_TEXTURE_OVERRIDE_POLICY_H

/*
 * Local presentation-only override gate.  This deliberately accepts only
 * command facts: it does not know about GL objects, game state, replay data,
 * or texture asset ownership.  A caller must keep the retail VRAM path when
 * this policy selects NATIVE.
 */

enum NativeTextureOverridePrimitive
{
	NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_TRIANGLE = 0,
	NATIVE_TEXTURE_OVERRIDE_PRIMITIVE_TEXTURED_QUAD = 1
};

enum NativeTextureOverrideMode
{
	/* The only initial mode.  Lighting/modulation and blend modes are native. */
	NATIVE_TEXTURE_OVERRIDE_MODE_OPAQUE = 0
};

enum NativeTextureOverrideSelection
{
	NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE = 0,
	NATIVE_TEXTURE_OVERRIDE_SELECTION_OVERRIDE = 1
};

/*
 * This is part of the validation evidence.  Do not collapse failures to a
 * boolean: the caller/test harness needs to report why retail rendering won.
 */
enum NativeTextureOverrideFallbackReason
{
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_NONE = 0,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_NO_EXACT_MATCH,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_PRIMITIVE,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_UNSUPPORTED_MODE,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_SEMITRANSPARENCY,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_ACTIVE_DRAW_PAGE_OVERLAP,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_WRITTEN_HAZARD,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_COPY_HAZARD,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_PAGE_READBACK_HAZARD,
	NATIVE_TEXTURE_OVERRIDE_FALLBACK_BAD_UV
};

/*
 * All boolean fields are required to be exactly zero or one.  Unknown enum
 * values and malformed boolean data fail closed.  `hasExactMatch` means the
 * override's immutable retail identity exactly matches the draw's source
 * tpage/CLUT/rect identity; partial, fuzzy, and hash-only matches are false.
 */
struct NativeTextureOverrideRequest
{
	int hasExactMatch;
	enum NativeTextureOverridePrimitive primitive;
	enum NativeTextureOverrideMode mode;
	int semitransparent;
	int activeDrawPageOverlap;
	int pageWrittenHazard;
	int pageCopyHazard;
	int pageReadbackHazard;
	int uvIsValid;
	int overrideIsWellFormed;
};

struct NativeTextureOverrideDecision
{
	enum NativeTextureOverrideSelection selection;
	enum NativeTextureOverrideFallbackReason fallbackReason;
};

/*
 * Pure fail-closed command policy.  An override is selected only for an exact
 * match, an initial-supported opaque primitive/mode, valid UVs, a well-formed
 * replacement, and no live VRAM feedback hazard.  NULL is malformed.
 */
struct NativeTextureOverrideDecision NativeTextureOverridePolicy_Select(
	const struct NativeTextureOverrideRequest *request);

const char *NativeTextureOverridePolicy_FallbackReasonName(
	enum NativeTextureOverrideFallbackReason reason);

#endif
