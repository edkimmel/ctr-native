#include "platform/native_arcade_roster_proof.h"

#include "MAIN/MainArcadeRaceSetupPlan.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_canonical_state.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define ARGC(array) ((int)(sizeof(array) / sizeof((array)[0])))
#define TWO_CAB NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB
#define ONE_CAB NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB
#define SENTINEL_BYTE 0x5au

static void FillCounting(uint8_t *bytes, size_t size, uint8_t start)
{
	for (size_t i = 0; i < size; i++)
	{
		bytes[i] = (uint8_t)(start + i);
	}
}

static void TestIdentity(struct NativeIdentityV1 *identity)
{
	FillCounting(identity->build, sizeof(identity->build), 0x31u);
	FillCounting(identity->content, sizeof(identity->content), 0x71u);
}

static int IsAllByte(const void *memory, size_t size, uint8_t value)
{
	const uint8_t *bytes = (const uint8_t *)memory;

	for (size_t i = 0; i < size; i++)
	{
		if (bytes[i] != value)
		{
			return 0;
		}
	}
	return 1;
}

/* ApplyArgs must fail and leave the options untouched. */
static int ExpectReject(int argc, char *argv[])
{
	struct NativeArcadeRosterProofOptions options;

	memset(&options, SENTINEL_BYTE, sizeof(options));
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(argc, argv, &options) == 0);
	CHECK(IsAllByte(&options, sizeof(options), SENTINEL_BYTE));
	return 0;
}

static int TestDefaults(void)
{
	struct NativeArcadeRosterProofOptions options;
	char *none[] = {"ctr_native", "--windowed", "--render-scale", "2"};

	memset(&options, SENTINEL_BYTE, sizeof(options));
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(options.enabled == 0u);
	CHECK(options.seed == UINT64_C(1));
	CHECK(options.dwellTicks == 0u);
	CHECK(options.tickCount == 900u && options.tickCount == NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_TICKS);
	CHECK(options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);
	CHECK(NATIVE_ARCADE_ROSTER_PROOF_DEFAULT_PROFILE == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);
	CHECK(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
	CHECK(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
	CHECK(IsAllByte(options.logPath, sizeof(options.logPath), 0u));
	CHECK(IsAllByte(options.reserved, sizeof(options.reserved), 0u));
	NativeArcadeRosterProofOptions_SetDefaults(NULL);

	/* Unrelated arguments leave the defaults. */
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(none), none, &options) == 1);
	CHECK((options.enabled == 0u) && (options.seed == 1u) && (options.dwellTicks == 0u) && (options.logPath[0] == '\0'));
	CHECK(options.tickCount == 900u && options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(1, none, &options) == 1);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(none), none, NULL) == 0);
	return 0;
}

static int TestValidForms(void)
{
	struct NativeArcadeRosterProofOptions options;
	char *pathOnly[] = {"ctr_native", "--arcade-roster-proof", "C:/proof/run1.txt"};
	char *decimal[] = {"ctr_native", "--arcade-roster-proof-seed", "18446744073709551615", "--arcade-roster-proof", "r.txt",
		"--arcade-roster-proof-dwell", "7200", "--windowed"};
	char *hexLower[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0xdeadbeef00c0ffee"};
	char *hexUpper[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0XFFFFFFFFFFFFFFFF",
		"--arcade-roster-proof-dwell", "45"};
	char *zeros[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0",
		"--arcade-roster-proof-dwell", "0"};
	char *leadingZeros[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0x0000000000000002",
		"--arcade-roster-proof-dwell", "007"};
	char longPath[NATIVE_ARCADE_ROSTER_PROOF_PATH_BYTES];
	char *maxPath[] = {"ctr_native", "--arcade-roster-proof", longPath};

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(pathOnly), pathOnly, &options) == 1);
	CHECK((options.enabled == 1u) && (strcmp(options.logPath, "C:/proof/run1.txt") == 0));
	CHECK((options.seed == 1u) && (options.dwellTicks == 0u));

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(decimal), decimal, &options) == 1);
	CHECK((options.enabled == 1u) && (options.seed == UINT64_MAX) && (options.dwellTicks == 7200u));
	CHECK(strcmp(options.logPath, "r.txt") == 0);

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(hexLower), hexLower, &options) == 1);
	CHECK(options.seed == UINT64_C(0xdeadbeef00c0ffee));

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(hexUpper), hexUpper, &options) == 1);
	CHECK((options.seed == UINT64_MAX) && (options.dwellTicks == 45u));

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(zeros), zeros, &options) == 1);
	CHECK((options.seed == 0u) && (options.dwellTicks == 0u));

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(leadingZeros), leadingZeros, &options) == 1);
	CHECK((options.seed == 2u) && (options.dwellTicks == 7u));

	/* Four digits reach past the attract demo race start. */
	{
		char *fourDigits[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", "2400"};
		char *fourLeadingZero[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", "0601"};

		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(fourDigits), fourDigits, &options) == 1);
		CHECK(options.dwellTicks == 2400u);
		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(fourLeadingZero), fourLeadingZero, &options) == 1);
		CHECK(options.dwellTicks == 601u);
	}

	/* The tick count: 1..3600, 1..4 decimal digits, with the other options. */
	{
		char *ticksMin[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "1"};
		char *ticksMax[] = {"ctr_native", "--arcade-roster-proof-ticks", "3600", "--arcade-roster-proof", "r.txt",
			"--arcade-roster-proof-seed", "0x5EED", "--arcade-roster-proof-dwell", "5400"};
		char *ticksLeadingZero[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "0900"};

		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(ticksMin), ticksMin, &options) == 1);
		CHECK(options.tickCount == 1u && options.dwellTicks == 0u && options.seed == 1u);
		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(ticksMax), ticksMax, &options) == 1);
		CHECK(options.tickCount == 3600u && options.dwellTicks == 5400u && options.seed == UINT64_C(0x5EED));
		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(ticksLeadingZero), ticksLeadingZero, &options) == 1);
		CHECK(options.tickCount == 900u);
	}

	/* The longest path that fits with its NUL. */
	memset(longPath, 'p', sizeof(longPath) - 1u);
	longPath[sizeof(longPath) - 1u] = '\0';
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(maxPath), maxPath, &options) == 1);
	CHECK(strlen(options.logPath) == sizeof(longPath) - 1u);
	return 0;
}

