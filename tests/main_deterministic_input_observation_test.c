#include "MAIN/MainDeterministicInputObservation.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\\n", __FILE__, __LINE__, #expression);                                      \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static void FillSnapshots(struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT])
{
	memset(snapshots, 0, sizeof(struct PlatformInputPadSnapshot) * PLATFORM_INPUT_PAD_COUNT);
	for (uint32_t pad = 0; pad < PLATFORM_INPUT_PAD_COUNT; pad++)
	{
		snapshots[pad].status = (uint8_t)(0x10u + pad);
		snapshots[pad].id = (uint8_t)(0x20u + pad);
		snapshots[pad].buttons[0] = (uint8_t)(0x30u + pad);
		snapshots[pad].buttons[1] = (uint8_t)(0x40u + pad);
		for (uint32_t axis = 0; axis < 4u; axis++) snapshots[pad].analog[axis] = (uint8_t)(0x50u + (pad * 4u) + axis);
		snapshots[pad].connected = (uint8_t)(pad != 2u);
		memset(snapshots[pad].reserved, (int)(0x90u + pad), sizeof(snapshots[pad].reserved));
	}
}

static int TestExactCanonicalEquivalence(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct MainDeterministicInputObservationV1 observation;
	struct MainDeterministicInputObservationV1 reservedChanged;
	struct NativeCanonicalInputV1 expected;

	FillSnapshots(snapshots);
	CHECK(MainCanonicalState_FreezeInputV1(&expected, snapshots, PLATFORM_INPUT_PAD_COUNT));
	CHECK(MainDeterministicInputObservationV1_FromSnapshots(&observation, UINT64_C(0x8badf00d12345678), snapshots,
		PLATFORM_INPUT_PAD_COUNT));
	CHECK(MainDeterministicInputObservationV1_Validate(&observation));
	CHECK(observation.version == MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_VERSION);
	CHECK(observation.provenance == MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_PROVENANCE_POST_GAMEPAD_PROCESS_RAW_INGRESS);
	CHECK(observation.opaqueIteration == UINT64_C(0x8badf00d12345678));
	CHECK(memcmp(&observation.input, &expected, sizeof(expected)) == 0);

	for (uint32_t pad = 0; pad < PLATFORM_INPUT_PAD_COUNT; pad++)
		memset(snapshots[pad].reserved, 0xff, sizeof(snapshots[pad].reserved));
	CHECK(MainDeterministicInputObservationV1_FromSnapshots(&reservedChanged, observation.opaqueIteration, snapshots,
		PLATFORM_INPUT_PAD_COUNT));
	CHECK(memcmp(&reservedChanged, &observation, sizeof(observation)) == 0);
	return 0;
}

static int TestTransactionalRejection(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct MainDeterministicInputObservationV1 output;
	struct MainDeterministicInputObservationV1 before;
	union
	{
		struct MainDeterministicInputObservationV1 output;
		struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	} overlap;
	unsigned char overlapBefore[sizeof(overlap)];

	FillSnapshots(snapshots);
	memset(&output, 0xa5, sizeof(output));
	before = output;
	CHECK(!MainDeterministicInputObservationV1_FromSnapshots(NULL, 1u, snapshots, PLATFORM_INPUT_PAD_COUNT));
	CHECK(!MainDeterministicInputObservationV1_FromSnapshots(&output, 1u, NULL, PLATFORM_INPUT_PAD_COUNT));
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	CHECK(!MainDeterministicInputObservationV1_FromSnapshots(&output, 1u, snapshots, PLATFORM_INPUT_PAD_COUNT - 1u));
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	CHECK(!MainDeterministicInputObservationV1_FromSnapshots(&output, 1u, snapshots, PLATFORM_INPUT_PAD_COUNT + 1u));
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);

	memset(&overlap, 0x3c, sizeof(overlap));
	memcpy(overlapBefore, &overlap, sizeof(overlap));
	CHECK(!MainDeterministicInputObservationV1_FromSnapshots(&overlap.output, 1u, overlap.snapshots, PLATFORM_INPUT_PAD_COUNT));
	CHECK(memcmp(&overlap, overlapBefore, sizeof(overlap)) == 0);
	return 0;
}

static int TestValidationShape(void)
{
	struct PlatformInputPadSnapshot snapshots[PLATFORM_INPUT_PAD_COUNT];
	struct MainDeterministicInputObservationV1 observation;

	FillSnapshots(snapshots);
	CHECK(MainDeterministicInputObservationV1_FromSnapshots(&observation, 0u, snapshots, PLATFORM_INPUT_PAD_COUNT));
	observation.version++;
	CHECK(!MainDeterministicInputObservationV1_Validate(&observation));
	observation.version--;
	observation.provenance++;
	CHECK(!MainDeterministicInputObservationV1_Validate(&observation));
	observation.provenance--;
	observation.input.padCount--;
	CHECK(!MainDeterministicInputObservationV1_Validate(&observation));
	return 0;
}

int main(void)
{
	if (TestExactCanonicalEquivalence()) return 1;
	if (TestTransactionalRejection()) return 1;
	if (TestValidationShape()) return 1;
	return 0;
}
