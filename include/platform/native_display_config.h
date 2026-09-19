#ifndef PLATFORM_NATIVE_DISPLAY_CONFIG_H
#define PLATFORM_NATIVE_DISPLAY_CONFIG_H

/*
 * Host presentation settings are intentionally local-only.  Do not add this
 * structure to MatchConfig, canonical state, replay headers, or any network
 * handshake: changing a render scale must never affect a simulation frame.
 */
struct NativeDisplayConfig
{
	int renderScale;
	int fullscreen;
};

#define NATIVE_DISPLAY_CONFIG_DEFAULT_RENDER_SCALE 1
#define NATIVE_DISPLAY_CONFIG_DEFAULT_FULLSCREEN    0

void NativeDisplayConfig_SetDefaults(struct NativeDisplayConfig *config);
int NativeDisplayConfig_IsRenderScaleSupported(int renderScale);

/*
 * Applies local command-line overrides transactionally.  The parser owns
 * `--render-scale N`, `--render-scale=N`, `--fullscreen`, and `--windowed`;
 * unrelated options are left for their respective subsystems. Returns zero on
 * malformed/unsupported local display arguments and leaves `config`
 * unchanged in that case.
 */
int NativeDisplayConfig_ApplyArgs(int argc, char *argv[], struct NativeDisplayConfig *config);

#endif
