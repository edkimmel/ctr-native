#include <platform/native_display_config.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int Apply(int argc, char *argv[], struct NativeDisplayConfig *config)
{
	return NativeDisplayConfig_ApplyArgs(argc, argv, config);
}

int main(void)
{
	struct NativeDisplayConfig config;
	int parsed = -1;
	char *noArgs[] = {"ctr_native"};
	char *scale2[] = {"ctr_native", "--render-scale", "2"};
	char *scale4Equals[] = {"ctr_native", "--render-scale=4"};
	char *unrelated[] = {"ctr_native", "--perf", "--perf-dir", "reports", "--replay", "input.ctrreplay"};
	char *repeated[] = {"ctr_native", "--render-scale", "2", "--render-scale=6"};
	char *fullscreen[] = {"ctr_native", "--fullscreen"};
	char *windowed[] = {"ctr_native", "--windowed"};
	char *lastModeWins[] = {"ctr_native", "--fullscreen", "--windowed", "--fullscreen"};
	char *scaleAndFullscreen[] = {"ctr_native", "--render-scale", "4", "--fullscreen"};
	char *missing[] = {"ctr_native", "--render-scale"};
	char *optionInsteadOfValue[] = {"ctr_native", "--render-scale", "--perf"};
	char *zero[] = {"ctr_native", "--render-scale", "0"};
	char *five[] = {"ctr_native", "--render-scale=5"};
	char *negative[] = {"ctr_native", "--render-scale", "-2"};
	char *decimal[] = {"ctr_native", "--render-scale", "2.0"};
	char *overflow[] = {"ctr_native", "--render-scale", "999999999999999999999"};
	char *bilinear[] = {"ctr_native", "--texture-filter", "bilinear"};
	char *bilinearEquals[] = {"ctr_native", "--texture-filter=bilinear"};
	char *nearest[] = {"ctr_native", "--texture-filter", "nearest"};
	char *filterRepeated[] = {"ctr_native", "--texture-filter", "bilinear", "--texture-filter=nearest", "--texture-filter", "bilinear"};
	char *filterScaleFullscreen[] = {"ctr_native", "--texture-filter", "bilinear", "--render-scale", "8", "--fullscreen"};
	char *filterMissing[] = {"ctr_native", "--texture-filter"};
	char *filterOptionInsteadOfValue[] = {"ctr_native", "--texture-filter", "--perf"};
	char *filterUnknown[] = {"ctr_native", "--texture-filter", "xbr"};
	char *filterUppercase[] = {"ctr_native", "--texture-filter", "Bilinear"};
	char *filterEmptyEquals[] = {"ctr_native", "--texture-filter="};

	CHECK(NativeDisplayConfig_IsRenderScaleSupported(1));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(2));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(3));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(4));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(6));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(8));
	CHECK(!NativeDisplayConfig_IsRenderScaleSupported(0));
	CHECK(!NativeDisplayConfig_IsRenderScaleSupported(5));
	CHECK(!NativeDisplayConfig_IsRenderScaleSupported(9));

	CHECK(NativeDisplayConfig_IsTextureFilterSupported(NATIVE_TEXTURE_FILTER_NEAREST));
	CHECK(NativeDisplayConfig_IsTextureFilterSupported(NATIVE_TEXTURE_FILTER_BILINEAR));
	CHECK(!NativeDisplayConfig_IsTextureFilterSupported(-1));
	CHECK(!NativeDisplayConfig_IsTextureFilterSupported(2));

	CHECK(NativeDisplayConfig_ParseTextureFilter("nearest", &parsed));
	CHECK(parsed == NATIVE_TEXTURE_FILTER_NEAREST);
	CHECK(NativeDisplayConfig_ParseTextureFilter("bilinear", &parsed));
	CHECK(parsed == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(!NativeDisplayConfig_ParseTextureFilter("xbr", &parsed));
	CHECK(!NativeDisplayConfig_ParseTextureFilter("Bilinear", &parsed));
	CHECK(!NativeDisplayConfig_ParseTextureFilter("", &parsed));
	CHECK(parsed == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(!NativeDisplayConfig_ParseTextureFilter(NULL, &parsed));
	CHECK(!NativeDisplayConfig_ParseTextureFilter("nearest", NULL));

	CHECK(strcmp(NativeDisplayConfig_TextureFilterName(NATIVE_TEXTURE_FILTER_NEAREST), "nearest") == 0);
	CHECK(strcmp(NativeDisplayConfig_TextureFilterName(NATIVE_TEXTURE_FILTER_BILINEAR), "bilinear") == 0);
	CHECK(strcmp(NativeDisplayConfig_TextureFilterName(7), "unknown") == 0);

	NativeDisplayConfig_SetDefaults(&config);
	CHECK(config.renderScale == 1);
	CHECK(config.fullscreen == 0);
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_NEAREST);
	CHECK(Apply(1, noArgs, &config));
	CHECK(config.renderScale == 1);
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_NEAREST);
	CHECK(Apply(3, scale2, &config));
	CHECK(config.renderScale == 2);
	CHECK(Apply(2, scale4Equals, &config));
	CHECK(config.renderScale == 4);
	CHECK(Apply(6, unrelated, &config));
	CHECK(config.renderScale == 4);
	CHECK(Apply(4, repeated, &config));
	CHECK(config.renderScale == 6);
	CHECK(Apply(2, fullscreen, &config));
	CHECK(config.fullscreen == 1);
	CHECK(Apply(2, windowed, &config));
	CHECK(config.fullscreen == 0);
	CHECK(Apply(4, lastModeWins, &config));
	CHECK(config.fullscreen == 1);
	CHECK(Apply(4, scaleAndFullscreen, &config));
	CHECK(config.renderScale == 4 && config.fullscreen == 1);

	CHECK(Apply(3, bilinear, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(Apply(3, nearest, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_NEAREST);
	CHECK(Apply(2, bilinearEquals, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(Apply(6, filterRepeated, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(Apply(6, unrelated, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(config.renderScale == 4 && config.fullscreen == 1);

	NativeDisplayConfig_SetDefaults(&config);
	CHECK(Apply(6, filterScaleFullscreen, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(config.renderScale == 8);
	CHECK(config.fullscreen == 1);

	config.renderScale = 3;
	CHECK(!Apply(2, missing, &config));
	CHECK(config.renderScale == 3);
	CHECK(!Apply(3, optionInsteadOfValue, &config));
	CHECK(config.renderScale == 3);
	CHECK(!Apply(3, zero, &config));
	CHECK(config.renderScale == 3);
	CHECK(!Apply(2, five, &config));
	CHECK(config.renderScale == 3);
	CHECK(!Apply(3, negative, &config));
	CHECK(config.renderScale == 3);
	CHECK(!Apply(3, decimal, &config));
	CHECK(config.renderScale == 3);
	CHECK(!Apply(3, overflow, &config));
	CHECK(config.renderScale == 3);

	config.textureFilter = NATIVE_TEXTURE_FILTER_BILINEAR;
	CHECK(!Apply(2, filterMissing, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(!Apply(3, filterOptionInsteadOfValue, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(!Apply(3, filterUnknown, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(!Apply(3, filterUppercase, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(!Apply(2, filterEmptyEquals, &config));
	CHECK(config.textureFilter == NATIVE_TEXTURE_FILTER_BILINEAR);
	CHECK(config.renderScale == 3 && config.fullscreen == 1);

	CHECK(!NativeDisplayConfig_ApplyArgs(1, noArgs, NULL));
	config.renderScale = 1;
	config.fullscreen = 2;
	CHECK(!Apply(1, noArgs, &config));
	CHECK(config.fullscreen == 2);

	NativeDisplayConfig_SetDefaults(&config);
	config.textureFilter = 42;
	CHECK(!Apply(1, noArgs, &config));
	CHECK(config.textureFilter == 42);
	CHECK(!Apply(3, bilinear, &config));
	CHECK(config.textureFilter == 42);

	puts("native_display_config_test: PASS");
	return 0;
}
