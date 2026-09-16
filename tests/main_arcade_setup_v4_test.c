#include "MAIN/MainArcadeSetupV4.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
#define CHECK_PROJECT_FAILURE(x) do { \
	sentinel = baseline; \
	CHECK(!(x)); \
	CHECK(memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0); \
} while (0)

struct Fixture {
	struct NativeMatchConfigV1 config;
	struct MainArcadeRosterNativeFacts rosterFacts;
	struct MainArcadeBotSetupSourceFacts setupFacts;
	struct NativeDeterministicRngBankV1 rng;
	struct MainArcadeSetupV4Context context;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 retail;
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalDriversV1 drivers;
	struct NativeCanonicalWorldCountersV1 counters;
	struct NativeCanonicalWorldMineRegistryV1 mines;
	struct NativeCanonicalTopologyV1 topology;
};

static void Facts(const struct NativeMatchConfigV1 *c, struct MainArcadeRosterNativeFacts *r,
	struct MainArcadeBotSetupSourceFacts *s, int reverse)
{
	uint8_t next = 0;
	memset(r, 0, sizeof(*r)); memset(s, 0, sizeof(*s));
	memset(r->rosterInput.raceOrder, 0xff, 8); memset(r->rosterInput.winnerDriverIDs, 0xff, 4);
	memset(r->rosterInput.ranks, 0xff, 8); memset(r->rosterInput.navOrder, 0xff, 24); memset(r->nativeDriverSlots, 0xff, 8);
	r->rosterInput.numLaps = (int8_t)c->lapCount; s->factCount = 8;
	for (uint8_t slot = 0; slot < 8; ++slot) {
		const struct NativeMatchConfigSlotV1 *cfg = &c->slots[slot];
		struct MainArcadeBotSetupSourceSlot *f = &s->facts[reverse ? 7 - slot : slot];
		f->stableSlot = slot;
		if (cfg->role == NATIVE_MATCH_SLOT_ROLE_INACTIVE) { f->nativeDriverSlot = f->spawnOrder = f->navPathIndex = f->accelerationOrder = 0xff; continue; }
		r->rosterInput.slots[slot].present = 1; r->rosterInput.slots[slot].driverID = slot;
		r->rosterInput.slots[slot].kind = cfg->role == NATIVE_MATCH_SLOT_ROLE_BOT ? NATIVE_CANONICAL_DRIVER_KIND_BOT : NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
		r->rosterInput.slots[slot].behaviorID = cfg->role == NATIVE_MATCH_SLOT_ROLE_BOT ? 1 : 0;
		r->rosterInput.slots[slot].threadBehaviorID = cfg->role == NATIVE_MATCH_SLOT_ROLE_BOT ? NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE : NATIVE_CANONICAL_DRIVER_THREAD_NULL;
		r->rosterInput.raceOrder[next] = slot; r->nativeDriverSlots[next] = slot;
		r->slots[slot].present = 1; r->slots[slot].driverID = slot; r->slots[slot].role = cfg->role;
		r->slots[slot].initialLifecycle = cfg->initialLifecycle; r->slots[slot].characterID = cfg->characterID; r->slots[slot].difficulty = cfg->difficulty;
		if (cfg->role == NATIVE_MATCH_SLOT_ROLE_BOT) { r->rosterInput.navOrder[slot % 3][r->rosterInput.navCount[slot % 3]++] = slot; r->numBotsNextGame++; }
		else { r->rosterInput.ranks[r->numPlyrCurrGame] = r->numPlyrCurrGame; r->numPlyrCurrGame++; }
		f->present = 1; f->nativeDriverSlot = slot; f->role = cfg->role; f->characterID = cfg->characterID; f->difficulty = cfg->difficulty;
		f->spawnOrder = (uint8_t)((slot * 3) & 7); f->navPathIndex = (uint8_t)(slot % 3); f->accelerationOrder = (uint8_t)((slot * 5 + 1) & 7); next++;
	}
	r->nativeDriverCount = next; r->rosterInput.raceOrderCount = next; r->rosterInput.playerCount = r->numPlyrCurrGame;
}

static int Topology(struct NativeCanonicalTopologyV1 *out)
{
	struct NativeCanonicalTopologyV1Input in; uint8_t quad[1] = {0}, restart[12] = {0xff}, nav[3][68] = {{0}};
	memset(&in, 0, sizeof(in)); restart[1] = 0; restart[8] = restart[9] = restart[10] = restart[11] = 0xff;
	in.flags = NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE; in.levelID = 1; in.quadCount = 1; in.restartCount = 1;
	in.quadCheckpoints = quad; in.quadCheckpointSize = sizeof(quad); in.restartStream = restart; in.restartStreamSize = sizeof(restart);
	for (uint32_t i = 0; i < 3; ++i) { in.navStreams[i] = nav[i]; in.navStreamSizes[i] = sizeof(nav[i]); }
	return NativeCanonicalTopologyV1_FromNormativeStreams(out, &in);
}

