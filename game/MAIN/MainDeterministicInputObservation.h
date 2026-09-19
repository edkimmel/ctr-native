#ifndef MAIN_DETERMINISTIC_INPUT_OBSERVATION_H
#define MAIN_DETERMINISTIC_INPUT_OBSERVATION_H

#include "platform/native_canonical_projector.h"

/*
 * A source-only value record for four already-captured raw controller
 * snapshots.  POST_GAMEPAD_PROCESS_RAW_INGRESS describes where a future
 * caller observed these bytes: after GAMEPAD_ProcessAnyoneVars has consumed
 * the same raw ingress.  It is provenance only.  This adapter neither reads
 * controller-derived state nor establishes a simulation or network tick.
 *
 * opaqueIteration is supplied and interpreted solely by the caller.  In
 * particular it is not a time, VBlank count, RNG value, or derived game
 * counter.  This record has no serializer and no runtime hook.
 */
#define MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_VERSION UINT32_C(1)
#define MAIN_DETERMINISTIC_INPUT_OBSERVATION_V1_PROVENANCE_POST_GAMEPAD_PROCESS_RAW_INGRESS UINT32_C(1)

struct MainDeterministicInputObservationV1
{
	uint32_t version;
	uint32_t provenance;
	uint64_t opaqueIteration;
	struct NativeCanonicalInputV1 input;
};

/*
 * Transactional raw-ingress observation.  snapshots must address exactly
 * PLATFORM_INPUT_PAD_COUNT (four) caller-owned snapshot values and must not
 * overlap output.  Failure leaves output unchanged.  No pointer, clock,
 * VBlank, RNG, or controller-derived game value is recorded.
 */
int MainDeterministicInputObservationV1_FromSnapshots(
	struct MainDeterministicInputObservationV1 *output, uint64_t opaqueIteration,
	const struct PlatformInputPadSnapshot *snapshots, uint32_t count);

/* Validates the record shape; every canonical raw pad-byte value is allowed. */
int MainDeterministicInputObservationV1_Validate(const struct MainDeterministicInputObservationV1 *observation);

#endif
