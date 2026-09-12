#include "platform/native_canonical_projector.h"
#include "platform/native_replay_v3.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                         \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static void FillIdentity(struct NativeIdentityV1 *identity)
{
	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		identity->build[i] = (uint8_t)(0x10u + i);
		identity->content[i] = (uint8_t)(0x90u + i);
	}
}

static void FillSnapshots(struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT])
{
	memset(snapshots, 0, sizeof(struct PlatformInputPadSnapshot) * PLATFORM_INPUT_PAD_COUNT);
	for (uint32_t i = 0; i < PLATFORM_INPUT_PAD_COUNT; i++)
	{
		snapshots[i].status = (uint8_t)(0x20u + i);
		snapshots[i].id = (uint8_t)(0x30u + i);
		snapshots[i].buttons[0] = (uint8_t)(0x40u + i);
		snapshots[i].buttons[1] = (uint8_t)(0x50u + i);
		for (uint32_t j = 0; j < 4; j++) snapshots[i].analog[j] = (uint8_t)(0x60u + (4u * i) + j);
		snapshots[i].connected = (uint8_t)(i != 2u);
		memset(snapshots[i].reserved, (int)(0xa0u + i), sizeof(snapshots[i].reserved));
	}
}

static void FillControlAndRng(struct NativeCanonicalControlV1 *control, struct NativeCanonicalRngV1 *rng)
{
	memset(control, 0, sizeof(*control));
	memset(rng, 0, sizeof(*rng));
	control->frameTimer = -1;
	control->frameCounter = 2;
	control->timer = -3;
	control->framesInThisLEV = 4;
	control->elapsedTimeMS = 32;
	control->msInThisLEV = 64;
	control->elapsedEventTime = 96;
	control->mainGameState = 7;
	control->loadingStage = 8;
	control->levelID = -9;
	control->gameMode1 = 0x1020;
	control->gameMode2 = -0x3040;
	rng->mixRandomNumber = UINT32_C(0x11223344);
	rng->deadcoed0 = UINT32_C(0x55667788);
	rng->deadcoed1 = UINT32_C(0x99aabbcc);
	rng->advRng0 = UINT32_C(0xddeeff00);
	rng->advRng1 = UINT32_C(0x01234567);
}

static int DriversEqual(const struct NativeCanonicalDriversV1 *left, const struct NativeCanonicalDriversV1 *right)
{
	if ((left->version != right->version) || (left->slotCount != right->slotCount) || (left->presenceMask != right->presenceMask) ||
	    (left->groupCount != right->groupCount) || (left->rosterMetaDigest != right->rosterMetaDigest) ||
	    (left->fullStreamDigest != right->fullStreamDigest)) return 0;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
		if ((left->slots[i].slotDigest != right->slots[i].slotDigest) ||
		    (left->slots[i].metaRaceDigest != right->slots[i].metaRaceDigest) ||
		    (left->slots[i].physicsDynamicsDigest != right->slots[i].physicsDynamicsDigest) ||
		    (left->slots[i].behaviorBotDigest != right->slots[i].behaviorBotDigest)) return 0;
	return 1;
}

static uint64_t DriversDigest(const struct NativeCanonicalDriversV1 *drivers)
{
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES];
	struct NativeCodecWriter writer;
	struct NativeCodecDigest64 digest;
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeCanonicalDriversV1_Encode(&writer, drivers)) return 0;
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, sizeof(bytes));
	return digest.value;
}

static uint64_t EmptyDigest(void)
{
	struct NativeCodecDigest64 digest;
	NativeCodecDigest64_Init(&digest);
	return digest.value;
}

static int DomainDigestsEqualExceptDrivers(const struct NativeCanonicalStateV3 *left, const struct NativeCanonicalStateV3 *right)
{
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
		if ((i != (NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u)) && (left->domainDigests[i] != right->domainDigests[i])) return 0;
	return 1;
}

