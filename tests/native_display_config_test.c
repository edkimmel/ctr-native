#include <platform/native_display_config.h>

#include <stdio.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int Apply(int argc, char *argv[], struct NativeDisplayConfig *config)
{
	return NativeDisplayConfig_ApplyArgs(argc, argv, config);
}

int main(void)
{
	struct NativeDisplayConfig config;
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

	CHECK(NativeDisplayConfig_IsRenderScaleSupported(1));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(2));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(3));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(4));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(6));
	CHECK(NativeDisplayConfig_IsRenderScaleSupported(8));
	CHECK(!NativeDisplayConfig_IsRenderScaleSupported(0));
	CHECK(!NativeDisplayConfig_IsRenderScaleSupported(5));
	CHECK(!NativeDisplayConfig_IsRenderScaleSupported(9));

	NativeDisplayConfig_SetDefaults(&config);
	CHECK(config.renderScale == 1);
	CHECK(config.fullscreen == 0);
	CHECK(Apply(1, noArgs, &config));
	CHECK(config.renderScale == 1);
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
	CHECK(!NativeDisplayConfig_ApplyArgs(1, noArgs, NULL));
	config.renderScale = 1;
	config.fullscreen = 2;
	CHECK(!Apply(1, noArgs, &config));
	CHECK(config.fullscreen == 2);

	puts("native_display_config_test: PASS");
	return 0;
}