static int TestInvalidValues(void)
{
	char longPath[NATIVE_ARCADE_ROSTER_PROOF_PATH_BYTES + 1u];
	char *pathMissing[] = {"ctr_native", "--arcade-roster-proof"};
	char *pathOption[] = {"ctr_native", "--arcade-roster-proof", "--windowed"};
	char *pathEmpty[] = {"ctr_native", "--arcade-roster-proof", ""};
	char *pathNull[] = {"ctr_native", "--arcade-roster-proof", NULL};
	char *pathTooLong[] = {"ctr_native", "--arcade-roster-proof", longPath};
	char *seedMissing[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed"};
	char *seedEmpty[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", ""};
	char *seedNegative[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "-1"};
	char *seedPlus[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "+1"};
	char *seedAlpha[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "12a"};
	char *seedSpace[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", " 1"};
	char *seedOverflow[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "18446744073709551616"};
	char *seedTooManyDigits[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "000000000000000000001"};
	char *seedHexEmpty[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0x"};
	char *seedHexLong[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0x10000000000000000"};
	char *seedHexBad[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed", "0xg1"};
	char *dwellMissing[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell"};
	char *dwellTooLarge[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", "7201"};
	char *dwellTooManyDigits[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", "00001"};
	char *dwellNegative[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", "-5"};
	char *dwellHex[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", "0x10"};
	char *dwellEmpty[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-dwell", ""};
	char *ticksMissing[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks"};
	char *ticksZero[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "0"};
	char *ticksZeros[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "0000"};
	char *ticksTooLarge[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "3601"};
	char *ticksTooManyDigits[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "00900"};
	char *ticksNegative[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "-900"};
	char *ticksHex[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "0x10"};
	char *ticksAlpha[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "9a"};
	char *ticksEmpty[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", ""};

	memset(longPath, 'q', sizeof(longPath) - 1u);
	longPath[sizeof(longPath) - 1u] = '\0';
	CHECK(ExpectReject(ARGC(pathMissing), pathMissing) == 0);
	CHECK(ExpectReject(ARGC(pathOption), pathOption) == 0);
	CHECK(ExpectReject(ARGC(pathEmpty), pathEmpty) == 0);
	CHECK(ExpectReject(ARGC(pathNull), pathNull) == 0);
	CHECK(ExpectReject(ARGC(pathTooLong), pathTooLong) == 0);
	CHECK(ExpectReject(ARGC(seedMissing), seedMissing) == 0);
	CHECK(ExpectReject(ARGC(seedEmpty), seedEmpty) == 0);
	CHECK(ExpectReject(ARGC(seedNegative), seedNegative) == 0);
	CHECK(ExpectReject(ARGC(seedPlus), seedPlus) == 0);
	CHECK(ExpectReject(ARGC(seedAlpha), seedAlpha) == 0);
	CHECK(ExpectReject(ARGC(seedSpace), seedSpace) == 0);
	CHECK(ExpectReject(ARGC(seedOverflow), seedOverflow) == 0);
	CHECK(ExpectReject(ARGC(seedTooManyDigits), seedTooManyDigits) == 0);
	CHECK(ExpectReject(ARGC(seedHexEmpty), seedHexEmpty) == 0);
	CHECK(ExpectReject(ARGC(seedHexLong), seedHexLong) == 0);
	CHECK(ExpectReject(ARGC(seedHexBad), seedHexBad) == 0);
	CHECK(ExpectReject(ARGC(dwellMissing), dwellMissing) == 0);
	CHECK(ExpectReject(ARGC(dwellTooLarge), dwellTooLarge) == 0);
	CHECK(ExpectReject(ARGC(dwellTooManyDigits), dwellTooManyDigits) == 0);
	CHECK(ExpectReject(ARGC(dwellNegative), dwellNegative) == 0);
	CHECK(ExpectReject(ARGC(dwellHex), dwellHex) == 0);
	CHECK(ExpectReject(ARGC(dwellEmpty), dwellEmpty) == 0);
	CHECK(ExpectReject(ARGC(ticksMissing), ticksMissing) == 0);
	CHECK(ExpectReject(ARGC(ticksZero), ticksZero) == 0);
	CHECK(ExpectReject(ARGC(ticksZeros), ticksZeros) == 0);
	CHECK(ExpectReject(ARGC(ticksTooLarge), ticksTooLarge) == 0);
	CHECK(ExpectReject(ARGC(ticksTooManyDigits), ticksTooManyDigits) == 0);
	CHECK(ExpectReject(ARGC(ticksNegative), ticksNegative) == 0);
	CHECK(ExpectReject(ARGC(ticksHex), ticksHex) == 0);
	CHECK(ExpectReject(ARGC(ticksAlpha), ticksAlpha) == 0);
	CHECK(ExpectReject(ARGC(ticksEmpty), ticksEmpty) == 0);

	/* ParseSeed directly: NULL arguments and output left untouched. */
	{
		uint64_t value = UINT64_C(0x1234);

		CHECK(NativeArcadeRosterProof_ParseSeed(NULL, &value) == 0);
		CHECK(NativeArcadeRosterProof_ParseSeed("5", NULL) == 0);
		CHECK((NativeArcadeRosterProof_ParseSeed("5x", &value) == 0) && (value == UINT64_C(0x1234)));
		CHECK((NativeArcadeRosterProof_ParseSeed("18446744073709551615", &value) == 1) && (value == UINT64_MAX));
		CHECK((NativeArcadeRosterProof_ParseSeed("0xA", &value) == 1) && (value == 10u));
	}
	return 0;
}

static int TestUnknownCombinations(void)
{
	char *seedAlone[] = {"ctr_native", "--arcade-roster-proof-seed", "7"};
	char *dwellAlone[] = {"ctr_native", "--arcade-roster-proof-dwell", "7"};
	char *ticksAlone[] = {"ctr_native", "--arcade-roster-proof-ticks", "900"};
	char *repeatedTicks[] = {"ctr_native", "--arcade-roster-proof", "a.txt", "--arcade-roster-proof-ticks", "900",
		"--arcade-roster-proof-ticks", "900"};
	char *repeatedProof[] = {"ctr_native", "--arcade-roster-proof", "a.txt", "--arcade-roster-proof", "b.txt"};
	char *repeatedSeed[] = {"ctr_native", "--arcade-roster-proof", "a.txt", "--arcade-roster-proof-seed", "1",
		"--arcade-roster-proof-seed", "2"};
	char *repeatedDwell[] = {"ctr_native", "--arcade-roster-proof", "a.txt", "--arcade-roster-proof-dwell", "1",
		"--arcade-roster-proof-dwell", "1"};
	char *equalsForm[] = {"ctr_native", "--arcade-roster-proof=a.txt"};
	char *prefixOnly[] = {"ctr_native", "--arcade-roster-proof-", "1"};
	struct NativeArcadeRosterProofOptions options;

	CHECK(ExpectReject(ARGC(seedAlone), seedAlone) == 0);
	CHECK(ExpectReject(ARGC(dwellAlone), dwellAlone) == 0);
	CHECK(ExpectReject(ARGC(ticksAlone), ticksAlone) == 0);
	CHECK(ExpectReject(ARGC(repeatedTicks), repeatedTicks) == 0);
	CHECK(ExpectReject(ARGC(repeatedProof), repeatedProof) == 0);
	CHECK(ExpectReject(ARGC(repeatedSeed), repeatedSeed) == 0);
	CHECK(ExpectReject(ARGC(repeatedDwell), repeatedDwell) == 0);

	/* Names that are not one of the three options are left to other parsers. */
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(equalsForm), equalsForm, &options) == 1);
	CHECK(options.enabled == 0u);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(prefixOnly), prefixOnly, &options) == 1);
	CHECK(options.enabled == 0u);
	return 0;
}

/* --arcade-roster-proof-hold (LR-S2 (a)): a flag, only with the proof, once,
 * and only with more than HOLD_TICK race ticks. */
static int TestHoldOption(void)
{
	struct NativeArcadeRosterProofOptions options;
	char *noHold[] = {"ctr_native", "--arcade-roster-proof", "r.txt"};
	char *hold[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-hold"};
	char *holdFirst[] = {"ctr_native", "--arcade-roster-proof-hold", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-seed",
		"0x5EED"};
	char *holdMinTicks[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-hold", "--arcade-roster-proof-ticks",
		"301"};
	char *holdAlone[] = {"ctr_native", "--arcade-roster-proof-hold"};
	char *holdRepeated[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-hold", "--arcade-roster-proof-hold"};
	char *holdFewTicks[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-hold", "--arcade-roster-proof-ticks",
		"300"};
	char *holdOneTick[] = {"ctr_native", "--arcade-roster-proof-ticks", "1", "--arcade-roster-proof", "r.txt",
		"--arcade-roster-proof-hold"};
	/* The flag takes no value: a following word is another parser's. */
	char *holdValue[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-hold", "45"};
	char *holdEquals[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-hold=1"};

	CHECK(NATIVE_ARCADE_ROSTER_PROOF_HOLD_TICK == 300u && NATIVE_ARCADE_ROSTER_PROOF_HOLD_PERIODS == 45u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(options.hold == 0u);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(noHold), noHold, &options) == 1);
	CHECK(options.enabled == 1u && options.hold == 0u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(hold), hold, &options) == 1);
	CHECK(options.enabled == 1u && options.hold == 1u && options.tickCount == 900u);
	CHECK(IsAllByte(options.reserved, sizeof(options.reserved), 0u));
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(holdFirst), holdFirst, &options) == 1);
	CHECK(options.hold == 1u && options.seed == UINT64_C(0x5EED));
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(holdMinTicks), holdMinTicks, &options) == 1);
	CHECK(options.hold == 1u && options.tickCount == 301u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(holdValue), holdValue, &options) == 1);
	CHECK(options.hold == 1u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(holdEquals), holdEquals, &options) == 1);
	CHECK(options.enabled == 1u && options.hold == 0u);
	CHECK(ExpectReject(ARGC(holdAlone), holdAlone) == 0);
	CHECK(ExpectReject(ARGC(holdRepeated), holdRepeated) == 0);
	CHECK(ExpectReject(ARGC(holdFewTicks), holdFewTicks) == 0);
	CHECK(ExpectReject(ARGC(holdOneTick), holdOneTick) == 0);

	/* Configure refuses a hold without its tick lines; the accessor reports it. */
	{
		struct NativeIdentityV1 identity;

		TestIdentity(&identity);
		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(hold), hold, &options) == 1);
		CHECK(NativeArcadeRosterProof_Hold() == 0u);
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
		CHECK(NativeArcadeRosterProof_Hold() == 1u);
		NativeArcadeRosterProof_Shutdown();
		CHECK(NativeArcadeRosterProof_Hold() == 0u);
		options.hold = 0u;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
		CHECK(NativeArcadeRosterProof_Hold() == 0u);
		options.hold = 1u;
		options.tickCount = 300u;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
		CHECK(NativeArcadeRosterProof_Active() == 0 && NativeArcadeRosterProof_Hold() == 0u);
		options.tickCount = 900u;
		options.hold = 2u;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
		NativeArcadeRosterProof_Shutdown();
	}
	return 0;
}

/* --arcade-roster-proof-autopilot (LR-S2 (b)): a flag, only with the proof,
 * once, two-cab only; it alone allows a tick count up to AUTOPILOT_MAX_TICKS. */
static int TestAutopilotOption(void)
{
	struct NativeArcadeRosterProofOptions options;
	char *noAutopilot[] = {"ctr_native", "--arcade-roster-proof", "r.txt"};
	char *autopilot[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot"};
	char *autopilotRace[] = {"ctr_native", "--arcade-roster-proof-autopilot", "--arcade-roster-proof-ticks", "6000", "--arcade-roster-proof",
		"r.txt", "--arcade-roster-proof-seed", "0x5EED", "--arcade-roster-proof-profile", "two-cab"};
	char *autopilotHold[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot", "--arcade-roster-proof-hold"};
	char *autopilotAlone[] = {"ctr_native", "--arcade-roster-proof-autopilot"};
	char *autopilotRepeated[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot",
		"--arcade-roster-proof-autopilot"};
	char *autopilotOneCab[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot",
		"--arcade-roster-proof-profile", "one-cab"};
	char *autopilotTooMany[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot",
		"--arcade-roster-proof-ticks", "6001"};
	char *ticksWithoutAutopilot[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "3601"};
	char *ticksMaxWithoutAutopilot[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-ticks", "6000"};
	/* The flag takes no value: a following word is another parser's. */
	char *autopilotValue[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot", "1"};
	char *autopilotEquals[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-autopilot=1"};

	CHECK(NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS == 6000u && NATIVE_ARCADE_ROSTER_PROOF_MAX_TICKS == 3600u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(options.autopilot == 0u);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(noAutopilot), noAutopilot, &options) == 1);
	CHECK(options.enabled == 1u && options.autopilot == 0u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilot), autopilot, &options) == 1);
	CHECK(options.enabled == 1u && options.autopilot == 1u && options.hold == 0u && options.tickCount == 900u);
	CHECK(options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);
	CHECK(IsAllByte(options.reserved, sizeof(options.reserved), 0u));
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilotRace), autopilotRace, &options) == 1);
	CHECK(options.autopilot == 1u && options.tickCount == 6000u && options.seed == UINT64_C(0x5EED));
	/* The hold combines with the autopilot under its own tick rule. */
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilotHold), autopilotHold, &options) == 1);
	CHECK(options.autopilot == 1u && options.hold == 1u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	options.tickCount = 300u;
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilotHold), autopilotHold, &options) == 0);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilotValue), autopilotValue, &options) == 1);
	CHECK(options.autopilot == 1u);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilotEquals), autopilotEquals, &options) == 1);
	CHECK(options.enabled == 1u && options.autopilot == 0u);
	CHECK(ExpectReject(ARGC(autopilotAlone), autopilotAlone) == 0);
	CHECK(ExpectReject(ARGC(autopilotRepeated), autopilotRepeated) == 0);
	CHECK(ExpectReject(ARGC(autopilotOneCab), autopilotOneCab) == 0);
	CHECK(ExpectReject(ARGC(autopilotTooMany), autopilotTooMany) == 0);
	CHECK(ExpectReject(ARGC(ticksWithoutAutopilot), ticksWithoutAutopilot) == 0);
	CHECK(ExpectReject(ARGC(ticksMaxWithoutAutopilot), ticksMaxWithoutAutopilot) == 0);

	/* Configure: the same rules; the accessor reports the option; the tick
	 * lines of a 6000-tick autopilot run are all kept. */
	{
		struct NativeIdentityV1 identity;
		struct NativeArcadeRosterProofTickLine line;

		TestIdentity(&identity);
		NativeArcadeRosterProofOptions_SetDefaults(&options);
		CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(autopilotRace), autopilotRace, &options) == 1);
		CHECK(NativeArcadeRosterProof_Autopilot() == 0u);
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
		CHECK(NativeArcadeRosterProof_Autopilot() == 1u && NativeArcadeRosterProof_Ticks() == 6000u);
		memset(&line, 0, sizeof(line));
		for (uint32_t tick = 0; tick < 6000u; tick++)
		{
			line.tick = tick;
			CHECK(NativeArcadeRosterProof_RecordTick(&line) == 1);
		}
		line.tick = 6000u;
		CHECK(NativeArcadeRosterProof_RecordTick(&line) == 0);
		CHECK(NativeArcadeRosterProof_TickCount() == 6000u);
		NativeArcadeRosterProof_Shutdown();
		CHECK(NativeArcadeRosterProof_Autopilot() == 0u);
		options.autopilot = 0u;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0); /* 6000 ticks without the autopilot */
		options.tickCount = NATIVE_ARCADE_ROSTER_PROOF_MAX_TICKS;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
		CHECK(NativeArcadeRosterProof_Autopilot() == 0u);
		options.autopilot = 1u;
		options.tickCount = NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS + 1u;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
		CHECK(NativeArcadeRosterProof_Active() == 0 && NativeArcadeRosterProof_Autopilot() == 0u);
		options.tickCount = 900u;
		options.profile = NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
		options.profile = NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB;
		options.autopilot = 2u;
		CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
		NativeArcadeRosterProof_Shutdown();
	}
	return 0;
}