static int TestFreezeAndProjection(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct NativeCanonicalInputV1 frozen;
	struct NativeCanonicalInputV1 frozenDifferentReserved;
	struct NativeCanonicalControlV1 control = {0};
	struct NativeCanonicalRngV1 rng = {0};
	struct NativeIdentityV1 identity;
	struct NativeCanonicalStateV1 state;
	struct NativeCanonicalStateV1 sameState;

	FillSnapshots(snapshots);
	CHECK(MainCanonicalState_FreezeInputV1(&frozen, snapshots, PLATFORM_INPUT_PAD_COUNT));
	CHECK(frozen.padCount == NATIVE_CANONICAL_INPUT_PAD_COUNT);
	CHECK(frozen.pads[1].status == 0x21 && frozen.pads[1].buttons[1] == 0x51 && frozen.pads[1].analog[3] == 0x67 && frozen.pads[1].connected == 1);

	/* Reserved native transport bytes cannot affect frozen ingress state. */
	memset(snapshots[1].reserved, 0xff, sizeof(snapshots[1].reserved));
	CHECK(MainCanonicalState_FreezeInputV1(&frozenDifferentReserved, snapshots, PLATFORM_INPUT_PAD_COUNT));
	CHECK(memcmp(&frozen, &frozenDifferentReserved, sizeof(frozen)) == 0);

	FillControlAndRng(&control, &rng);
	FillIdentity(&identity);

	/* Replay indexing is owned by the scheduler and need not equal game time. */
	CHECK(MainCanonicalState_ProjectV1(&state, &identity, UINT32_C(0x40000007), &control, &rng, &frozen));
	CHECK(NativeCanonicalStateV1_Validate(&state));
	CHECK(state.frameNumber == UINT32_C(0x40000007) && state.control.gameMode1 == 0x1020 && state.control.gameMode2 == -0x3040);
	CHECK(state.rng.mixRandomNumber == UINT32_C(0x11223344) && state.rng.deadcoed1 == UINT32_C(0x99aabbcc) &&
	      state.rng.advRng1 == UINT32_C(0x01234567));
	CHECK(state.input.pads[3].id == 0x33 && state.input.pads[3].analog[2] == 0x6e);
	CHECK(MainCanonicalState_ProjectV1(&sameState, &identity, UINT32_C(0x40000007), &control, &rng, &frozenDifferentReserved));
	CHECK(state.domainDigests[NATIVE_CANONICAL_DOMAIN_INPUT - 1u] == sameState.domainDigests[NATIVE_CANONICAL_DOMAIN_INPUT - 1u]);
	CHECK(state.combinedDigest == sameState.combinedDigest);
	return 0;
}

