#include <platform/native_presentation_override_config.h>

#include <string.h>

void NativePresentationOverrideConfig_SetDefaults(struct NativePresentationOverrideConfig *config)
{
	if (config != NULL)
	{
		config->enabled = NATIVE_PRESENTATION_OVERRIDE_CONFIG_DEFAULT_ENABLED;
		config->packDirectory = NULL;
	}
}

static int NativePresentationOverrideConfig_IsValid(const struct NativePresentationOverrideConfig *config)
{
	return (config != NULL) &&
	       ((config->enabled == 0) || (config->enabled == 1)) &&
	       ((config->enabled == 0) ||
	        ((config->packDirectory != NULL) && (config->packDirectory[0] != '\0')));
}

static int NativePresentationOverrideConfig_IsPackDirectory(const char *value)
{
	return (value != NULL) && (value[0] != '\0') && (value[0] != '-');
}

static int NativePresentationOverrideConfig_ParseEnabled(const char *value, int *enabled)
{
	if ((value == NULL) || (enabled == NULL))
	{
		return 0;
	}

	if (strcmp(value, "on") == 0)
	{
		*enabled = 1;
		return 1;
	}

	if (strcmp(value, "off") == 0)
	{
		*enabled = 0;
		return 1;
	}

	return 0;
}

int NativePresentationOverrideConfig_ApplyArgs(
	int argc,
	char *argv[],
	struct NativePresentationOverrideConfig *config)
{
	struct NativePresentationOverrideConfig candidate;
	int sawPack = 0;
	int sawEnabled = 0;

	if ((config == NULL) || (argc < 0) || ((argc > 0) && (argv == NULL)))
	{
		return 0;
	}

	candidate = *config;
	if (!NativePresentationOverrideConfig_IsValid(&candidate))
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

		if (strcmp(arg, "--presentation-pack") == 0)
		{
			if (sawPack || (index + 1 >= argc) ||
			    !NativePresentationOverrideConfig_IsPackDirectory(argv[index + 1]))
			{
				return 0;
			}
			candidate.packDirectory = argv[++index];
			sawPack = 1;
		}
		else if (strncmp(arg, "--presentation-pack=", strlen("--presentation-pack=")) == 0)
		{
			value = arg + strlen("--presentation-pack=");
			if (sawPack || !NativePresentationOverrideConfig_IsPackDirectory(value))
			{
				return 0;
			}
			candidate.packDirectory = value;
			sawPack = 1;
		}
		else if (strcmp(arg, "--presentation-overrides") == 0)
		{
			if (sawEnabled)
			{
				return 0;
			}
			candidate.enabled = 1;
			sawEnabled = 1;
		}
		else if (strncmp(arg, "--presentation-overrides=", strlen("--presentation-overrides=")) == 0)
		{
			value = arg + strlen("--presentation-overrides=");
			if (sawEnabled || !NativePresentationOverrideConfig_ParseEnabled(value, &candidate.enabled))
			{
				return 0;
			}
			sawEnabled = 1;
		}
	}

	if (!NativePresentationOverrideConfig_IsValid(&candidate))
	{
		return 0;
	}

	*config = candidate;
	return 1;
}