/* --arcade-roster-proof-profile (RS-23): two-cab by default, one-cab, or an error. */
static int TestProfileOption(void)
{
	struct NativeArcadeRosterProofOptions options;
	char *noProfile[] = {"ctr_native", "--arcade-roster-proof", "r.txt"};
	char *oneCab[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "one-cab"};
	char *twoCab[] = {"ctr_native", "--arcade-roster-proof-profile", "two-cab", "--arcade-roster-proof", "r.txt"};
	char *allOptions[] = {"ctr_native", "--arcade-roster-proof-profile", "one-cab", "--arcade-roster-proof", "r.txt",
		"--arcade-roster-proof-seed", "0x5EEE", "--arcade-roster-proof-dwell", "37", "--arcade-roster-proof-ticks", "900",
		"--windowed"};
	char *profileMissing[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile"};
	char *profileOption[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "--windowed"};
	char *profileNull[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", NULL};
	char *profileEmpty[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", ""};
	char *profileUnknown[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "three-cab"};
	char *profileUpper[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "ONE-CAB"};
	char *profileMixed[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "One-Cab"};
	char *profileEnumName[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "ONE_CAB"};
	char *profileUnderscore[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "one_cab"};
	char *profileTrailing[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "one-cab "};
	char *profilePrefix[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "one"};
	char *profileNumber[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "2"};
	char *profileRepeated[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "one-cab",
		"--arcade-roster-proof-profile", "one-cab"};
	char *profileRepeatedMixed[] = {"ctr_native", "--arcade-roster-proof-profile", "two-cab", "--arcade-roster-proof", "r.txt",
		"--arcade-roster-proof-profile", "one-cab"};
	char *profileAlone[] = {"ctr_native", "--arcade-roster-proof-profile", "one-cab"};
	char *profileAloneTwo[] = {"ctr_native", "--arcade-roster-proof-profile", "two-cab", "--windowed"};
	/* A valid profile before a bad seed: the whole parse fails, nothing applied. */
	char *profileThenBadSeed[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--arcade-roster-proof-profile", "one-cab",
		"--arcade-roster-proof-seed", "x"};

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(noProfile), noProfile, &options) == 1);
	CHECK(options.enabled == 1u && options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(oneCab), oneCab, &options) == 1);
	CHECK(options.enabled == 1u && options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB);
	CHECK(options.seed == 1u && options.dwellTicks == 0u && options.tickCount == 900u);

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(twoCab), twoCab, &options) == 1);
	CHECK(options.enabled == 1u && options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);

	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(allOptions), allOptions, &options) == 1);
	CHECK(options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB && options.seed == UINT64_C(0x5EEE));
	CHECK(options.dwellTicks == 37u && options.tickCount == 900u && strcmp(options.logPath, "r.txt") == 0);

	CHECK(ExpectReject(ARGC(profileMissing), profileMissing) == 0);
	CHECK(ExpectReject(ARGC(profileOption), profileOption) == 0);
	CHECK(ExpectReject(ARGC(profileNull), profileNull) == 0);
	CHECK(ExpectReject(ARGC(profileEmpty), profileEmpty) == 0);
	CHECK(ExpectReject(ARGC(profileUnknown), profileUnknown) == 0);
	CHECK(ExpectReject(ARGC(profileUpper), profileUpper) == 0);
	CHECK(ExpectReject(ARGC(profileMixed), profileMixed) == 0);
	CHECK(ExpectReject(ARGC(profileEnumName), profileEnumName) == 0);
	CHECK(ExpectReject(ARGC(profileUnderscore), profileUnderscore) == 0);
	CHECK(ExpectReject(ARGC(profileTrailing), profileTrailing) == 0);
	CHECK(ExpectReject(ARGC(profilePrefix), profilePrefix) == 0);
	CHECK(ExpectReject(ARGC(profileNumber), profileNumber) == 0);
	CHECK(ExpectReject(ARGC(profileRepeated), profileRepeated) == 0);
	CHECK(ExpectReject(ARGC(profileRepeatedMixed), profileRepeatedMixed) == 0);
	CHECK(ExpectReject(ARGC(profileAlone), profileAlone) == 0);
	CHECK(ExpectReject(ARGC(profileAloneTwo), profileAloneTwo) == 0);
	CHECK(ExpectReject(ARGC(profileThenBadSeed), profileThenBadSeed) == 0);

	/* Transactional on real options too: a rejected parse keeps the old profile. */
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(profileThenBadSeed), profileThenBadSeed, &options) == 0);
	CHECK(options.enabled == 0u && options.profile == NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB);

	/* The profile names. */
	CHECK(strcmp(NativeArcadeRosterProof_ProfileName(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_TWO_CAB), "TWO_CAB") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ProfileName(NATIVE_ARCADE_ROSTER_PROOF_PROFILE_ONE_CAB), "ONE_CAB") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ProfileName(0u), "UNKNOWN") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ProfileName(3u), "UNKNOWN") == 0);
	return 0;
}

static int TestExitOptionNames(void)
{
	char *separate[] = {"ctr_native", "--arcade-roster-proof", "r.txt", "--exit-after-frame", "100"};
	char *equals[] = {"ctr_native", "--exit-after-frame=100", "--arcade-roster-proof", "r.txt"};
	char *equalsEmpty[] = {"ctr_native", "--exit-after-frame="};
	char *captureOnly[] = {"ctr_native", "--capture-frame", "5=a.bmp", "--arcade-roster-proof", "r.txt"};
	char *lookalikes[] = {"ctr_native", "--exit-after-frames", "--exit-after", "-exit-after-frame", "exit-after-frame"};
	char *withNull[] = {"ctr_native", NULL, "--exit-after-frame", "3"};
	char *programOnly[] = {"--exit-after-frame"};

	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(separate), separate) == 1);
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(equals), equals) == 1);
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(equalsEmpty), equalsEmpty) == 1);
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(withNull), withNull) == 1);
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(captureOnly), captureOnly) == 0);
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(lookalikes), lookalikes) == 0);
	/* argv[0] is the program, never an option. */
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(programOnly), programOnly) == 0);
	CHECK(NativeArcadeRosterProof_NamesExitOption(0, separate) == 0);
	CHECK(NativeArcadeRosterProof_NamesExitOption(ARGC(separate), NULL) == 0);
	return 0;
}

/* The proof failure codes never collide with main.c's startup failure 1 or abort()'s 3. */
static int TestExitCodes(void)
{
	static const uint32_t failures[] = {NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE, NATIVE_ARCADE_ROSTER_PROOF_REPORT_WRITE_FAILED,
		NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED, NATIVE_ARCADE_ROSTER_PROOF_ARM_FAILED, NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED,
		NATIVE_ARCADE_ROSTER_PROOF_MENU_READY_TIMEOUT, NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT,
		NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH, NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING,
		NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT, NATIVE_ARCADE_ROSTER_PROOF_DRIVERS_FAILED,
		NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED, NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH, NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_TIMEOUT};
	struct NativeIdentityV1 identity;
	struct NativeArcadeRosterProofOptions options;

	CHECK(NATIVE_ARCADE_ROSTER_PROOF_PASS == 0);
	for (uint32_t i = 0; i < (uint32_t)(sizeof(failures) / sizeof(failures[0])); i++)
	{
		CHECK(failures[i] == 20u + i);
		CHECK(strcmp(NativeArcadeRosterProof_ResultName(failures[i]), "UNKNOWN") != 0);
	}
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE), "INCOMPLETE") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH), "SEED_MISMATCH") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING), "EVIDENCE_MISSING") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_RACE_TICK_TIMEOUT), "RACE_TICK_TIMEOUT") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_DRIVERS_FAILED), "DRIVERS_FAILED") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED), "DIGEST_FAILED") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH), "PIN_MISMATCH") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(NATIVE_ARCADE_ROSTER_PROOF_TICK_LOG_TIMEOUT), "TICK_LOG_TIMEOUT") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(1u), "UNKNOWN") == 0);

	/* Inactive: every exit path keeps its own code (a default run is unchanged). */
	NativeArcadeRosterProof_Shutdown();
	CHECK(NativeArcadeRosterProof_ExitCode(0) == 0);
	CHECK(NativeArcadeRosterProof_ExitCode(7) == 7);
	NativeArcadeRosterProof_RecordExitCode(0);
	CHECK(NativeArcadeRosterProof_ExitCode(5) == 5);

	/* Active: nonzero until the proof reports, then the recorded code, once. */
	TestIdentity(&identity);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	options.enabled = 1u;
	memcpy(options.logPath, "proof_report.txt", sizeof("proof_report.txt"));
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeRosterProof_ExitCode(0) == (int)NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE);
	CHECK(NativeArcadeRosterProof_ExitCode(1) == (int)NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE);
	NativeArcadeRosterProof_RecordExitCode((int)NATIVE_ARCADE_ROSTER_PROOF_PASS);
	CHECK(NativeArcadeRosterProof_ExitCode(1) == 0);
	NativeArcadeRosterProof_RecordExitCode((int)NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
	CHECK(NativeArcadeRosterProof_ExitCode(1) == 0);
	/* A new Configure forgets the recorded code. */
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeRosterProof_ExitCode(0) == (int)NATIVE_ARCADE_ROSTER_PROOF_INCOMPLETE);
	NativeArcadeRosterProof_RecordExitCode((int)NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
	CHECK(NativeArcadeRosterProof_ExitCode(0) == (int)NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
	NativeArcadeRosterProof_Shutdown();
	CHECK(NativeArcadeRosterProof_ExitCode(0) == 0);
	return 0;
}

static int EncodeConfig(const struct NativeMatchConfigV1 *config, uint8_t *bytes, size_t size, size_t *length)
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, size, NULL);
	if (!NativeMatchConfigV1_Encode(&writer, config))
	{
		return 0;
	}
	*length = NativeCodecWriter_Size(&writer);
	return 1;
}