static int TestProjectV3Drivers(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 rng;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalDriversV1 drivers;
	struct NativeCanonicalStateV3 state;
	struct NativeCanonicalStateV3 changed;
	struct NativeCanonicalStateV3 before;
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};

	for (uint32_t i = 0; i < sizeof(stream); i++) stream[i] = (uint8_t)(i * 29u + 7u);
	FillSnapshots(snapshots);
	CHECK(MainCanonicalState_FreezeInputV1(&input, snapshots, PLATFORM_INPUT_PAD_COUNT));
	FillControlAndRng(&control, &rng);
	FillIdentity(&identity);
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&drivers, UINT32_C(0xff), stream, sizeof(stream)));
	CHECK(MainCanonicalState_ProjectV3(&state, &identity, UINT32_C(0x70000003), &control, &rng, &input, &drivers));
	CHECK(NativeCanonicalStateV3_Validate(&state));
	CHECK(state.frameNumber == UINT32_C(0x70000003));
	CHECK(memcmp(state.identity.build, identity.build, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
	CHECK(memcmp(state.identity.content, identity.content, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
	CHECK(DriversEqual(&state.drivers, &drivers));
	CHECK(state.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] == DriversDigest(&drivers));
	CHECK(state.domainDigests[NATIVE_CANONICAL_DOMAIN_WORLD - 1u] == EmptyDigest());
	CHECK(state.domainDigests[NATIVE_CANONICAL_DOMAIN_TOPOLOGY - 1u] == EmptyDigest());

	/* Every mutable summary word is opaque to this bridge: only DRIVERS and
	 * the combined diagnostic digest can change. */
	{
		uint64_t *fields[34];
		uint32_t count = 0;
		fields[count++] = &drivers.rosterMetaDigest;
		for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
		{
			fields[count++] = &drivers.slots[i].slotDigest;
			fields[count++] = &drivers.slots[i].metaRaceDigest;
			fields[count++] = &drivers.slots[i].physicsDynamicsDigest;
			fields[count++] = &drivers.slots[i].behaviorBotDigest;
		}
		fields[count++] = &drivers.fullStreamDigest;
		CHECK(count == 34u);
		for (uint32_t i = 0; i < count; i++)
		{
			struct NativeCanonicalDriversV1 altered = drivers;
			uint64_t *alteredField = (uint64_t *)((uint8_t *)&altered + ((const uint8_t *)fields[i] - (const uint8_t *)&drivers));
			*alteredField ^= UINT64_C(0x0100000000000001);
			CHECK(NativeCanonicalDriversV1_Validate(&altered));
			CHECK(MainCanonicalState_ProjectV3(&changed, &identity, state.frameNumber, &control, &rng, &input, &altered));
			CHECK(DomainDigestsEqualExceptDrivers(&state, &changed));
			CHECK(changed.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] != state.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u]);
			CHECK(changed.combinedDigest != state.combinedDigest);
		}
	}

	/* The presence mask is the remaining mutable summary field.  A present
	 * slot may carry the fixed absent digest, so this remains a valid summary. */
	{
		struct NativeCanonicalDriversV1 altered;
		NativeCanonicalDriversV1_Init(&altered);
		altered.presenceMask = 1u;
		CHECK(NativeCanonicalDriversV1_Validate(&altered));
		CHECK(MainCanonicalState_ProjectV3(&changed, &identity, state.frameNumber, &control, &rng, &input, &altered));
		CHECK(DomainDigestsEqualExceptDrivers(&state, &changed));
		CHECK(changed.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] != state.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u]);
		CHECK(changed.combinedDigest != state.combinedDigest);
	}

	/* Identity and the scheduler-owned replay index gate compatibility but are
	 * intentionally outside all payload and combined hashes. */
	CHECK(MainCanonicalState_ProjectV3(&changed, &identity, state.frameNumber + 1u, &control, &rng, &input, &drivers));
	CHECK(memcmp(state.domainDigests, changed.domainDigests, sizeof(state.domainDigests)) == 0 && state.combinedDigest == changed.combinedDigest);
	identity.build[0] ^= 1u;
	CHECK(MainCanonicalState_ProjectV3(&changed, &identity, state.frameNumber, &control, &rng, &input, &drivers));
	CHECK(memcmp(state.domainDigests, changed.domainDigests, sizeof(state.domainDigests)) == 0 && state.combinedDigest == changed.combinedDigest);
	identity.build[0] ^= 1u;

	memset(&state, 0xa5, sizeof(state)); before = state;
	input.padCount = 3u;
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, &rng, &input, &drivers));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT;
	drivers.version--;
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, &rng, &input, &drivers));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	drivers.version++;
	drivers.slotCount--;
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, &rng, &input, &drivers));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	drivers.slotCount++;
	drivers.groupCount--;
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, &rng, &input, &drivers));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	drivers.groupCount++;
	CHECK(!MainCanonicalState_ProjectV3(NULL, &identity, 1u, &control, &rng, &input, &drivers));
	CHECK(!MainCanonicalState_ProjectV3(&state, NULL, 1u, &control, &rng, &input, &drivers));
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, NULL, &rng, &input, &drivers));
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, NULL, &input, &drivers));
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, &rng, NULL, &drivers));
	CHECK(!MainCanonicalState_ProjectV3(&state, &identity, 1u, &control, &rng, &input, NULL));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	return 0;
}

