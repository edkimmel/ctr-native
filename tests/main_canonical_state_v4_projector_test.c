#include "platform/native_canonical_projector.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)

struct Fixture
{
	struct NativeMatchConfigV1 config;
	struct MainCanonicalStateV4Context context;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 retailRng;
	struct NativeDeterministicRngBankV1 deterministicRng;
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalDriversV1 drivers;
	struct NativeCanonicalWorldCountersV1 counters;
	struct NativeCanonicalWorldMineRegistryV1 mines;
	struct NativeCanonicalTopologyV1 topology;
};

static int InitTopology(struct NativeCanonicalTopologyV1 *topology)
{
	struct NativeCanonicalTopologyV1Input input;
	uint8_t quad[1] = {0};
	uint8_t restart[12] = {0xff, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff};
	uint8_t nav[3][68] = {{0}};
	memset(&input, 0, sizeof(input));
	input.flags = NATIVE_CANONICAL_TOPOLOGY_FLAG_AVAILABLE;
	input.levelID = -7; input.quadCount = 1; input.restartCount = 1;
	input.quadCheckpoints = quad; input.quadCheckpointSize = sizeof(quad);
	input.restartStream = restart; input.restartStreamSize = sizeof(restart);
	for (uint32_t i = 0; i < 3; ++i) { input.navStreams[i] = nav[i]; input.navStreamSizes[i] = sizeof(nav[i]); }
	return NativeCanonicalTopologyV1_FromNormativeStreams(topology, &input);
}

static int InitFixture(struct Fixture *f)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	NativeMatchConfigV1_InitArcadeTwoCab(&f->config);
	f->config.trackID = 7; f->config.gameMode1 = 2; f->config.gameMode2 = 3; f->config.rules = 4;
	f->config.lapCount = 3; f->config.tickRateNumerator = 30; f->config.tickRateDenominator = 1;
	f->config.masterSeed = UINT64_C(0x123456789abcdef0);
	for (uint32_t i = 0; i < 32; ++i) {
		f->config.buildIdentity[i] = (uint8_t)(0x10u + i);
		f->config.contentIdentity[i] = (uint8_t)(0x80u + i);
		f->config.botRulesDigest[i] = (uint8_t)(0x40u + i);
	}
	for (uint32_t i = 0; i < 6; ++i) { f->config.slots[i].characterID = (uint8_t)i; f->config.slots[i].difficulty = 2; }
	if (!NativeMatchConfigV1_Validate(&f->config) || !MainCanonicalStateV4Context_Init(&f->context, &f->config)) return 0;
	memcpy(f->identity.build, f->config.buildIdentity, 32); memcpy(f->identity.content, f->config.contentIdentity, 32);
	memset(&f->control, 0, sizeof(f->control)); f->control.levelID = -9; f->control.gameMode1 = 99; /* no control-track gate */
	memset(&f->retailRng, 0, sizeof(f->retailRng)); f->retailRng.advRng1 = 17;
	memset(&f->input, 0, sizeof(f->input)); f->input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT; f->input.pads[0].connected = 1;
	stream[64] = 1;
	return NativeDeterministicRngBankV1_Init(&f->deterministicRng, f->config.masterSeed, f->config.rngDerivationVersion) &&
		NativeCanonicalDriversV1_FromNormativeStream(&f->drivers, 1, stream, sizeof(stream)) &&
		(NativeCanonicalWorldCountersV1_Init(&f->counters), NativeCanonicalWorldCountersV1_Validate(&f->counters)) &&
		(NativeCanonicalWorldMineRegistryV1_Init(&f->mines), NativeCanonicalWorldMineRegistryV1_Validate(&f->mines)) &&
		InitTopology(&f->topology) && NativeCanonicalTopologyV1_Validate(&f->topology);
}

static int Project(struct NativeCanonicalStateV4 *out, const struct Fixture *f)
{
	return MainCanonicalState_ProjectV4(out, &f->context, &f->identity, 41, &f->control, &f->retailRng,
		&f->deterministicRng, &f->input, &f->drivers, &f->counters, &f->mines, &f->topology);
}