static int TestConfigBuilder(void)
{
	struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 a;
	struct NativeMatchConfigV1 b;
	struct NativeMatchConfigV1 c;
	struct NativeMatchConfigV1 untouched;
	struct MainArcadeRaceSetupPlan plan;
	uint8_t bytesA[1024];
	uint8_t bytesB[1024];
	uint8_t bytesC[1024];
	size_t lengthA = 0;
	size_t lengthB = 0;
	size_t lengthC = 0;
	uint8_t proofBuild[NATIVE_IDENTITY_DIGEST_BYTES];
	uint8_t expectedBuild[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeSha256 sha;

	TestIdentity(&identity);
	CHECK(NativeArcadeLinkFixture_Build(&identity, &base) == 1);

	/* Same identity and seed: byte-identical encoded configs. */
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, UINT64_C(1), &a) == 1);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, UINT64_C(1), &b) == 1);
	CHECK(EncodeConfig(&a, bytesA, sizeof(bytesA), &lengthA) == 1);
	CHECK(EncodeConfig(&b, bytesB, sizeof(bytesB), &lengthB) == 1);
	CHECK((lengthA == lengthB) && (lengthA > 0u) && (memcmp(bytesA, bytesB, lengthA) == 0));

	/* A different seed differs in masterSeed, and only there. */
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, UINT64_C(2), &c) == 1);
	CHECK(c.masterSeed != a.masterSeed);
	CHECK(EncodeConfig(&c, bytesC, sizeof(bytesC), &lengthC) == 1);
	CHECK((lengthC == lengthA) && (memcmp(bytesC, bytesA, lengthA) != 0));
	c.masterSeed = a.masterSeed;
	CHECK(EncodeConfig(&c, bytesC, sizeof(bytesC), &lengthC) == 1);
	CHECK(memcmp(bytesC, bytesA, lengthA) == 0);

	/* TWO_CAB is unchanged by the profile parameter (RS-23): the masterSeed and
	 * config digest the builder gave before OC-3 (computed at 30d5a1c71) for
	 * this identity. */
	{
		static const struct
		{
			uint64_t seed;
			uint64_t masterSeed;
			uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
		} golden[] = {
			{UINT64_C(1), UINT64_C(0x95B5E53F2586F411),
				{0xd0, 0x41, 0xe8, 0x82, 0xc9, 0xc5, 0x65, 0x65, 0x30, 0xd6, 0xc8, 0x72, 0xab, 0xc3, 0xc3, 0xd6, 0xd2, 0xa3,
					0xd2, 0xa5, 0x8d, 0xe6, 0xcb, 0x31, 0x9c, 0x42, 0x0a, 0x6e, 0xd9, 0xed, 0xc7, 0x0c}},
			{UINT64_C(0x5EED), UINT64_C(0xBE8D3076EF263FC8),
				{0x16, 0xcd, 0x0f, 0x09, 0x3d, 0xcd, 0x71, 0xd5, 0xd0, 0x96, 0x89, 0x39, 0x30, 0x6b, 0x58, 0x18, 0x91, 0xc6,
					0x74, 0xb5, 0xd3, 0xb6, 0x31, 0xd1, 0x93, 0xef, 0x79, 0xdb, 0x1b, 0x35, 0xec, 0xfa}},
			{UINT64_C(9), UINT64_C(0xF7E8821AE8D200FD),
				{0xc4, 0xa6, 0x52, 0x9a, 0xaa, 0x1a, 0xb8, 0xb5, 0x98, 0x7d, 0xdf, 0x7d, 0x53, 0x0a, 0x2c, 0x7f, 0x2a, 0xfa,
					0x8f, 0x6c, 0x82, 0xf3, 0x94, 0x77, 0x52, 0x13, 0xcb, 0x08, 0x76, 0x58, 0x35, 0xd3}},
		};

		for (uint32_t i = 0; i < (uint32_t)(sizeof(golden) / sizeof(golden[0])); i++)
		{
			struct NativeMatchConfigV1 pinned;
			uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

			CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, golden[i].seed, &pinned) == 1);
			CHECK(pinned.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
			CHECK(pinned.masterSeed == golden[i].masterSeed);
			CHECK(NativeMatchConfigV1_Digest(&pinned, digest) == 1);
			CHECK(memcmp(digest, golden[i].digest, sizeof(digest)) == 0);
		}
	}

	/* The fixture's choices: characters, track, and laps kept; a fresh seed. */
	CHECK((a.trackID == base.trackID) && (a.lapCount == base.lapCount));
	for (uint32_t slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		CHECK(a.slots[slot].characterID == base.slots[slot].characterID);
		CHECK(a.slots[slot].difficulty == base.slots[slot].difficulty);
		CHECK(a.slots[slot].role == base.slots[slot].role);
	}
	CHECK((a.masterSeed != 0u) && (a.masterSeed != base.masterSeed));
	CHECK(memcmp(a.buildIdentity, identity.build, sizeof(identity.build)) == 0);
	CHECK(memcmp(a.contentIdentity, identity.content, sizeof(identity.content)) == 0);

	/* It passes the bot rules and the race setup plan. */
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&a) == 1);
	CHECK(NativeArcadeBotRules_ValidateConfigV1(&c) == 1);
	CHECK(MainArcadeRaceSetupPlan_Build(&a, &plan) == 1);
	CHECK((plan.levelID == (int32_t)base.trackID) && (plan.numLaps == (int8_t)base.lapCount));
	CHECK(MainArcadeRaceSetupPlan_Build(&c, &plan) == 1);

	/* Rejections leave the output untouched. */
	memset(&untouched, SENTINEL_BYTE, sizeof(untouched));
	CHECK(NativeArcadeRosterProof_BuildConfig(NULL, TWO_CAB, 1u, &untouched) == 0);
	CHECK(IsAllByte(&untouched, sizeof(untouched), SENTINEL_BYTE));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, 1u, NULL) == 0);
	memset(identity.build, 0, sizeof(identity.build));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, 1u, &untouched) == 0);
	CHECK(IsAllByte(&untouched, sizeof(untouched), SENTINEL_BYTE));

	/* The fixed proof build identity is SHA-256 of its tag. */
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG, strlen(NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG));
	NativeSha256_Final(&sha, expectedBuild);
	CHECK(NativeArcadeRosterProof_ProofBuildIdentity(proofBuild) == 1);
	CHECK(memcmp(proofBuild, expectedBuild, sizeof(proofBuild)) == 0);
	CHECK(NativeArcadeRosterProof_ProofBuildIdentity(NULL) == 0);
	memcpy(identity.build, proofBuild, sizeof(identity.build));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, 1u, &a) == 1);
	return 0;
}

/* The ONE_CAB proof config (RS-23): no match select; the fixture's identity,
 * track, laps, tick rate, CAB1 character, and bot difficulty; the 1P bots;
 * the seed as masterSeed; the 1P bot rules digest. */
