#include <platform/native_display_config.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void NativeDisplayConfig_SetDefaults(struct NativeDisplayConfig *config)
{
	if (config != NULL)
	{
		config->renderScale = NATIVE_DISPLAY_CONFIG_DEFAULT_RENDER_SCALE;
		config->fullscreen = NATIVE_DISPLAY_CONFIG_DEFAULT_FULLSCREEN;
		config->textureFilter = NATIVE_DISPLAY_CONFIG_DEFAULT_TEXTURE_FILTER;
	}
}

int NativeDisplayConfig_IsRenderScaleSupported(int renderScale)
{
	switch (renderScale)
	{
	case 1:
	case 2:
	case 3:
	case 4:
	case 6:
	case 8:
		return 1;
	default:
		return 0;
	}
}

int NativeDisplayConfig_IsTextureFilterSupported(int filter)
{
	switch (filter)
	{
	case NATIVE_TEXTURE_FILTER_NEAREST:
	case NATIVE_TEXTURE_FILTER_BILINEAR:
		return 1;
	default:
		return 0;
	}
}

const char *NativeDisplayConfig_TextureFilterName(int filter)
{
	switch (filter)
	{
	case NATIVE_TEXTURE_FILTER_NEAREST:
		return "nearest";
	case NATIVE_TEXTURE_FILTER_BILINEAR:
		return "bilinear";
	default:
		return "unknown";
	}
}

static int NativeDisplayConfig_IsValid(const struct NativeDisplayConfig *config)
{
	return (config != NULL) &&
	       NativeDisplayConfig_IsRenderScaleSupported(config->renderScale) &&
	       NativeDisplayConfig_IsTextureFilterSupported(config->textureFilter) &&
	       ((config->fullscreen == 0) || (config->fullscreen == 1));
}

static int NativeDisplayConfig_ParseScale(const char *text, int *renderScale)
{
	char *end = NULL;
	long value;

	if ((text == NULL) || (text[0] == '\0') || (renderScale == NULL))
	{
		return 0;
	}

	errno = 0;
	value = strtol(text, &end, 10);
	if ((errno == ERANGE) || (end == text) || (end[0] != '\0') || (value < INT_MIN) || (value > INT_MAX))
	{
		return 0;
	}

	if (!NativeDisplayConfig_IsRenderScaleSupported((int)value))
	{
		return 0;
	}

	*renderScale = (int)value;
	return 1;
}

int NativeDisplayConfig_ParseTextureFilter(const char *text, int *outFilter)
{
	if ((text == NULL) || (text[0] == '\0') || (outFilter == NULL))
	{
		return 0;
	}

	if (strcmp(text, "nearest") == 0)
	{
		*outFilter = NATIVE_TEXTURE_FILTER_NEAREST;
		return 1;
	}

	if (strcmp(text, "bilinear") == 0)
	{
		*outFilter = NATIVE_TEXTURE_FILTER_BILINEAR;
		return 1;
	}

	return 0;
}

int NativeDisplayConfig_ApplyArgs(int argc, char *argv[], struct NativeDisplayConfig *config)
{
	struct NativeDisplayConfig candidate;

	if (config == NULL)
	{
		return 0;
	}

	candidate = *config;
	if (!NativeDisplayConfig_IsValid(&candidate))
	{
		return 0;
	}

	for (int index = 1; index < argc; index++)
	{
		const char *arg = argv[index];
		const char *scaleValue = NULL;
		const char *filterValue = NULL;

		if (arg == NULL)
		{
			return 0;
		}

		if (strcmp(arg, "--render-scale") == 0)
		{
			if ((index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '-'))
			{
				return 0;
			}
			scaleValue = argv[++index];
		}
		else if (strncmp(arg, "--render-scale=", strlen("--render-scale=")) == 0)
		{
			scaleValue = arg + strlen("--render-scale=");
		}
		else if (strcmp(arg, "--texture-filter") == 0)
		{
			if ((index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '-'))
			{
				return 0;
			}
			filterValue = argv[++index];
		}
		else if (strncmp(arg, "--texture-filter=", strlen("--texture-filter=")) == 0)
		{
			filterValue = arg + strlen("--texture-filter=");
		}
		else if (strcmp(arg, "--fullscreen") == 0)
		{
			candidate.fullscreen = 1;
			continue;
		}
		else if (strcmp(arg, "--windowed") == 0)
		{
			candidate.fullscreen = 0;
			continue;
		}
		else
		{
			continue;
		}

		if (scaleValue != NULL)
		{
			if (!NativeDisplayConfig_ParseScale(scaleValue, &candidate.renderScale))
			{
				return 0;
			}
		}
		else if (!NativeDisplayConfig_ParseTextureFilter(filterValue, &candidate.textureFilter))
		{
			return 0;
		}
	}

	*config = candidate;
	return 1;
}
