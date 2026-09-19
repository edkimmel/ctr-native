#include <platform/native_host_texture_binding.h>

#include <string.h>

#define NATIVE_HOST_TEXTURE_BINDING_VRAM_WIDTH 1024u
#define NATIVE_HOST_TEXTURE_BINDING_VRAM_HEIGHT 512u
#define NATIVE_HOST_TEXTURE_BINDING_UV_LIMIT 255u
#define NATIVE_HOST_TEXTURE_BINDING_TPAGE_LIMIT 0x9ffu

static void NativeHostTextureBinding_SetError(enum NativeHostTextureBindingError *errorOut,
	enum NativeHostTextureBindingError error)
{
	if (errorOut != NULL)
	{
		*errorOut = error;
	}
}

static int NativeHostTextureBinding_IsAssetClass(unsigned int assetClass)
{
	return (assetClass == NATIVE_PRESENTATION_ASSET_CLASS_FONT) ||
	       (assetClass == NATIVE_PRESENTATION_ASSET_CLASS_UI_ICON) ||
	       (assetClass == NATIVE_PRESENTATION_ASSET_CLASS_UI_STATIC);
}

static int NativeHostTextureBinding_TexelsPerVramWord(unsigned int textureMode)
{
	switch (textureMode)
	{
	case NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT: return 4;
	case NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT: return 2;
	case NATIVE_PRESENTATION_TEXTURE_MODE_16_BIT: return 1;
	default: return 0;
	}
}

static int NativeHostTextureBinding_TPageMatchesMode(unsigned int textureMode, unsigned int tpage)
{
	const unsigned int rawMode = (tpage >> 7u) & 3u;

	if (textureMode == NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT)
		return rawMode == 0u;
	if (textureMode == NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT)
		return rawMode == 1u;
	/* NativeGpu's retail path deliberately treats raw mode 3 as 16-bit. */
	return (textureMode == NATIVE_PRESENTATION_TEXTURE_MODE_16_BIT) &&
	       ((rawMode == 2u) || (rawMode == 3u));
}

int NativeHostTextureBinding_BuildSourceKey(
	const struct NativeHostTexturePrimitiveSourceFacts *source,
	struct NativePresentationSourceKey *sourceKey,
	struct NativeHostTextureUvRemapFacts *uvRemap,
	enum NativeHostTextureBindingError *errorOut)
{
	unsigned int texelsPerVramWord;
	unsigned int pageX;
	unsigned int pageY;
	unsigned int sourceX;
	unsigned int sourceY;
	unsigned int sourceWidth;
	unsigned int sourceHeight;

	NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_NONE);
	if ((source == NULL) || (sourceKey == NULL) || (uvRemap == NULL))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT);
		return 0;
	}

	texelsPerVramWord = (unsigned int)NativeHostTextureBinding_TexelsPerVramWord(source->textureMode);
	if (texelsPerVramWord == 0u)
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_TEXTURE_MODE);
		return 0;
	}
	if (source->tpage > NATIVE_HOST_TEXTURE_BINDING_TPAGE_LIMIT)
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE);
		return 0;
	}
	if (!NativeHostTextureBinding_TPageMatchesMode(source->textureMode, source->tpage))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE_MODE);
		return 0;
	}
	if (source->clut > 0xffffu)
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT);
		return 0;
	}
	if (!NativeHostTextureBinding_IsAssetClass(source->assetClass))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_ASSET_CLASS);
		return 0;
	}
	if ((source->uvMinU > NATIVE_HOST_TEXTURE_BINDING_UV_LIMIT) ||
	    (source->uvMinV > NATIVE_HOST_TEXTURE_BINDING_UV_LIMIT) ||
	    (source->uvMaxU > NATIVE_HOST_TEXTURE_BINDING_UV_LIMIT) ||
	    (source->uvMaxV > NATIVE_HOST_TEXTURE_BINDING_UV_LIMIT))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_RANGE);
		return 0;
	}
	if ((source->uvMinU > source->uvMaxU) || (source->uvMinV > source->uvMaxV))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WRAP);
		return 0;
	}
	/* Inclusive bounds must cover complete physical VRAM words. */
	if (((source->uvMinU % texelsPerVramWord) != 0u) ||
	    (((source->uvMaxU + 1u) % texelsPerVramWord) != 0u))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WORD_ALIGNMENT);
		return 0;
	}

	pageX = (source->tpage & 0xfu) << 6u;
	pageY = (source->tpage & 0x10u) != 0u ? 0x100u : 0u;
	sourceX = pageX + source->uvMinU / texelsPerVramWord;
	sourceY = pageY + source->uvMinV;
	sourceWidth = source->uvMaxU / texelsPerVramWord - source->uvMinU / texelsPerVramWord + 1u;
	sourceHeight = source->uvMaxV - source->uvMinV + 1u;
	if ((sourceX >= NATIVE_HOST_TEXTURE_BINDING_VRAM_WIDTH) ||
	    (sourceY >= NATIVE_HOST_TEXTURE_BINDING_VRAM_HEIGHT) ||
	    (sourceWidth == 0u) || (sourceHeight == 0u) ||
	    (sourceX + sourceWidth > NATIVE_HOST_TEXTURE_BINDING_VRAM_WIDTH) ||
	    (sourceY + sourceHeight > NATIVE_HOST_TEXTURE_BINDING_VRAM_HEIGHT))
	{
		NativeHostTextureBinding_SetError(errorOut, NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_RANGE);
		return 0;
	}

	sourceKey->textureMode = (u8)source->textureMode;
	sourceKey->tpage = (u16)source->tpage;
	sourceKey->clut = (u16)source->clut;
	sourceKey->x = (u16)sourceX;
	sourceKey->y = (u16)sourceY;
	sourceKey->width = (u16)sourceWidth;
	sourceKey->height = (u16)sourceHeight;
	sourceKey->assetClass = (u8)source->assetClass;
	uvRemap->sourceU = source->uvMinU;
	uvRemap->sourceV = source->uvMinV;
	uvRemap->sourceUExtent = source->uvMaxU - source->uvMinU + 1u;
	uvRemap->sourceVExtent = sourceHeight;
	return 1;
}