static int Init(struct Fixture *f, uint32_t profile, int reverse)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0}; struct NativeCodecWriter writer;
	memset(f, 0, sizeof(*f));
	if (profile == NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB) NativeMatchConfigV1_InitArcadeTwoCab(&f->config); else NativeMatchConfigV1_InitArcadeOneCab(&f->config);
	f->config.trackID = 7; f->config.gameMode1 = 2; f->config.gameMode2 = 3; f->config.rules = 4; f->config.lapCount = 3;
	f->config.tickRateNumerator = 30; f->config.tickRateDenominator = 1; f->config.masterSeed = UINT64_C(0x123456789abcdef0);
	memset(f->config.buildIdentity, 0x11, 32); memset(f->config.contentIdentity, 0x22, 32); memset(f->config.botRulesDigest, 0x33, 32);
	for (uint8_t i = 0; i < 8; ++i) if (f->config.slots[i].role != NATIVE_MATCH_SLOT_ROLE_INACTIVE) { f->config.slots[i].characterID = i + 1; f->config.slots[i].difficulty = i + 20; }
	Facts(&f->config, &f->rosterFacts, &f->setupFacts, reverse);
	if (!NativeDeterministicRngBankV1_Init(&f->rng, f->config.masterSeed, f->config.rngDerivationVersion) ||
		!MainArcadeSetupV4Context_Init(&f->context, &f->config, &f->rosterFacts, &f->setupFacts, &f->rng)) return 0;
	NativeCodecWriter_Init(&writer, stream, sizeof(stream), NULL);
	if (!NativeCanonicalDriversPreludeV1_Encode(&writer, &f->context.validatedRoster.roster.prelude) || writer.offset != NATIVE_CANONICAL_DRIVERS_PRELUDE_BYTES ||
		!NativeCanonicalDriversV1_FromNormativeStream(&f->drivers, f->context.driversPresenceMask, stream, sizeof(stream))) return 0;
	memcpy(f->identity.build, f->config.buildIdentity, 32); memcpy(f->identity.content, f->config.contentIdentity, 32);
	f->input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT; f->input.pads[0].connected = 1;
	NativeCanonicalWorldCountersV1_Init(&f->counters); NativeCanonicalWorldMineRegistryV1_Init(&f->mines);
	return NativeCanonicalWorldCountersV1_Validate(&f->counters) && NativeCanonicalWorldMineRegistryV1_Validate(&f->mines) && Topology(&f->topology);
}

static int Project(struct NativeCanonicalStateV4 *out, const struct Fixture *f)
{
	return MainArcadeSetupV4_Project(out, &f->context, &f->identity, 12, &f->control, &f->retail, &f->input,
		&f->drivers, &f->counters, &f->mines, &f->topology);
}

int main(void)
{
	struct Fixture two, shuffled, one, changed; struct NativeCanonicalStateV4 baseline, sentinel; struct MainArcadeSetupV4Context contextSentinel;
	CHECK(Init(&two, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 0)); CHECK(Project(&baseline, &two));
	CHECK(Init(&shuffled, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB, 1)); CHECK(Project(&sentinel, &shuffled));
	CHECK(baseline.combinedDigest == sentinel.combinedDigest && baseline.drivers.presenceMask == sentinel.drivers.presenceMask);
	CHECK(Init(&one, NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_ONE_CAB, 0)); CHECK(Project(&sentinel, &one));
	/* Cross-binding, context tamper, null, and reserved data must all fail atomically. */
	sentinel = baseline; changed = two; changed.drivers.presenceMask ^= 1; CHECK(!Project(&sentinel, &changed) && memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	sentinel = baseline; changed = two; changed.drivers.rosterMetaDigest++; CHECK(!Project(&sentinel, &changed) && memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	sentinel = baseline; changed = two; changed.context.rosterPlan.botCount++; CHECK(!Project(&sentinel, &changed) && memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	sentinel = baseline; changed = two; changed.context.setupFacts.reserved[0] = 1; CHECK(!Project(&sentinel, &changed) && memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	sentinel = baseline; changed = two; changed.context.rngBefore.streams[0].drawCount++; CHECK(!Project(&sentinel, &changed) && memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	CHECK(!MainArcadeSetupV4Context_Init(NULL, &two.config, &two.rosterFacts, &two.setupFacts, &two.rng));
	contextSentinel = two.context; CHECK(!MainArcadeSetupV4Context_Init(&contextSentinel, NULL, &two.rosterFacts, &two.setupFacts, &two.rng));
	changed = two; changed.rosterFacts.slots[0].reserved[0] = 1; contextSentinel = two.context;
	CHECK(!MainArcadeSetupV4Context_Init(&contextSentinel, &changed.config, &changed.rosterFacts, &changed.setupFacts, &changed.rng) &&
		memcmp(&contextSentinel, &two.context, sizeof(contextSentinel)) == 0);
	CHECK(!MainArcadeSetupV4_Project(NULL, &two.context, &two.identity, 1, &two.control, &two.retail, &two.input, &two.drivers, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, NULL, &two.identity, 1, &two.control, &two.retail, &two.input, &two.drivers, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, NULL, 1, &two.control, &two.retail, &two.input, &two.drivers, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, NULL, &two.retail, &two.input, &two.drivers, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, &two.control, NULL, &two.input, &two.drivers, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, &two.control, &two.retail, NULL, &two.drivers, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, &two.control, &two.retail, &two.input, NULL, &two.counters, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, &two.control, &two.retail, &two.input, &two.drivers, NULL, &two.mines, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, &two.control, &two.retail, &two.input, &two.drivers, &two.counters, NULL, &two.topology));
	CHECK_PROJECT_FAILURE(MainArcadeSetupV4_Project(&sentinel, &two.context, &two.identity, 1, &two.control, &two.retail, &two.input, &two.drivers, &two.counters, &two.mines, NULL));
	puts("main_arcade_setup_v4_test: passed"); return 0;
}
