#include "platform/native_canonical_projector.h"

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

	control.frameTimer = -1;
	control.frameCounter = 2;
	control.timer = -3;
	control.framesInThisLEV = 4;
	control.elapsedTimeMS = 32;
	control.msInThisLEV = 64;
	control.elapsedEventTime = 96;
	control.mainGameState = 7;
	control.loadingStage = 8;
	control.levelID = -9;
	control.gameMode1 = 0x1020;
	control.gameMode2 = -0x3040;
	rng.mixRandomNumber = UINT32_C(0x11223344);
	rng.deadcoed0 = UINT32_C(0x55667788);
	rng.deadcoed1 = UINT32_C(0x99aabbcc);
	rng.advRng0 = UINT32_C(0xddeeff00);
	rng.advRng1 = UINT32_C(0x01234567);
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
	if ((TestFreezeAndProjection() != 0) || (TestTransactions() != 0)) return 1;
	puts("native_canonical_projector_test: passed");
	return 0;
}