static int TestOneCabConfigBuilder(void)
{
	static const uint64_t seeds[] = {UINT64_C(0x5EED), UINT64_C(0x5EEE), UINT64_C(0), UINT64_C(1), UINT64_MAX};
	/* ExpectedBots1P(Crash 0): every base character but 0, ascending. */
	static const uint8_t goldenCharacters[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
	struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 a;
	struct NativeMatchConfigV1 b;
	struct NativeMatchConfigV1 c;
	struct NativeMatchConfigV1 twoCab;
	struct NativeMatchConfigV1 untouched;
	struct MainArcadeRaceSetupPlan plan;
	uint8_t bytesA[1024];
	uint8_t bytesB[1024];
	uint8_t bytesC[1024];
	size_t lengthA = 0;
	size_t lengthB = 0;
	size_t lengthC = 0;
	uint8_t digestA[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t digestB[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t digestC[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t digestTwoCab[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t rules1P[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t rules2P[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t bots[NATIVE_ARCADE_BOT_RULES_1P_BOT_COUNT];
	uint8_t cab1Slot = 0;
	uint8_t botDifficulty = 0;
	int botFound = 0;

	TestIdentity(&identity);
	CHECK(NativeArcadeLinkFixture_Build(&identity, &base) == 1);
	CHECK(NativeMatchConfigV1_FindRoleSlot(&base, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot) == 1);
	/* The fixture's bot difficulty: its first BOT slot, found as the builder finds it. */
	for (uint32_t slot = 0; (slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT) && !botFound; slot++)
	{
		if (base.slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT)
		{
			botDifficulty = base.slots[slot].difficulty;
			botFound = 1;
		}
	}
	CHECK(botFound == 1);
	CHECK(NativeArcadeBotRules_ExpectedBots1P(base.slots[cab1Slot].characterID, bots) == 1);
	CHECK(NativeArcadeBotRules_Digest1PV1(rules1P) == 1);
	CHECK(NativeArcadeBotRules_DigestV1(rules2P) == 1);

	for (uint32_t i = 0; i < (uint32_t)(sizeof(seeds) / sizeof(seeds[0])); i++)
	{
		memset(&a, SENTINEL_BYTE, sizeof(a));
		CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, seeds[i], &a) == 1);
		CHECK(NativeArcadeBotRules_ValidateConfigV1(&a) == 1);
		CHECK(a.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
		CHECK(a.masterSeed == seeds[i]);
		CHECK((a.gameMode1 == 0u) && (a.gameMode2 == 0u) && (a.rules == 0u));
		CHECK((a.trackID == base.trackID) && (a.lapCount == base.lapCount));
		CHECK((a.tickRateNumerator == base.tickRateNumerator) && (a.tickRateDenominator == base.tickRateDenominator));
		CHECK((a.tickRateNumerator == 30u) && (a.tickRateDenominator == 1u));
		CHECK(memcmp(a.buildIdentity, identity.build, sizeof(identity.build)) == 0);
		CHECK(memcmp(a.contentIdentity, identity.content, sizeof(identity.content)) == 0);
		CHECK(memcmp(a.botRulesDigest, rules1P, sizeof(rules1P)) == 0);
		CHECK(memcmp(a.botRulesDigest, rules2P, sizeof(rules2P)) != 0);
		CHECK(IsAllByte(a.reserved, sizeof(a.reserved), 0u));
		/* Slot 0 the CAB1 human at difficulty 0; slots 1..7 the 1P bots at the fixture's bot difficulty. */
		CHECK(a.slots[0].role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
		CHECK(a.slots[0].characterID == base.slots[cab1Slot].characterID && a.slots[0].difficulty == 0u);
		for (uint32_t slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
		{
			CHECK(a.slots[slot].initialLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE);
			CHECK(a.slots[slot].characterID == goldenCharacters[slot]);
			CHECK(IsAllByte(a.slots[slot].reserved, sizeof(a.slots[slot].reserved), 0u));
			if (slot >= 1u)
			{
				CHECK(a.slots[slot].role == NATIVE_MATCH_SLOT_ROLE_BOT);
				CHECK(a.slots[slot].characterID == bots[slot - 1u]);
				CHECK(a.slots[slot].difficulty == botDifficulty);
				CHECK(a.slots[slot].difficulty == NATIVE_ARCADE_BOT_RULES_DEFAULT_DIFFICULTY);
			}
		}
		/* The race setup plan builds the 1P arcade race from it. */
		CHECK(MainArcadeRaceSetupPlan_Build(&a, &plan) == 1);
		CHECK(plan.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
		CHECK(plan.numPlyrNextGame == 1u && plan.firstBotSlot == 1u && plan.botCount == 7u);
		CHECK((plan.levelID == (int32_t)base.trackID) && (plan.numLaps == (int8_t)base.lapCount));
		CHECK(plan.masterSeed == seeds[i]);
		for (uint32_t slot = 0; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
		{
			CHECK(plan.characterIDs[slot] == (int16_t)goldenCharacters[slot]);
		}
	}

	/* Same identity and seed: byte-identical configs and digests. */
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, UINT64_C(0x5EED), &a) == 1);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, UINT64_C(0x5EED), &b) == 1);
	CHECK(memcmp(&a, &b, sizeof(a)) == 0);
	CHECK(EncodeConfig(&a, bytesA, sizeof(bytesA), &lengthA) == 1);
	CHECK(EncodeConfig(&b, bytesB, sizeof(bytesB), &lengthB) == 1);
	CHECK((lengthA == lengthB) && (lengthA > 0u) && (memcmp(bytesA, bytesB, lengthA) == 0));
	CHECK(NativeMatchConfigV1_Digest(&a, digestA) == 1);
	CHECK(NativeMatchConfigV1_Digest(&b, digestB) == 1);
	CHECK(memcmp(digestA, digestB, sizeof(digestA)) == 0);

	/* A different seed: a different config digest, and a difference only in masterSeed. */
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, UINT64_C(0x5EEE), &c) == 1);
	CHECK(NativeMatchConfigV1_Digest(&c, digestC) == 1);
	CHECK(memcmp(digestA, digestC, sizeof(digestA)) != 0);
	c.masterSeed = a.masterSeed;
	CHECK(EncodeConfig(&c, bytesC, sizeof(bytesC), &lengthC) == 1);
	CHECK((lengthC == lengthA) && (memcmp(bytesC, bytesA, lengthA) == 0));

	/* ONE_CAB and TWO_CAB for the same identity and seed are different configs. */
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, UINT64_C(0x5EED), &twoCab) == 1);
	CHECK(twoCab.profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB);
	CHECK(NativeMatchConfigV1_Digest(&twoCab, digestTwoCab) == 1);
	CHECK(memcmp(digestA, digestTwoCab, sizeof(digestA)) != 0);

	/* Rejections leave the output untouched: an unknown profile, NULL
	 * arguments, and an identity the fixture refuses. */
	memset(&untouched, SENTINEL_BYTE, sizeof(untouched));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, 0u, UINT64_C(1), &untouched) == 0);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, 3u, UINT64_C(1), &untouched) == 0);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, UINT32_MAX, UINT64_C(1), &untouched) == 0);
	CHECK(NativeArcadeRosterProof_BuildConfig(NULL, ONE_CAB, UINT64_C(1), &untouched) == 0);
	CHECK(IsAllByte(&untouched, sizeof(untouched), SENTINEL_BYTE));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, UINT64_C(1), NULL) == 0);
	memset(identity.content, 0, sizeof(identity.content));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, UINT64_C(1), &untouched) == 0);
	CHECK(IsAllByte(&untouched, sizeof(untouched), SENTINEL_BYTE));
	return 0;
}

/* One pad's expected bytes: status, id, the two button bytes, connected. */
static int ExpectPad(const struct NativeArcadeRosterProofPad *pad, uint8_t status, uint8_t id, uint8_t buttons0, uint8_t buttons1,
	uint8_t connected)
{
	CHECK(pad->status == status && pad->id == id);
	CHECK(pad->buttons[0] == buttons0 && pad->buttons[1] == buttons1);
	CHECK(pad->analog[0] == 0x80u && pad->analog[1] == 0x80u && pad->analog[2] == 0x80u && pad->analog[3] == 0x80u);
	CHECK(pad->connected == connected);
	CHECK(IsAllByte(pad->reserved, sizeof(pad->reserved), 0u));
	return 0;
}