static uint32_t ChangedDomains(const struct NativeCanonicalStateV4 *a, const struct NativeCanonicalStateV4 *b)
{
	uint32_t mask = 0;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; ++i) if (a->domainDigests[i] != b->domainDigests[i]) mask |= UINT32_C(1) << i;
	return mask;
}

int main(void)
{
	struct Fixture f, changed;
	struct NativeCanonicalStateV4 state, baseline, sentinel, decoded;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	uint8_t bytes[1432];

	memset(&f, 0, sizeof(f)); CHECK(InitFixture(&f)); CHECK(Project(&state, &f)); baseline = state;
	CHECK(memcmp(state.configDigest, f.context.configDigest, 32) == 0);
	CHECK(state.frameNumber == 41 && state.control.levelID == -9 && state.control.gameMode1 == 99);
	/* frame is metadata, not a canonical-domain value. */
	CHECK(MainCanonicalState_ProjectV4(&state, &f.context, &f.identity, 42, &f.control, &f.retailRng, &f.deterministicRng, &f.input, &f.drivers, &f.counters, &f.mines, &f.topology));
	CHECK(state.frameNumber == 42 && ChangedDomains(&baseline, &state) == 0 && baseline.combinedDigest == state.combinedDigest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL); CHECK(NativeCanonicalStateV4_Encode(&writer, &state));
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes)); CHECK(NativeCanonicalStateV4_Decode(&reader, &f.identity, f.context.configDigest, &decoded));
	CHECK(reader.offset == sizeof(bytes) && memcmp(&state, &decoded, sizeof(state)) == 0);

	changed = f; changed.control.frameCounter++; CHECK(Project(&state, &changed)); CHECK(ChangedDomains(&baseline, &state) == 1);
	changed = f; changed.retailRng.advRng0++; CHECK(Project(&state, &changed)); CHECK(ChangedDomains(&baseline, &state) == 2);
	changed = f; changed.input.pads[0].buttons[0]++; CHECK(Project(&state, &changed)); CHECK(ChangedDomains(&baseline, &state) == 4);
	changed = f; changed.drivers.fullStreamDigest++; CHECK(Project(&state, &changed)); CHECK(ChangedDomains(&baseline, &state) == 8);
	changed = f; changed.counters.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE; changed.counters.activeBombMissileCount = 1;
	CHECK(Project(&state, &changed)); CHECK(ChangedDomains(&baseline, &state) == 16);
	changed = f; changed.topology.fullStreamDigest++; CHECK(Project(&state, &changed)); CHECK(ChangedDomains(&baseline, &state) == 32);

	/* Context, config identity, RNG bindings, and each rejected argument are atomic. */
	sentinel = baseline; changed = f; changed.context.config.masterSeed++; CHECK(!Project(&sentinel, &changed)); CHECK(memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	changed = f; changed.identity.build[0]++; CHECK(!Project(&sentinel, &changed)); CHECK(memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	changed = f; changed.deterministicRng.masterSeed++; CHECK(!Project(&sentinel, &changed)); CHECK(memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	changed = f; changed.input.padCount--; CHECK(!Project(&sentinel, &changed)); CHECK(memcmp(&sentinel, &baseline, sizeof(sentinel)) == 0);
	CHECK(!MainCanonicalState_ProjectV4(NULL, &f.context, &f.identity, 1, &f.control, &f.retailRng, &f.deterministicRng, &f.input, &f.drivers, &f.counters, &f.mines, &f.topology));
	CHECK(!MainCanonicalState_ProjectV4(&sentinel, NULL, &f.identity, 1, &f.control, &f.retailRng, &f.deterministicRng, &f.input, &f.drivers, &f.counters, &f.mines, &f.topology));
	CHECK(!MainCanonicalStateV4Context_Init(NULL, &f.config));
	changed.context = f.context; changed.context.config.canonicalSchemaVersion--; CHECK(!MainCanonicalStateV4Context_Init(&changed.context, &changed.context.config));
	puts("main_canonical_state_v4_projector_test: passed");
	return 0;
}
