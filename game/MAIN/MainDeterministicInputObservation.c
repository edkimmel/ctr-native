#include "MAIN/MainDeterministicInputObservation.h"

#include <stdint.h>

static int MainDeterministicInputObservationV1_RangesOverlap(const void *left, size_t leftSize,
	const void *right, size_t rightSize)
{
	uintptr_t leftAddress = (uintptr_t)left;
	uintptr_t rightAddress = (uintptr_t)right;

	if ((leftAddress > (UINTPTR_MAX - leftSize)) || (rightAddress > (UINTPTR_MAX - rightSize))) return 1;
	return (leftAddress < (rightAddress + rightSize)) && (rightAddress < (leftAddress + leftSize));
}

int MainDeterministicInputObservationV1_Validate(const struct MainDeterministicInputObservationV1 *observation)
{
	return (observation != NULL) &&
		(observation->version == MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_VERSION) &&
		(observation->provenance == MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_PROVENANCE_POST_GAMEPAD_PROCESS_RAW_INGRESS) &&
		(observation->input.padCount == NATIVE_CANONICAL_INPUT_PAD_COUNT);
}

int MainDeterministicInputObservationV1_FromSnapshots(
	struct MainDeterministicInputObservationV1 *output, uint64_t opaqueIteration,
	const struct PlatformInputPadSnapshot *snapshots, uint32_t count)
{
	struct MainDeterministicInputObservationV1 candidate = {0};

	if ((output == NULL) || (snapshots == NULL) || (count != PLATFORM_INPUT_PAD_COUNT) ||
		(PLATFORM_INPUT_PAD_COUNT != NATIVE_CANONICAL_INPUT_PAD_COUNT) ||
		MainDeterministicInputObservationV1_RangesOverlap(output, sizeof(*output), snapshots,
			sizeof(*snapshots) * PLATFORM_INPUT_PAD_COUNT))
	{
		return 0;
	}

	candidate.version = MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_VERSION;
	candidate.provenance = MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_PROVENANCE_POST_GAMEPAD_PROCESS_RAW_INGRESS;
	candidate.opaqueIteration = opaqueIteration;
	if (!MainCanonicalState_FreezeInputV1(&candidate.input, snapshots, count)) return 0;

	*output = candidate;
	return 1;
}