static int TestV3WireAndReplayRoundTrip(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalControlV1 control;
	struct NativeCanonicalRngV1 rng;
	struct NativeIdentityV1 identity;
	struct NativeCanonicalDriversV1 drivers;
	struct NativeCanonicalStateV3 state, decoded;
	struct NativeReplayV3Header header, decodedHeader;
	struct NativeReplayV3Frame frame, decodedFrame;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	uint8_t stateBytes[584];
	uint8_t replayBytes[140 + 904];

	FillSnapshots(snapshots); CHECK(MainCanonicalState_FreezeInputV1(&input, snapshots, PLATFORM_INPUT_PAD_COUNT));
	FillControlAndRng(&control, &rng); FillIdentity(&identity); stream[64] = 1u;
	CHECK(NativeCanonicalDriversV1_FromNormativeStream(&drivers, 1u, stream, sizeof(stream)));
	CHECK(MainCanonicalState_ProjectV3(&state, &identity, 12u, &control, &rng, &input, &drivers));
	CHECK(NativeCanonicalStateV3_EncodedSize() == sizeof(stateBytes));
	CHECK(NativeReplayV3Header_EncodedSize() == 140u && NativeReplayV3Frame_EncodedSize() == 904u);
	NativeCodecWriter_Init(&writer, stateBytes, sizeof(stateBytes), NULL);
	CHECK(NativeCanonicalStateV3_Encode(&writer, &state));
	NativeCodecReader_Init(&reader, stateBytes, sizeof(stateBytes));
	CHECK(NativeCanonicalStateV3_Decode(&reader, &identity, &decoded));
	CHECK(reader.offset == sizeof(stateBytes) && DriversEqual(&decoded.drivers, &drivers));

	NativeReplayV3Header_Init(&header); header.flags = NATIVE_REPLAY_V3_HEADER_FLAG_FINALIZED; header.frameCount = 1u; header.identity = identity;
	memset(&frame, 0, sizeof(frame)); frame.replayFrame = state.frameNumber; frame.padCount = NATIVE_REPLAY_V2_PAD_COUNT; frame.canonical = state;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_PAD_COUNT; i++)
	{
		frame.pads[i].status = input.pads[i].status; frame.pads[i].id = input.pads[i].id;
		frame.pads[i].buttons[0] = input.pads[i].buttons[0]; frame.pads[i].buttons[1] = input.pads[i].buttons[1];
		for (uint32_t j = 0; j < 4; j++) frame.pads[i].analog[j] = input.pads[i].analog[j];
		frame.pads[i].connected = input.pads[i].connected;
	}
	NativeCodecWriter_Init(&writer, replayBytes, sizeof(replayBytes), NULL);
	CHECK(NativeReplayV3Header_Encode(&writer, &header)); CHECK(NativeReplayV3Frame_Encode(&writer, &header, &frame));
	NativeCodecReader_Init(&reader, replayBytes, sizeof(replayBytes));
	CHECK(NativeReplayV3Header_Decode(&reader, &identity, &decodedHeader));
	CHECK(NativeReplayV3Frame_Decode(&reader, &decodedHeader, &identity, &decodedFrame));
	CHECK(reader.offset == sizeof(replayBytes) && DriversEqual(&decodedFrame.canonical.drivers, &drivers));
	/* The older sealed codec stays its independently tested 296-byte v1 record. */
	CHECK(NativeCanonicalStateV1_EncodedSize() == 296u);
	return 0;
}

static int TestTransactions(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct NativeCanonicalInputV1 input;
	struct NativeCanonicalInputV1 beforeInput;
	struct NativeCanonicalControlV1 control = {0};
	struct NativeCanonicalRngV1 rng = {0};
	struct NativeIdentityV1 identity;
	struct NativeCanonicalStateV1 state;
	struct NativeCanonicalStateV1 beforeState;

	FillSnapshots(snapshots);
	memset(&input, 0xa5, sizeof(input)); beforeInput = input;
	CHECK(!MainCanonicalState_FreezeInputV1(&input, snapshots, PLATFORM_INPUT_PAD_COUNT - 1));
	CHECK(memcmp(&input, &beforeInput, sizeof(input)) == 0);
	CHECK(MainCanonicalState_FreezeInputV1(&input, snapshots, PLATFORM_INPUT_PAD_COUNT));
	FillIdentity(&identity);
	memset(&state, 0xa5, sizeof(state)); beforeState = state;
	input.padCount--;
	CHECK(!MainCanonicalState_ProjectV1(&state, &identity, 1u, &control, &rng, &input));
	CHECK(memcmp(&state, &beforeState, sizeof(state)) == 0);
	return 0;
}

int main(void)
{
	if ((TestFreezeAndProjection() != 0) || (TestTransactions() != 0) || (TestProjectV3Drivers() != 0) ||
	    (TestV3WireAndReplayRoundTrip() != 0)) return 1;
	puts("native_canonical_projector_test: passed");
	return 0;
}