static void NativeHostTextureBinding_InitResult(struct NativeHostTextureBindingResult *result)
{
	memset(result, 0, sizeof(*result));
	result->decision.selection = NATIVE_TEXTURE_OVERRIDE_SELECTION_NATIVE;
	result->decision.fallbackReason = NATIVE_TEXTURE_OVERRIDE_FALLBACK_MALFORMED_OVERRIDE;
	result->error = NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT;
}

int NativeHostTextureBinding_Select(const struct NativeHostTextureBindingInput *input,
	struct NativeHostTextureBindingResult *result)
{
	struct NativeTextureOverrideRequest request;
	int lookupResult = 0;

	if (result == NULL)
	{
		return 0;
	}
	NativeHostTextureBinding_InitResult(result);
	if (input == NULL)
	{
		return 0;
	}
	if (!NativeHostTextureBinding_BuildSourceKey(&input->source, &result->sourceKey,
		&result->uvRemap, &result->error))
	{
		return 0;
	}

	if (input->lookup != NULL)
	{
		lookupResult = input->lookup(input->lookupContext, &result->sourceKey);
		if ((lookupResult != 0) && (lookupResult != 1))
		{
			result->error = NATIVE_HOST_TEXTURE_BINDING_ERROR_LOOKUP_RESULT;
			return 0;
		}
	}

	request.hasExactMatch = lookupResult;
	request.primitive = input->flags.primitive;
	request.mode = input->flags.mode;
	request.semitransparent = input->flags.semitransparent;
	request.activeDrawPageOverlap = input->flags.activeDrawPageOverlap;
	request.pageWrittenHazard = input->flags.pageWrittenHazard;
	request.pageCopyHazard = input->flags.pageCopyHazard;
	request.pageReadbackHazard = input->flags.pageReadbackHazard;
	request.uvIsValid = 1;
	request.overrideIsWellFormed = input->flags.overrideIsWellFormed;
	result->decision = NativeTextureOverridePolicy_Select(&request);
	result->error = NATIVE_HOST_TEXTURE_BINDING_ERROR_NONE;
	return 1;
}

const char *NativeHostTextureBinding_ErrorName(enum NativeHostTextureBindingError error)
{
	switch (error)
	{
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_NONE: return "none";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_ARGUMENT: return "argument";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_TEXTURE_MODE: return "texture-mode";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE: return "tpage";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_TPAGE_MODE: return "tpage-mode";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_ASSET_CLASS: return "asset-class";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_RANGE: return "uv-range";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WRAP: return "uv-wrap";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_UV_WORD_ALIGNMENT: return "uv-word-alignment";
	case NATIVE_HOST_TEXTURE_BINDING_ERROR_LOOKUP_RESULT: return "lookup-result";
	default: return "invalid-binding-error";
	}
}
