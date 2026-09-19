#ifndef PLATFORM_NATIVE_PRESENTATION_OVERRIDE_CONFIG_H
#define PLATFORM_NATIVE_PRESENTATION_OVERRIDE_CONFIG_H

/*
 * Local host-presentation selection only. Do not add this structure, its
 * fields, or its command-line arguments to MatchConfig, canonical state,
 * replay metadata, or network handshakes. A presentation pack must never
 * affect a simulation frame or compatibility identity.
 *
 * packDirectory, when non-NULL, borrows the corresponding argv string. The
 * caller must keep argv alive for as long as the configuration is in use.
 */
struct NativePresentationOverrideConfig
{
	int enabled;
	const char *packDirectory;
};

#define NATIVE_PRESENTATION_OVERRIDE_CONFIG_DEFAULT_ENABLED 0

void NativePresentationOverrideConfig_SetDefaults(struct NativePresentationOverrideConfig *config);

/*
 * Applies only local presentation arguments transactionally:
 *   --presentation-pack DIRECTORY
 *   --presentation-pack=DIRECTORY
 *   --presentation-overrides
 *   --presentation-overrides=on
 *   --presentation-overrides=off
 *
 * A pack is required when overrides are enabled. Repeated presentation
 * arguments and malformed values fail, leaving config unchanged. Unrelated
 * arguments are ignored for their owning subsystem to parse.
 */
int NativePresentationOverrideConfig_ApplyArgs(
	int argc,
	char *argv[],
	struct NativePresentationOverrideConfig *config);

#endif
