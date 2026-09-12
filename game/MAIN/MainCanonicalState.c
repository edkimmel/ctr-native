#include "platform/native_canonical_projector.h"

#include <string.h>

int MainCanonicalState_FreezeInputV1(struct NativeCanonicalInputV1 *input,
                                     const struct PlatformInputPadSnapshot *snapshots, uint32_t count)
{
	struct NativeCanonicalInputV1 candidate;

	if ((input == NULL) || (snapshots == NULL) || (count != NATIVE_CANONICAL_INPUT_PAD_COUNT))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	candidate.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_INPUT_PAD_COUNT; i++)
	{
		candidate.pads[i].status = snapshots[i].status;
		candidate.pads[i].id = snapshots[i].id;
		candidate.pads[i].buttons[0] = snapshots[i].buttons[0];
		candidate.pads[i].buttons[1] = snapshots[i].buttons[1];
		candidate.pads[i].analog[0] = snapshots[i].analog[0];
		candidate.pads[i].analog[1] = snapshots[i].analog[1];
		candidate.pads[i].analog[2] = snapshots[i].analog[2];
		candidate.pads[i].analog[3] = snapshots[i].analog[3];
		candidate.pads[i].connected = snapshots[i].connected;
	}

	*input = candidate;
	return 1;
}

int MainCanonicalState_ProjectV1(struct NativeCanonicalStateV1 *state, const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
                                 const struct NativeCanonicalControlV1 *control, const struct NativeCanonicalRngV1 *rng,
                                 const struct NativeCanonicalInputV1 *input)
{
	struct NativeCanonicalStateV1 candidate;

	if ((state == NULL) || (identity == NULL) || (control == NULL) || (rng == NULL) || (input == NULL) ||
	    (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT))
	{
		return 0;
	}

	NativeCanonicalStateV1_Init(&candidate);
	candidate.frameNumber = replayFrameNumber;
	memcpy(candidate.identity.build, identity->build, NATIVE_IDENTITY_DIGEST_BYTES);
	memcpy(candidate.identity.content, identity->content, NATIVE_IDENTITY_DIGEST_BYTES);
	candidate.control.frameTimer = control->frameTimer;
	candidate.control.frameCounter = control->frameCounter;
	candidate.control.timer = control->timer;
	candidate.control.framesInThisLEV = control->framesInThisLEV;
	candidate.control.elapsedTimeMS = control->elapsedTimeMS;
	candidate.control.msInThisLEV = control->msInThisLEV;
	candidate.control.elapsedEventTime = control->elapsedEventTime;
	candidate.control.mainGameState = control->mainGameState;
	candidate.control.loadingStage = control->loadingStage;
	candidate.control.levelID = control->levelID;
	candidate.control.gameMode1 = control->gameMode1;
	candidate.control.gameMode2 = control->gameMode2;
	candidate.rng.mixRandomNumber = rng->mixRandomNumber;
	candidate.rng.deadcoed0 = rng->deadcoed0;
	candidate.rng.deadcoed1 = rng->deadcoed1;
	candidate.rng.advRng0 = rng->advRng0;
	candidate.rng.advRng1 = rng->advRng1;
	candidate.input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_INPUT_PAD_COUNT; i++)
	{
		candidate.input.pads[i].status = input->pads[i].status;
		candidate.input.pads[i].id = input->pads[i].id;
		candidate.input.pads[i].buttons[0] = input->pads[i].buttons[0];
		candidate.input.pads[i].buttons[1] = input->pads[i].buttons[1];
		candidate.input.pads[i].analog[0] = input->pads[i].analog[0];
		candidate.input.pads[i].analog[1] = input->pads[i].analog[1];
		candidate.input.pads[i].analog[2] = input->pads[i].analog[2];
		candidate.input.pads[i].analog[3] = input->pads[i].analog[3];
		candidate.input.pads[i].connected = input->pads[i].connected;
	}

	if (!NativeCanonicalStateV1_ComputeDigests(&candidate))
	{
		return 0;
	}
	*state = candidate;
	return 1;
}

int MainCanonicalState_ProjectV3(struct NativeCanonicalStateV3 *state, const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
                                 const struct NativeCanonicalControlV1 *control, const struct NativeCanonicalRngV1 *rng,
                                 const struct NativeCanonicalInputV1 *input, const struct NativeCanonicalDriversV1 *drivers)
{
	struct NativeCanonicalStateV3 candidate;

	if ((state == NULL) || (identity == NULL) || (control == NULL) || (rng == NULL) || (input == NULL) || (drivers == NULL) ||
	    (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT) || !NativeCanonicalDriversV1_Validate(drivers))
	{
		return 0;
	}

	NativeCanonicalStateV3_Init(&candidate);
	candidate.frameNumber = replayFrameNumber;
	memcpy(candidate.identity.build, identity->build, NATIVE_IDENTITY_DIGEST_BYTES);
	memcpy(candidate.identity.content, identity->content, NATIVE_IDENTITY_DIGEST_BYTES);
	candidate.control.frameTimer = control->frameTimer;
	candidate.control.frameCounter = control->frameCounter;
	candidate.control.timer = control->timer;
	candidate.control.framesInThisLEV = control->framesInThisLEV;
	candidate.control.elapsedTimeMS = control->elapsedTimeMS;
	candidate.control.msInThisLEV = control->msInThisLEV;
	candidate.control.elapsedEventTime = control->elapsedEventTime;
	candidate.control.mainGameState = control->mainGameState;
	candidate.control.loadingStage = control->loadingStage;
	candidate.control.levelID = control->levelID;
	candidate.control.gameMode1 = control->gameMode1;
	candidate.control.gameMode2 = control->gameMode2;
	candidate.rng.mixRandomNumber = rng->mixRandomNumber;
	candidate.rng.deadcoed0 = rng->deadcoed0;
	candidate.rng.deadcoed1 = rng->deadcoed1;
	candidate.rng.advRng0 = rng->advRng0;
	candidate.rng.advRng1 = rng->advRng1;
	candidate.input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_INPUT_PAD_COUNT; i++)
	{
		candidate.input.pads[i].status = input->pads[i].status;
		candidate.input.pads[i].id = input->pads[i].id;
		candidate.input.pads[i].buttons[0] = input->pads[i].buttons[0];
		candidate.input.pads[i].buttons[1] = input->pads[i].buttons[1];
		candidate.input.pads[i].analog[0] = input->pads[i].analog[0];
		candidate.input.pads[i].analog[1] = input->pads[i].analog[1];
		candidate.input.pads[i].analog[2] = input->pads[i].analog[2];
		candidate.input.pads[i].analog[3] = input->pads[i].analog[3];
		candidate.input.pads[i].connected = input->pads[i].connected;
	}

	/* Copy the sealed summary as a value; detailed bytes stay upstream. */
	candidate.drivers = *drivers;
	if (!NativeCanonicalStateV3_ComputeDigests(&candidate)) return 0;
	*state = candidate;
	return 1;
}
