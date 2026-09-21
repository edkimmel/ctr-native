#ifndef PLATFORM_NATIVE_DISPLAY_CONFIG_H
#define PLATFORM_NATIVE_DISPLAY_CONFIG_H

/*
 * Host presentation settings are intentionally local-only.  Do not add this
 * structure to MatchConfig, canonical state, replay headers, or any network
 * handshake: changing a render scale or texture filter must never affect a
 * simulation frame.
 */
struct NativeDisplayConfig
{
	int renderScale;
	int fullscreen;
	int textureFilter;
};

/*
 * Host-local texture sampling modes.  Presentation-only: these values never
 * travel across a transport or into canonical state.
 */
enum NativeTextureFilter
{
	NATIVE_TEXTURE_FILTER_NEAREST = 0,
	NATIVE_TEXTURE_FILTER_BILINEAR = 1
};

#define NATIVE_DISPLAY_CONFIG_DEFAULT_RENDER_SCALE 1
#define NATIVE_DISPLAY_CONFIG_DEFAULT_FULLSCREEN    0
#define NATIVE_DISPLAY_CONFIG_DEFAULT_TEXTURE_FILTER NATIVE_TEXTURE_FILTER_NEAREST

void NativeDisplayConfig_SetDefaults(struct NativeDisplayConfig *config);
int NativeDisplayConfig_IsRenderScaleSupported(int renderScale);

/* Returns non-zero when `filter` is one of the supported `enum NativeTextureFilter` values. */
int NativeDisplayConfig_IsTextureFilterSupported(int filter);

/*
 * Parses a lowercase texture filter name (`nearest` or `bilinear`).  Returns
 * zero for NULL/empty text, a NULL `outFilter`, or an unrecognised name, and
 * leaves `*outFilter` untouched in that case.
 */
int NativeDisplayConfig_ParseTextureFilter(const char *text, int *outFilter);

/*
 * Returns the lowercase name of a supported texture filter, or "unknown" for
 * an out-of-range value.  The returned pointer is a static string literal.
 */
const char *NativeDisplayConfig_TextureFilterName(int filter);

/*
 * Applies local command-line overrides transactionally.  The parser owns
 * `--render-scale N`, `--render-scale=N`, `--texture-filter MODE`,
 * `--texture-filter=MODE`, `--fullscreen`, and `--windowed`; unrelated options
 * are left for their respective subsystems. Returns zero on
 * malformed/unsupported local display arguments and leaves `config`
 * unchanged in that case.
 */
int NativeDisplayConfig_ApplyArgs(int argc, char *argv[], struct NativeDisplayConfig *config);

#endif
