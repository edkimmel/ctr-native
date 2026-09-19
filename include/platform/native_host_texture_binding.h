#ifndef PLATFORM_NATIVE_HOST_TEXTURE_BINDING_H
#define PLATFORM_NATIVE_HOST_TEXTURE_BINDING_H

/*
 * Pure selection seam for the future local host-texture draw path.
 *
 * This module translates facts captured from one retail textured primitive
 * into the exact registry identity and the source-UV facts a host renderer
 * will need.  It owns neither GL objects nor renderer state.  In particular,
 * it does not load assets and it cannot make a draw eligible by itself: the
 * caller injects an exact registry lookup and the command hazard flags.
 *
 * `sourceKey` is expressed in PS1 VRAM words, matching the presentation
 * manifest and trace.  Since 4- and 8-bit texels share a VRAM word, this
 * module accepts those formats only at whole-word U bounds.  That prevents
 * two different sub-word UV rectangles from silently sharing one key.
 */

#include <platform/native_presentation_registry.h>
#include <platform/native_texture_override_policy.h>

enum NativeHostTextureBindingError
{
	NATIVE_HOST_TEXTURE_BINDING_ERROR_NONE = 0,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_TEXTURE_MODE,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE_MODE,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_ASSET_CLASS,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_RANGE,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WRAP,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WORD_ALIGNMENT,
	NATIVE_HOST_TEXTURE_BINDING_ERROR_LOOKUP_RESULT,
};

/* UV values are inclusive retail texel bounds, not normalized host UVs. */
struct NativeHostTexturePrimitiveSourceFacts
{
	unsigned int textureMode;
	unsigned int tpage;
	unsigned int clut;
	unsigned int uvMinU;
	unsigned int uvMinV;
	unsigned int uvMaxU;
	unsigned int uvMaxV;
	unsigned int assetClass;
};

/*
 * The future host draw maps each original UV with:
 *   localU = (u - sourceU) / sourceUExtent
 *   localV = (v - sourceV) / sourceVExtent
 * The values remain integers here to keep the selection seam renderer-free.
 */
struct NativeHostTextureUvRemapFacts
{
	unsigned int sourceU;
	unsigned int sourceV;
	unsigned int sourceUExtent;
	unsigned int sourceVExtent;
};

/* Returns exactly 0 (not found) or 1 (exact, enabled, usable match). */
typedef int (*NativeHostTextureBindingLookupFn)(void *context,
	const struct NativePresentationSourceKey *sourceKey);

/* These are copied into NativeTextureOverridePolicy_Select after lookup. */
struct NativeHostTextureBindingFlags
{
	enum NativeTextureOverridePrimitive primitive;
	enum NativeTextureOverrideMode mode;
	int semitransparent;
	int activeDrawPageOverlap;
	int pageWrittenHazard;
	int pageCopyHazard;
	int pageReadbackHazard;
	int overrideIsWellFormed;
};

struct NativeHostTextureBindingInput
{
	struct NativeHostTexturePrimitiveSourceFacts source;
	struct NativeHostTextureBindingFlags flags;
	NativeHostTextureBindingLookupFn lookup;
	void *lookupContext;
};

struct NativeHostTextureBindingResult
{
	struct NativePresentationSourceKey sourceKey;
	struct NativeHostTextureUvRemapFacts uvRemap;
	struct NativeTextureOverrideDecision decision;
	enum NativeHostTextureBindingError error;
};

/*
 * Builds a manifest-compatible exact identity from explicit retail primitive
 * facts.  It rejects invalid source values and U ranges that would alias a
 * partial 4/8-bit VRAM word.  No lookup is invoked.
 */
int NativeHostTextureBinding_BuildSourceKey(
	const struct NativeHostTexturePrimitiveSourceFacts *source,
	struct NativePresentationSourceKey *sourceKey,
	struct NativeHostTextureUvRemapFacts *uvRemap,
	enum NativeHostTextureBindingError *errorOut);

/*
 * Builds the key, invokes the optional exact lookup once, and applies the
 * existing pure override policy.  Invalid source facts do not invoke lookup
 * and return zero.  A missing lookup is a normal fail-closed no-match.  A
 * callback result other than 0/1 is malformed and remains native.
 */
int NativeHostTextureBinding_Select(const struct NativeHostTextureBindingInput *input,
	struct NativeHostTextureBindingResult *result);

const char *NativeHostTextureBinding_ErrorName(enum NativeHostTextureBindingError error);

#endif