/* The scripted pad pattern: golden values, a pure function of the race tick. */
static int TestScriptedPads(void)
{
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	struct NativeArcadeRosterProofPad again[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	/* Active-low button words: none 0xFFFF; CROSS clears 0x4000; RIGHT 0x0020. */
	static const struct
	{
		uint32_t tick;
		uint8_t p0[2];
		uint8_t p1[2];
	} golden[] = {
		{NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, {0xFFu, 0xFFu}, {0xFFu, 0xFFu}},
		{0u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{1u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{59u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{60u, {0xFFu, 0xBFu}, {0xDFu, 0xBFu}},
		{75u, {0xFFu, 0xBFu}, {0xDFu, 0xBFu}},
		{89u, {0xFFu, 0xBFu}, {0xDFu, 0xBFu}},
		{90u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{119u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{120u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{180u, {0xFFu, 0xBFu}, {0xDFu, 0xBFu}},
		{899u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{3599u, {0xFFu, 0xBFu}, {0xFFu, 0xBFu}},
		{3540u, {0xFFu, 0xBFu}, {0xDFu, 0xBFu}},
	};
	uint32_t steering = 0;

	for (uint32_t i = 0; i < (uint32_t)(sizeof(golden) / sizeof(golden[0])); i++)
	{
		memset(pads, SENTINEL_BYTE, sizeof(pads));
		NativeArcadeRosterProof_ScriptedPads(TWO_CAB, golden[i].tick, pads);
		CHECK(ExpectPad(&pads[0], 0u, 0x41u, golden[i].p0[0], golden[i].p0[1], 1u) == 0);
		CHECK(ExpectPad(&pads[1], 0u, 0x41u, golden[i].p1[0], golden[i].p1[1], 1u) == 0);
		CHECK(ExpectPad(&pads[2], 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u) == 0);
		CHECK(ExpectPad(&pads[3], 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u) == 0);
	}
	/* Pure: the same tick gives the same bytes, whatever the output held. */
	for (uint32_t tick = 0; tick < 900u; tick++)
	{
		memset(pads, 0x00, sizeof(pads));
		memset(again, 0xA5, sizeof(again));
		NativeArcadeRosterProof_ScriptedPads(TWO_CAB, tick, pads);
		NativeArcadeRosterProof_ScriptedPads(TWO_CAB, tick, again);
		CHECK(memcmp(pads, again, sizeof(pads)) == 0);
		CHECK(pads[0].buttons[1] == 0xBFu && pads[1].buttons[1] == 0xBFu);
		if (pads[1].buttons[0] == 0xDFu)
		{
			steering++;
		}
	}
	/* Seven 30-tick steering windows in ticks 0..899 (60..89, ..., 780..809). */
	CHECK(steering == 210u);
	NativeArcadeRosterProof_ScriptedPads(TWO_CAB, 5u, NULL);
	CHECK(NATIVE_ARCADE_ROSTER_PROOF_STEER_PERIOD == 120u && NATIVE_ARCADE_ROSTER_PROOF_STEER_BEGIN == 60u &&
	      NATIVE_ARCADE_ROSTER_PROOF_STEER_END == 90u);
	return 0;
}

/* The ONE_CAB scripted pads (RS-24): the same pad layout as TWO_CAB; player 0
 * holds CROSS and steers RIGHT in [60, 90) of every 120; player 1 stays
 * neutral. Golden values, a pure function of (profile, race tick). */
static int TestScriptedPadsOneCab(void)
{
	struct NativeArcadeRosterProofPad pads[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	struct NativeArcadeRosterProofPad again[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	struct NativeArcadeRosterProofPad twoCab[NATIVE_ARCADE_ROSTER_PROOF_PAD_COUNT];
	/* Active-low button words: none 0xFFFF; CROSS clears 0x4000; RIGHT 0x0020. */
	static const struct
	{
		uint32_t tick;
		uint8_t p0[2];
		uint8_t p1[2];
	} golden[] = {
		{NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, {0xFFu, 0xFFu}, {0xFFu, 0xFFu}},
		{0u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{1u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{59u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{60u, {0xDFu, 0xBFu}, {0xFFu, 0xFFu}},
		{75u, {0xDFu, 0xBFu}, {0xFFu, 0xFFu}},
		{89u, {0xDFu, 0xBFu}, {0xFFu, 0xFFu}},
		{90u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{119u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{120u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{180u, {0xDFu, 0xBFu}, {0xFFu, 0xFFu}},
		{899u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{3599u, {0xFFu, 0xBFu}, {0xFFu, 0xFFu}},
		{3540u, {0xDFu, 0xBFu}, {0xFFu, 0xFFu}},
	};
	uint32_t steering = 0;

	for (uint32_t i = 0; i < (uint32_t)(sizeof(golden) / sizeof(golden[0])); i++)
	{
		memset(pads, SENTINEL_BYTE, sizeof(pads));
		NativeArcadeRosterProof_ScriptedPads(ONE_CAB, golden[i].tick, pads);
		CHECK(ExpectPad(&pads[0], 0u, 0x41u, golden[i].p0[0], golden[i].p0[1], 1u) == 0);
		CHECK(ExpectPad(&pads[1], 0u, 0x41u, golden[i].p1[0], golden[i].p1[1], 1u) == 0);
		CHECK(ExpectPad(&pads[2], 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u) == 0);
		CHECK(ExpectPad(&pads[3], 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0u) == 0);
	}
	for (uint32_t tick = 0; tick < 900u; tick++)
	{
		memset(pads, 0x00, sizeof(pads));
		memset(again, 0xA5, sizeof(again));
		NativeArcadeRosterProof_ScriptedPads(ONE_CAB, tick, pads);
		NativeArcadeRosterProof_ScriptedPads(ONE_CAB, tick, again);
		CHECK(memcmp(pads, again, sizeof(pads)) == 0);
		CHECK(pads[0].buttons[1] == 0xBFu);
		CHECK(pads[1].buttons[0] == 0xFFu && pads[1].buttons[1] == 0xFFu && pads[1].connected == 1u);
		if (pads[0].buttons[0] == 0xDFu)
		{
			steering++;
		}
		else
		{
			CHECK(pads[0].buttons[0] == 0xFFu);
		}
		/* Player 0 steers exactly when TWO_CAB's player 1 does. */
		NativeArcadeRosterProof_ScriptedPads(TWO_CAB, tick, twoCab);
		CHECK(pads[0].buttons[0] == twoCab[1].buttons[0]);
		/* Pads 2 and 3 are identical in both profiles. */
		CHECK(memcmp(&pads[2], &twoCab[2], sizeof(pads[0]) * 2u) == 0);
	}
	CHECK(steering == 210u);

	/* The neutral frames (TICK_NONE) are byte-identical in both profiles. */
	NativeArcadeRosterProof_ScriptedPads(ONE_CAB, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, pads);
	NativeArcadeRosterProof_ScriptedPads(TWO_CAB, NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE, twoCab);
	CHECK(memcmp(pads, twoCab, sizeof(pads)) == 0);

	/* Any other profile: the neutral pads at every tick. */
	for (uint32_t tick = 0; tick < 240u; tick++)
	{
		memset(again, 0xA5, sizeof(again));
		NativeArcadeRosterProof_ScriptedPads(0u, tick, again);
		CHECK(memcmp(again, twoCab, sizeof(again)) == 0);
		NativeArcadeRosterProof_ScriptedPads(3u, tick, again);
		CHECK(memcmp(again, twoCab, sizeof(again)) == 0);
	}
	NativeArcadeRosterProof_ScriptedPads(ONE_CAB, 5u, NULL);
	return 0;
}

/* The tick lines: kept in order, bounded, formatted, and written with the report. */
static int TestTickLines(void)
{
	struct NativeIdentityV1 identity;
	struct NativeArcadeRosterProofOptions options;
	struct NativeArcadeRosterProofTickLine line;
	struct NativeArcadeRosterProofReport report;
	static const char path[] = "native_arcade_roster_proof_test_report.txt";
	static char text[8192];
	char small[32];
	size_t length = 0;
	FILE *file;

	memset(&line, 0, sizeof(line));
	line.tick = 7u;
	line.control = UINT64_C(0x0123456789ABCDEF);
	line.raceControl = UINT64_C(0xA1B2C3D4E5F60718);
	line.rng = UINT64_C(0xFEDCBA9876543210);
	line.input = UINT64_C(1);
	FillCounting(line.drivers, sizeof(line.drivers), 0u);
	CHECK(NativeArcadeRosterProof_FormatTickLine(&line, text, sizeof(text), &length) == 1);
	CHECK(strcmp(text, "tick 7 control 0123456789abcdef rcontrol a1b2c3d4e5f60718 rng fedcba9876543210 input 0000000000000001 drivers "
	                   "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n") == 0);
	CHECK(length == strlen(text));
	CHECK(NativeArcadeRosterProof_FormatTickLine(&line, small, sizeof(small), &length) == 0);
	CHECK(NativeArcadeRosterProof_FormatTickLine(NULL, text, sizeof(text), &length) == 0);
	CHECK(NativeArcadeRosterProof_FormatTickLine(&line, text, sizeof(text), NULL) == 0);

	/* Inactive: nothing is kept. */
	NativeArcadeRosterProof_Shutdown();
	line.tick = 0u;
	CHECK(NativeArcadeRosterProof_RecordTick(&line) == 0);
	CHECK(NativeArcadeRosterProof_TickCount() == 0u && NativeArcadeRosterProof_Ticks() == 0u);

	/* A tick count outside 1..3600 does not configure. */
	TestIdentity(&identity);
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	options.enabled = 1u;
	memcpy(options.logPath, path, sizeof(path));
	options.tickCount = 0u;
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
	options.tickCount = NATIVE_ARCADE_ROSTER_PROOF_MAX_TICKS + 1u;
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);

	/* Three ticks: only the next tick is kept, and no more than three. */
	options.tickCount = 3u;
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeRosterProof_Ticks() == 3u);
	line.tick = 1u;
	CHECK(NativeArcadeRosterProof_RecordTick(&line) == 0);
	CHECK(NativeArcadeRosterProof_RecordTick(NULL) == 0);
	for (uint32_t tick = 0; tick < 3u; tick++)
	{
		line.tick = tick;
		line.rng = tick;
		CHECK(NativeArcadeRosterProof_RecordTick(&line) == 1);
		CHECK(NativeArcadeRosterProof_TickCount() == tick + 1u);
	}
	line.tick = 3u;
	CHECK(NativeArcadeRosterProof_RecordTick(&line) == 0);
	CHECK(NativeArcadeRosterProof_TickCount() == 3u);

	/* The written report: the formatted header, the tick lines in order, then "end ticks". */
	memset(&report, 0, sizeof(report));
	report.result = NATIVE_ARCADE_ROSTER_PROOF_PASS;
	report.profile = ONE_CAB;
	memcpy(report.setupStatusName, "VALIDATED", sizeof("VALIDATED"));
	memcpy(report.setupFailureName, "NONE", sizeof("NONE"));
	report.ticksRequested = 3u;
	report.menuReadyTick = 732u;
	report.demoRaceTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
	report.launchTick = 732u;
	report.launchWindow = NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE;
	report.validatedTick = 762u;
	report.raceTickZeroTick = 762u;
	report.countersValid = 1u;
	report.raceTickZeroCounters.timer = 1466;
	report.raceTickZeroCounters.frameCounter = 1523;
	report.raceTickZeroCounters.frameTimer = -2;
	report.raceTickZeroCounters.frameTimerConfetti = 57;
	report.launchCountersValid = 1u;
	report.launchCounters.timer = 701;
	report.launchCounters.frameCounter = 733;
	report.launchCounters.frameTimer = -7;
	report.launchCounters.frameTimerConfetti = -1402;
	(void)remove(path);
	CHECK(NativeArcadeRosterProof_WriteReport(&report) == 1);
	file = fopen(path, "rb");
	CHECK(file != NULL);
	length = fread(text, 1u, sizeof(text) - 1u, file);
	(void)fclose(file);
	(void)remove(path);
	text[length] = '\0';
	{
		static const char head[] = "arcade roster proof v9\ndrivers digest excludes physics\nresult PASS (0)\nprofile ONE_CAB\n"
		                           "setup status VALIDATED (0)\n";

		CHECK(strncmp(text, head, sizeof(head) - 1u) == 0);
	}
	CHECK(strstr(text, "\ndwell 0\nticks 3\nmenu ready tick 732\n") != NULL);
	CHECK(strstr(text, "\nlaunch window title\n"
	                   "launch counters timer 701 frameCounter 733 frameTimer -7 frameTimerConfetti -1402\n"
	                   "validated tick 762\n") != NULL);
	CHECK(strstr(text, "\nvalidated tick 762\nrace tick 0 tick 762\n"
	                   "race tick 0 counters timer 1466 frameCounter 1523 frameTimer -2 frameTimerConfetti 57\n"
	                   "config digest none\n") != NULL);
	CHECK(strstr(text, "slot 7 none\nhold none\n"
	                   "tick 0 control 0123456789abcdef rcontrol a1b2c3d4e5f60718 rng 0000000000000000 input 0000000000000001 drivers 0001")
	      != NULL);
	CHECK(strstr(text, "\ntick 1 control 0123456789abcdef rcontrol a1b2c3d4e5f60718 rng 0000000000000001 ") != NULL);
	CHECK(strstr(text, "\ntick 2 control 0123456789abcdef rcontrol a1b2c3d4e5f60718 rng 0000000000000002 ") != NULL);
	CHECK((length > 12u) && (strcmp(text + length - 12u, "end ticks 3\n") == 0));
	NativeArcadeRosterProof_Shutdown();
	CHECK(NativeArcadeRosterProof_TickCount() == 0u);
	return 0;
}

/* The seed readback comparison and the PASS gate. */
static int TestSeedsAndFinalResult(void)
{
	struct NativeArcadeRetailRngSeedsV1 produced;
	struct NativeArcadeRetailRngSeedsV1 stored;
	struct NativeArcadeRosterProofPins pinsProduced;
	struct NativeArcadeRosterProofPins pinsStored;
	struct NativeArcadeRosterProofReport report;

	produced.randomNumber = 0x7D2Eu;
	produced.advRng0 = 0x60C79386u;
	produced.advRng1 = 0x78DFDBA8u;
	produced.psxRandSeed = 0x1472E10Bu;
	produced.audioRNG = 0x75599A57u;
	stored = produced;
	CHECK(NativeArcadeRosterProof_SeedsMatch(&produced, &stored) == 1);
	/* Any one field differing is a mismatch, a swapped pair included. */
	for (uint32_t field = 0; field < 5u; field++)
	{
		stored = produced;
		if (field == 0u)
		{
			stored.randomNumber ^= 1u;
		}
		else if (field == 1u)
		{
			stored.advRng0 ^= 0x80000000u;
		}
		else if (field == 2u)
		{
			stored.advRng1 += 1u;
		}
		else if (field == 3u)
		{
			stored.psxRandSeed = 0u;
		}
		else
		{
			stored.audioRNG = produced.psxRandSeed;
		}
		CHECK(NativeArcadeRosterProof_SeedsMatch(&produced, &stored) == 0);
	}
	stored = produced;
	stored.advRng0 = produced.advRng1;
	stored.advRng1 = produced.advRng0;
	CHECK(NativeArcadeRosterProof_SeedsMatch(&produced, &stored) == 0);
	CHECK(NativeArcadeRosterProof_SeedsMatch(NULL, &stored) == 0);
	CHECK(NativeArcadeRosterProof_SeedsMatch(&produced, NULL) == 0);

	/* The pinned counters (RS-17): either one differing, or swapped, is a mismatch. */
	pinsProduced.timer = 0;
	pinsProduced.frameTimerConfetti = 0;
	pinsStored = pinsProduced;
	CHECK(NativeArcadeRosterProof_PinsMatch(&pinsProduced, &pinsStored) == 1);
	pinsStored.timer = 37;
	CHECK(NativeArcadeRosterProof_PinsMatch(&pinsProduced, &pinsStored) == 0);
	pinsStored = pinsProduced;
	pinsStored.frameTimerConfetti = -1;
	CHECK(NativeArcadeRosterProof_PinsMatch(&pinsProduced, &pinsStored) == 0);
	pinsProduced.timer = 1;
	pinsStored.timer = 0;
	pinsStored.frameTimerConfetti = 1;
	pinsProduced.frameTimerConfetti = 0;
	CHECK(NativeArcadeRosterProof_PinsMatch(&pinsProduced, &pinsStored) == 0);
	CHECK(NativeArcadeRosterProof_PinsMatch(NULL, &pinsStored) == 0);
	CHECK(NativeArcadeRosterProof_PinsMatch(&pinsProduced, NULL) == 0);

	/* PASS needs the digests, the slot facts, matching seed and pin
	 * readbacks, race tick 0's counters, and every requested tick line. */
	memset(&report, 0, sizeof(report));
	report.digestsValid = 1u;
	report.slotsValid = 1u;
	report.seedValid = 1u;
	report.seedMatch = 1u;
	report.pinValid = 1u;
	report.pinMatch = 1u;
	report.launchCountersValid = 1u;
	report.countersValid = 1u;
	report.ticksRequested = 900u;
	report.tickLineCount = 900u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_PASS);
	report.launchCountersValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.launchCountersValid = 1u;
	report.tickLineCount = 899u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.tickLineCount = 901u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.tickLineCount = 0u;
	report.ticksRequested = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.tickLineCount = 900u;
	report.ticksRequested = 900u;
	report.countersValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.countersValid = 1u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_PASS);
	/* A requested hold (LR-S2 (a)) must have run and kept both frameTimer
	 * values; the hold's measurements themselves are judged by the check. */
	report.holdRequested = 1u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.holdDone = 1u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.hold.frameTimerValid = 1u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.hold.frameTimerValid = 2u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.hold.frameTimerValid = 3u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_PASS);
	report.holdRequested = 0u;
	report.holdDone = 0u;
	report.hold.frameTimerValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_PASS);
	report.digestsValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.digestsValid = 1u;
	report.slotsValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.slotsValid = 1u;
	report.seedValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.seedValid = 1u;
	report.pinValid = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	report.pinValid = 1u;
	report.pinMatch = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_PIN_MISMATCH);
	report.seedMatch = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH);
	report.pinMatch = 1u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH);
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, NULL) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	/* A failure is reported as requested, whatever the evidence. */
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED, &report) == NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED, NULL) == NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED);
	return 0;
}

/* The CONTROL domain digest of a state. */
static uint64_t ControlDigest(const struct NativeCanonicalStateV1 *state)
{
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		if (NativeCanonicalDomainOrder[i] == (uint32_t)NATIVE_CANONICAL_DOMAIN_CONTROL)
		{
			return state->domainDigests[i];
		}
	}
	return 0u;
}

/* The race-relative control digest: the V1 control digest with the
 * boot-relative counters zeroed, and only those. */
static int TestRaceControlDigest(void)
{
	struct NativeCanonicalStateV1 state;
	struct NativeCanonicalStateV1 zeroed;
	struct NativeCanonicalStateV1 before;
	uint64_t digest = 0;
	uint64_t other = 0;
	uint64_t untouched = UINT64_C(0x5A5A5A5A5A5A5A5A);

	NativeCanonicalStateV1_Init(&state);
	state.control.frameTimer = 3059;
	state.control.frameCounter = 1523;
	state.control.timer = 1466;
	state.control.framesInThisLEV = 1;
	state.control.elapsedTimeMS = 32;
	state.control.msInThisLEV = 32;
	state.control.elapsedEventTime = 0;
	state.control.mainGameState = 3;
	state.control.loadingStage = -1;
	state.control.levelID = 3;
	state.control.gameMode1 = 0x00400000;
	state.control.gameMode2 = 0;
	state.rng.mixRandomNumber = 0x716Du;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&state) == 1);
	before = state;

	/* It is the V1 CONTROL digest of the state with the three counters zeroed. */
	zeroed = state;
	zeroed.control.frameTimer = 0;
	zeroed.control.frameCounter = 0;
	zeroed.control.timer = 0;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&zeroed) == 1);
	CHECK(NativeArcadeRosterProof_RaceControlDigest(&state, &digest) == 1);
	CHECK(digest == ControlDigest(&zeroed));
	CHECK(digest != ControlDigest(&state));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	/* With the counters already zero it is the control digest itself. */
	CHECK(NativeArcadeRosterProof_RaceControlDigest(&zeroed, &other) == 1);
	CHECK(other == ControlDigest(&zeroed) && other == digest);

	/* Any boot-relative offset leaves it unchanged, an odd one included. */
	state.control.frameTimer += 7;
	state.control.frameCounter += 37;
	state.control.timer += 37;
	CHECK(NativeArcadeRosterProof_RaceControlDigest(&state, &other) == 1);
	CHECK(other == digest);
	state = before;

	/* Every race-relative control value still counts. */
	for (uint32_t field = 0; field < 9u; field++)
	{
		int32_t *values[9] = {&state.control.framesInThisLEV, &state.control.elapsedTimeMS, &state.control.msInThisLEV,
			&state.control.elapsedEventTime, &state.control.mainGameState, &state.control.loadingStage, &state.control.levelID,
			&state.control.gameMode1, &state.control.gameMode2};

		state = before;
		*values[field] += 1;
		CHECK(NativeArcadeRosterProof_RaceControlDigest(&state, &other) == 1);
		CHECK(other != digest);
	}
	/* Other domains do not enter it. */
	state = before;
	state.rng.mixRandomNumber ^= 1u;
	state.input.pads[0].buttons[0] = (uint8_t)(state.input.pads[0].buttons[0] ^ 1u);
	CHECK(NativeArcadeRosterProof_RaceControlDigest(&state, &other) == 1);
	CHECK(other == digest);

	/* Rejections leave the output untouched. */
	CHECK(NativeArcadeRosterProof_RaceControlDigest(NULL, &untouched) == 0);
	CHECK(untouched == UINT64_C(0x5A5A5A5A5A5A5A5A));
	CHECK(NativeArcadeRosterProof_RaceControlDigest(&before, NULL) == 0);
	state = before;
	state.schemaVersion = 99u;
	CHECK(NativeArcadeRosterProof_RaceControlDigest(&state, &untouched) == 0);
	CHECK(untouched == UINT64_C(0x5A5A5A5A5A5A5A5A));
	return 0;
}

