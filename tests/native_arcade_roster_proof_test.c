#include "platform/native_arcade_roster_proof.h"

#include "MAIN/MainArcadeRaceSetupPlan.h"
#include "platform/native_arcade_bot_rules.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_canonical_codec.h"
#include "platform/native_identity.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define ARGC(array) ((int)(sizeof(array) / sizeof((array)[0])))
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
	CHECK(IsAllByte(options.logPath, sizeof(options.logPath), 0u));
	CHECK(IsAllByte(options.reserved, sizeof(options.reserved), 0u));
	NativeArcadeRosterProofOptions_SetDefaults(NULL);

	/* Unrelated arguments leave the defaults. */
	CHECK(NativeArcadeRosterProofOptions_ApplyArgs(ARGC(none), none, &options) == 1);
	CHECK((options.enabled == 0u) && (options.seed == 1u) && (options.dwellTicks == 0u) && (options.logPath[0] == '\0'));
	CHECK(options.tickCount == 900u);
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
		NATIVE_ARCADE_ROSTER_PROOF_DIGEST_FAILED};
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
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, UINT64_C(1), &a) == 1);
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, UINT64_C(1), &b) == 1);
	CHECK(EncodeConfig(&a, bytesA, sizeof(bytesA), &lengthA) == 1);
	CHECK(EncodeConfig(&b, bytesB, sizeof(bytesB), &lengthB) == 1);
	CHECK((lengthA == lengthB) && (lengthA > 0u) && (memcmp(bytesA, bytesB, lengthA) == 0));

	/* A different seed differs in masterSeed, and only there. */
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, UINT64_C(2), &c) == 1);
	CHECK(c.masterSeed != a.masterSeed);
	CHECK(EncodeConfig(&c, bytesC, sizeof(bytesC), &lengthC) == 1);
	CHECK((lengthC == lengthA) && (memcmp(bytesC, bytesA, lengthA) != 0));
	c.masterSeed = a.masterSeed;
	CHECK(EncodeConfig(&c, bytesC, sizeof(bytesC), &lengthC) == 1);
	CHECK(memcmp(bytesC, bytesA, lengthA) == 0);

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
	CHECK(NativeArcadeRosterProof_BuildConfig(NULL, 1u, &untouched) == 0);
	CHECK(IsAllByte(&untouched, sizeof(untouched), SENTINEL_BYTE));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, 1u, NULL) == 0);
	memset(identity.build, 0, sizeof(identity.build));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, 1u, &untouched) == 0);
	CHECK(IsAllByte(&untouched, sizeof(untouched), SENTINEL_BYTE));

	/* The fixed proof build identity is SHA-256 of its tag. */
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG, strlen(NATIVE_ARCADE_ROSTER_PROOF_BUILD_TAG));
	NativeSha256_Final(&sha, expectedBuild);
	CHECK(NativeArcadeRosterProof_ProofBuildIdentity(proofBuild) == 1);
	CHECK(memcmp(proofBuild, expectedBuild, sizeof(proofBuild)) == 0);
	CHECK(NativeArcadeRosterProof_ProofBuildIdentity(NULL) == 0);
	memcpy(identity.build, proofBuild, sizeof(identity.build));
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, 1u, &a) == 1);
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
		NativeArcadeRosterProof_ScriptedPads(golden[i].tick, pads);
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
		NativeArcadeRosterProof_ScriptedPads(tick, pads);
		NativeArcadeRosterProof_ScriptedPads(tick, again);
		CHECK(memcmp(pads, again, sizeof(pads)) == 0);
		CHECK(pads[0].buttons[1] == 0xBFu && pads[1].buttons[1] == 0xBFu);
		if (pads[1].buttons[0] == 0xDFu)
		{
			steering++;
		}
	}
	/* Seven 30-tick steering windows in ticks 0..899 (60..89, ..., 780..809). */
	CHECK(steering == 210u);
	NativeArcadeRosterProof_ScriptedPads(5u, NULL);
	CHECK(NATIVE_ARCADE_ROSTER_PROOF_STEER_PERIOD == 120u && NATIVE_ARCADE_ROSTER_PROOF_STEER_BEGIN == 60u &&
	      NATIVE_ARCADE_ROSTER_PROOF_STEER_END == 90u);
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
	line.rng = UINT64_C(0xFEDCBA9876543210);
	line.input = UINT64_C(1);
	FillCounting(line.drivers, sizeof(line.drivers), 0u);
	CHECK(NativeArcadeRosterProof_FormatTickLine(&line, text, sizeof(text), &length) == 1);
	CHECK(strcmp(text, "tick 7 control 0123456789abcdef rng fedcba9876543210 input 0000000000000001 drivers "
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
	memcpy(report.setupStatusName, "VALIDATED", sizeof("VALIDATED"));
	memcpy(report.setupFailureName, "NONE", sizeof("NONE"));
	report.ticksRequested = 3u;
	report.menuReadyTick = 732u;
	report.demoRaceTick = NATIVE_ARCADE_ROSTER_PROOF_TICK_NONE;
	report.launchTick = 732u;
	report.launchWindow = NATIVE_ARCADE_ROSTER_PROOF_WINDOW_TITLE;
	report.validatedTick = 762u;
	report.raceTickZeroTick = 762u;
	(void)remove(path);
	CHECK(NativeArcadeRosterProof_WriteReport(&report) == 1);
	file = fopen(path, "rb");
	CHECK(file != NULL);
	length = fread(text, 1u, sizeof(text) - 1u, file);
	(void)fclose(file);
	(void)remove(path);
	text[length] = '\0';
	CHECK(strncmp(text, "arcade roster proof v4\ndrivers digest excludes physics\nresult PASS (0)\n", 71u) == 0);
	CHECK(strstr(text, "\ndwell 0\nticks 3\nmenu ready tick 732\n") != NULL);
	CHECK(strstr(text, "\nvalidated tick 762\nrace tick 0 tick 762\nconfig digest none\n") != NULL);
	CHECK(strstr(text, "slot 7 none\n"
	                   "tick 0 control 0123456789abcdef rng 0000000000000000 input 0000000000000001 drivers 0001")
	      != NULL);
	CHECK(strstr(text, "\ntick 1 control 0123456789abcdef rng 0000000000000001 ") != NULL);
	CHECK(strstr(text, "\ntick 2 control 0123456789abcdef rng 0000000000000002 ") != NULL);
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

	/* PASS needs the digests, the slot facts, and a matching readback. */
	memset(&report, 0, sizeof(report));
	report.digestsValid = 1u;
	report.slotsValid = 1u;
	report.seedValid = 1u;
	report.seedMatch = 1u;
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
	report.seedMatch = 0u;
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, &report) == NATIVE_ARCADE_ROSTER_PROOF_SEED_MISMATCH);
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_PASS, NULL) == NATIVE_ARCADE_ROSTER_PROOF_EVIDENCE_MISSING);
	/* A failure is reported as requested, whatever the evidence. */
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED, &report) == NATIVE_ARCADE_ROSTER_PROOF_SETUP_FAILED);
	CHECK(NativeArcadeRosterProof_FinalResult(NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED, NULL) == NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_FAILED);
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
	CHECK(NativeArcadeRosterProof_BuildConfig(&identity, UINT64_C(9), &expected) == 1);
	CHECK(memcmp(NativeArcadeRosterProof_Config(), &expected, sizeof(expected)) == 0);
	CHECK((NativeArcadeRosterProof_Dwell() == 12u) && (NativeArcadeRosterProof_Seed() == UINT64_C(9)));
	CHECK(strcmp(NativeArcadeRosterProof_LogPath(), "proof_report.txt") == 0);

	/* The report formatter. */
	memset(&report, 0, sizeof(report));
	report.result = NATIVE_ARCADE_ROSTER_PROOF_PASS;
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
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(length == strlen(text));
	{
		static const char head[] = "arcade roster proof v4\ndrivers digest excludes physics\nresult PASS (0)\n"
		                           "setup status VALIDATED (4)\nsetup failure NONE (0)\n";

		CHECK(strncmp(text, head, sizeof(head) - 1u) == 0);
	}
	CHECK(strstr(text, "seed 0x0123456789ABCDEF\ndwell 45\nticks 900\nmenu ready tick 230\ndemo race tick 1200\nlaunch tick 1275\n"
	                   "launch window demo race\nvalidated tick none\nrace tick 0 tick none\n") != NULL);
	CHECK(strstr(text, "config digest 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n") != NULL);
	CHECK(strstr(text, "slot 0 role CAB1_HUMAN character 0 difficulty 0x00 spawn 0 nav 0 accel 0\n") != NULL);
	CHECK(strstr(text, "slot 2 role BOT character 6 difficulty 0xA0 spawn 2 nav 1 accel 3\n") != NULL);
	CHECK(strstr(text, "slot 7 role INACTIVE\n") != NULL);
	CHECK(strstr(text, "bank digest 0000000000000000000000000000000000000000000000000000000000000000\n"
	                   "seeded randomNumber 0x7D2E advRng0 0x60C79386 advRng1 0x78DFDBA8 psxRand 0x1472E10B audioRNG 0x75599A57 match 1\n"
	                   "slot 0 ") != NULL);
	report.seedMatch = 0u;
	CHECK(NativeArcadeRosterProof_FormatReport(&report, text, sizeof(text), &length) == 1);
	CHECK(strstr(text, "audioRNG 0x75599A57 match 0\n") != NULL);
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
	CHECK(strstr(text, "menu ready tick 230\ndemo race tick none\nlaunch tick none\nlaunch window none\n") != NULL);
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
	CHECK(TestConfigBuilder() == 0);
	CHECK(TestSeedsAndFinalResult() == 0);
	CHECK(TestScriptedPads() == 0);
	CHECK(TestTickLines() == 0);
	CHECK(TestSingletonAndReport() == 0);
	puts("native_arcade_roster_proof_test: ok");
	return 0;
}
