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

static int NativeDisplayConfig_IsValid(const struct NativeDisplayConfig *config)
{
	return (config != NULL) &&
	       NativeDisplayConfig_IsRenderScaleSupported(config->renderScale) &&
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
		const char *value = NULL;

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
			value = argv[++index];
		}
		else if (strncmp(arg, "--render-scale=", strlen("--render-scale=")) == 0)
		{
			value = arg + strlen("--render-scale=");
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

		if (!NativeDisplayConfig_ParseScale(value, &candidate.renderScale))
		{
			return 0;
		}
	}

	*config = candidate;
	return 1;
}