static int TestSingletonAndReport(void)
{
	struct NativeIdentityV1 identity;
	struct NativeArcadeRosterProofOptions options;
	struct NativeArcadeRosterProofReport report;
	struct NativeMatchConfigV1 expected;
	char text[4096];
	char small[64];
	size_t length = 0;

	TestIdentity(&identity);
	NativeArcadeRosterProof_Shutdown();
	CHECK(NativeArcadeRosterProof_Active() == 0);
	CHECK(NativeArcadeRosterProof_Config() == NULL);
	CHECK((NativeArcadeRosterProof_Dwell() == 0u) && (NativeArcadeRosterProof_Seed() == 0u));
	CHECK(NativeArcadeRosterProof_LogPath()[0] == '\0');

	/* Disabled options: inert, success. */
	NativeArcadeRosterProofOptions_SetDefaults(&options);
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeRosterProof_Active() == 0);
	CHECK(NativeArcadeRosterProof_Configure(NULL, &identity) == 1);
	CHECK(NativeArcadeRosterProof_Active() == 0);

	/* Enabled without an identity the fixture accepts: inactive, failure. */
	options.enabled = 1u;
	memcpy(options.logPath, "proof_report.txt", sizeof("proof_report.txt"));
	options.seed = UINT64_C(9);
	options.dwellTicks = 12u;
	CHECK(NativeArcadeRosterProof_Configure(&options, NULL) == 0);
	CHECK(NativeArcadeRosterProof_Active() == 0);

	/* Enabled: the config is BuildConfig's. */
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeRosterProof_Active() == 1);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, TWO_CAB, UINT64_C(9), &expected) == 1);
	CHECK(memcmp(NativeArcadeRosterProof_Config(), &expected, sizeof(expected)) == 0);
	CHECK((NativeArcadeRosterProof_Dwell() == 12u) && (NativeArcadeRosterProof_Seed() == UINT64_C(9)));
	CHECK(strcmp(NativeArcadeRosterProof_LogPath(), "proof_report.txt") == 0);
	CHECK(NativeArcadeRosterProof_Profile() == TWO_CAB);

	/* ONE_CAB: the config is BuildConfig's ONE_CAB config for the seed. */
	options.profile = ONE_CAB;
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);
	CHECK(NativeArcadeRosterProof_Active() == 1 && NativeArcadeRosterProof_Profile() == ONE_CAB);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, ONE_CAB, UINT64_C(9), &expected) == 1);
	CHECK(memcmp(NativeArcadeRosterProof_Config(), &expected, sizeof(expected)) == 0);
	CHECK(NativeArcadeRosterProof_Config()->profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB);
	CHECK(NativeArcadeRosterProof_Config()->masterSeed == UINT64_C(9));
	/* An unknown profile does not configure (and leaves the proof inactive). */
	options.profile = 3u;
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 0);
	CHECK(NativeArcadeRosterProof_Active() == 0 && NativeArcadeRosterProof_Profile() == 0u);
	options.profile = TWO_CAB;
	CHECK(NativeArcadeRosterProof_Configure(&options, &identity) == 1);

	/* The report formatter. */
	memset(&report, 0, sizeof(report));
	report.result = NATIVE_ARCADE_ROSTER_PROOF_PASS;
	report.profile = TWO_CAB;
	report.setupStatus = 4u;
	memcpy(report.setupStatusName, "VALIDATED", sizeof("VALIDATED"));
	memcpy(report.setupFailureName, "NONE", sizeof("NONE"));
	report.seed = UINT64_C(0x0123456789ABCDEF);
	report.dwellTicks = 45u;
	report.ticksRequested = 900u;
	report.menuReadyTick = 230u;
	report.demoRaceTick = 1200u;
	report.launchTick = 1275u;
	report.launchWindow = NATIVE_ARCADE_ROSTER_PROOF_WINDOW_DEMO_RACE;
	report.validatedTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
	report.raceTickZeroTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
	report.digestsValid = 1u;
	FillCounting(report.configDigest, sizeof(report.configDigest), 0u);
	report.slotsValid = 1u;
	report.slots[0].present = 1u;
	report.slots[0].role = NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
	report.slots[2].present = 1u;
	report.slots[2].role = NATIVE_MATCH_SLOT_ROLE_BOT;
	report.slots[2].characterID = 6u;
	report.slots[2].difficulty = 0xA0u;
	report.slots[2].spawnOrder = 2u;
	report.slots[2].navPathIndex = 1u;
	report.slots[2].accelerationOrder = 3u;
	report.seedValid = 1u;
	report.seedMatch = 1u;
	report.seedStored.randomNumber = 0x7D2Eu;
	report.seedStored.advRng0 = 0x60C79386u;
	report.seedStored.advRng1 = 0x78DFDBA8u;
	report.seedStored.psxRandSeed = 0x1472E10Bu;
	report.seedStored.audioRNG = 0x75599A57u;
	report.pinValid = 1u;
	report.pinMatch = 1u;
	report.pinStored.timer = 0;
	report.pinStored.frameTimerConfetti = 0;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(length == strlen(text));
	{
		static const char head[] = "arcade roster proof v9\ndrivers digest excludes physics\nresult PASS (0)\n"
		                           "profile TWO_CAB\nsetup status VALIDATED (4)\nsetup failure NONE (0)\n";

		CHECK(strncmp(text, head, sizeof(head) - 1u) == 0);
	}
	/* The profile line follows the result line, whatever the profile. */
	report.profile = ONE_CAB;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "\nresult PASS (0)\nprofile ONE_CAB\nsetup status VALIDATED (4)\n") != NULL);
	report.profile = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "\nresult PASS (0)\nprofile UNKNOWN\nsetup status VALIDATED (4)\n") != NULL);
	report.profile = TWO_CAB;
	CHECK(strstr(text, "seed 0x0123456789ABCDEF\ndwell 45\nticks 900\nmenu ready tick 230\ndemo race tick 1200\nlaunch tick 1275\n"
	                   "launch window demo race\nlaunch counters none\nvalidated tick none\nrace tick 0 tick none\n"
	                   "race tick 0 counters none\n"
	                   "config digest ") != NULL);
	CHECK(strstr(text, "config digest 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n") != NULL);
	CHECK(strstr(text, "slot 0 role CAB1_HUMAN character 0 difficulty 0x00 spawn 0 nav 0 accel 0\n") != NULL);
	CHECK(strstr(text, "slot 2 role BOT character 6 difficulty 0xA0 spawn 2 nav 1 accel 3\n") != NULL);
	CHECK(strstr(text, "slot 7 role INACTIVE\n") != NULL);
	/* The hold line (v9, LR-S2 (a)) ends the header: none, missing, or the evidence. */
	{
		static const char noHold[] = "slot 7 role INACTIVE\nhold none\n";
		static const char missing[] = "slot 7 role INACTIVE\nhold missing\n";

		CHECK((length >= sizeof(noHold) - 1u) && (strcmp(text + length - (sizeof(noHold) - 1u), noHold) == 0));
		report.holdRequested = 1u;
		CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
		CHECK((length >= sizeof(missing) - 1u) && (strcmp(text + length - (sizeof(missing) - 1u), missing) == 0));
	}
	report.holdDone = 1u;
	report.hold.raceTick = 300u;
	report.hold.periods = 45u;
	report.hold.wallUs = UINT64_C(1505012);
	report.hold.independentUs = UINT64_C(1505230);
	report.hold.independentValid = 1u;
	report.hold.expectedUs = UINT64_C(1504575);
	report.hold.pumps = 1398u;
	report.hold.minPeriodPumps = 29u;
	report.hold.bannersDue = 36u;
	report.hold.bannersPresented = 35u;
	report.hold.vsyncEntry = 12001;
	report.hold.vsyncExit = 12001;
	report.hold.frameTimerBefore = 12001;
	report.hold.frameTimerAfter = -3;
	report.hold.frameTimerValid = 3u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "slot 7 role INACTIVE\nhold tick 300 periods 45 wall us 1505012 independent us 1505230 "
	                   "expected us 1504575 pumps 1398 min pumps per period 29 banners due 36 presented 35 vsync entry 12001 exit 12001 "
	                   "frameTimer before 12001 after -3\n") != NULL);
	CHECK(text[length - 1u] == '\n' && strstr(text, "after -3\n") == text + length - 9u);
	report.hold.minPeriodPumps = UINT32_MAX;
	report.hold.frameTimerValid = 2u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, " min pumps per period none banners due 36 ") != NULL);
	CHECK(strstr(text, " frameTimer before none after -3\n") != NULL);
	report.hold.frameTimerValid = 1u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, " frameTimer before 12001 after none\n") != NULL);
	report.hold.independentValid = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, " wall us 1505012 independent us none expected us 1504575 ") != NULL);
	memset(&report.hold, 0, sizeof(report.hold));
	report.holdDone = 0u;
	report.holdRequested = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "bank digest 0000000000000000000000000000000000000000000000000000000000000000\n"
	                   "seeded randomNumber 0x7D2E advRng0 0x60C79386 advRng1 0x78DFDBA8 psxRand 0x1472E10B audioRNG 0x75599A57 "
	                   "timer 0 frameTimerConfetti 0 match 1\n"
	                   "slot 0 ") != NULL);
	/* "match" is 1 only when both the seeds and the pins match. */
	report.seedMatch = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "audioRNG 0x75599A57 timer 0 frameTimerConfetti 0 match 0\n") != NULL);
	report.seedMatch = 1u;
	report.pinMatch = 0u;
	report.pinStored.timer = -37;
	report.pinStored.frameTimerConfetti = 74;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "audioRNG 0x75599A57 timer -37 frameTimerConfetti 74 match 0\n") != NULL);
	report.pinValid = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "\nseeded none\n") != NULL);
	report.pinValid = 1u;
	report.seedValid = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "\nseeded none\n") != NULL);
	report.digestsValid = 0u;
	report.slotsValid = 0u;
	report.result = NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED;
	report.demoRaceTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
	report.launchTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
	report.launchWindow = NATIVE_ARCADE_ROSTER_PROOF_WINDOW_NONE;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "result SETUP_FAILED (22)\n") != NULL);
	CHECK(strstr(text, "menu ready tick 230\ndemo race tick none\nlaunch tick none\nlaunch window none\nlaunch counters none\n") != NULL);
	CHECK(strstr(text, "bank digest none\n") != NULL);
	CHECK(strstr(text, "slot 5 none\n") != NULL);
	CHECK(NativeArcadeRosterProof_FormatReport(&report, small, sizeof(small), &length) == 0);
	CHECK(NativeArcadeRosterProof_FormatReport(NULL, text, sizeof(text), &length) == 0);
	CHECK(NativeArcadeRosterProof_FormatReport(&report, NULL, sizeof(text), &length) == 0);
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), NULL) == 0);
	CHECK(strcmp(NativeArcadeRosterProof_ResultName(99u), "UNKNOWN") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_LaunchWindowName(NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE), "title") == 0);
	CHECK(strcmp(NativeArcadeRosterProof_LaunchWindowName(9u), "unknown") == 0);

	NativeArcadeRosterProof_Shutdown();
	CHECK(NativeArcadeRosterProof_Active() == 0);
	CHECK(NativeArcadeRosterProof_WriteReport(&report) == 0);
	return 0;
}

int main(void)
{
	CHECK(TestDefaults() == 0);
	CHECK(TestValidForms() == 0);
	CHECK(TestInvalidValues() == 0);
	CHECK(TestUnknownCombinations() == 0);
	CHECK(TestExitOptionNames() == 0);
	CHECK(TestExitCodes() == 0);
	CHECK(TestProfileOption() == 0);
	CHECK(TestHoldOption() == 0);
	CHECK(TestAutopilotOption() == 0);
	CHECK(TestConfigBuilder() == 0);
	CHECK(TestOneCabConfigBuilder() == 0);
	CHECK(TestSeedsAndFinalResult() == 0);
	CHECK(TestScriptedPads() == 0);
	CHECK(TestScriptedPadsOneCab() == 0);
	CHECK(TestTickLines() == 0);
	CHECK(TestRaceControlDigest() == 0);
	CHECK(TestSingletonAndReport() == 0);
	puts("native_arcade_roster_proof_test: ok");
	return 0;
}
