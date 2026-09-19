#include <platform/native_presentation_override_config.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int Apply(int argc, char *argv[], struct NativePresentationOverrideConfig *config)
{
	return NativePresentationOverrideConfig_ApplyArgs(argc, argv, config);
}

int main(void)
{
	struct NativePresentationOverrideConfig config;
	char *noArgs[] = {"ctr_native"};
	char *packOnly[] = {"ctr_native", "--presentation-pack", "packs\\hd-ui"};
	char *enabled[] = {"ctr_native", "--presentation-overrides"};
	char *enabledWithPack[] = {"ctr_native", "--presentation-pack", "packs\\hd-ui", "--presentation-overrides"};
	char *enabledWithEqualsPack[] = {"ctr_native", "--presentation-overrides=on", "--presentation-pack=packs/hd-ui"};
	char *disabledWithPack[] = {"ctr_native", "--presentation-pack=packs/hd-ui", "--presentation-overrides=off"};
	char *unrelated[] = {"ctr_native", "--perf", "--replay-v2", "input.v2.ctrreplay"};
	char *missingPack[] = {"ctr_native", "--presentation-pack"};
	char *optionAsPack[] = {"ctr_native", "--presentation-pack", "--perf"};
	char *emptyEqualsPack[] = {"ctr_native", "--presentation-pack="};
	char *malformedEnabled[] = {"ctr_native", "--presentation-overrides=maybe"};
	char *emptyEnabled[] = {"ctr_native", "--presentation-overrides="};
	char *duplicatePack[] = {"ctr_native", "--presentation-pack", "a", "--presentation-pack=b"};
	char *duplicateEnabled[] = {"ctr_native", "--presentation-overrides", "--presentation-overrides=off", "--presentation-pack", "packs/hd-ui"};
	char *packThenEquals[] = {"ctr_native", "--presentation-pack=packs/hd-ui", "--presentation-overrides=on"};

	NativePresentationOverrideConfig_SetDefaults(&config);
	CHECK(config.enabled == 0);
	CHECK(config.packDirectory == NULL);
	CHECK(Apply(1, noArgs, &config));
	CHECK(config.enabled == 0 && config.packDirectory == NULL);
	CHECK(Apply(3, packOnly, &config));
	CHECK(config.enabled == 0);
	CHECK(strcmp(config.packDirectory, "packs\\hd-ui") == 0);
	CHECK(Apply(4, enabledWithPack, &config));
	CHECK(config.enabled == 1);
	CHECK(strcmp(config.packDirectory, "packs\\hd-ui") == 0);

	NativePresentationOverrideConfig_SetDefaults(&config);
	CHECK(Apply(3, enabledWithEqualsPack, &config));
	CHECK(config.enabled == 1);
	CHECK(strcmp(config.packDirectory, "packs/hd-ui") == 0);
	CHECK(Apply(3, disabledWithPack, &config));
	CHECK(config.enabled == 0);
	CHECK(strcmp(config.packDirectory, "packs/hd-ui") == 0);
	CHECK(Apply(4, unrelated, &config));
	CHECK(config.enabled == 0);

	NativePresentationOverrideConfig_SetDefaults(&config);
	CHECK(!Apply(2, enabled, &config));
	CHECK(config.enabled == 0 && config.packDirectory == NULL);
	CHECK(!Apply(2, missingPack, &config));
	CHECK(config.enabled == 0 && config.packDirectory == NULL);
	CHECK(!Apply(3, optionAsPack, &config));
	CHECK(!Apply(2, emptyEqualsPack, &config));
	CHECK(!Apply(2, malformedEnabled, &config));
	CHECK(!Apply(2, emptyEnabled, &config));
	CHECK(!Apply(5, duplicatePack, &config));
	CHECK(!Apply(5, duplicateEnabled, &config));
	CHECK(config.enabled == 0 && config.packDirectory == NULL);
	CHECK(Apply(3, packThenEquals, &config));
	CHECK(config.enabled == 1);
	CHECK(strcmp(config.packDirectory, "packs/hd-ui") == 0);

	config.enabled = 2;
	CHECK(!Apply(1, noArgs, &config));
	CHECK(config.enabled == 2);
	config.enabled = 1;
	config.packDirectory = NULL;
	CHECK(!Apply(1, noArgs, &config));
	CHECK(!NativePresentationOverrideConfig_ApplyArgs(1, noArgs, NULL));
	CHECK(!NativePresentationOverrideConfig_ApplyArgs(-1, noArgs, &config));

	puts("native_presentation_override_config_test: PASS");
	return 0;
}
