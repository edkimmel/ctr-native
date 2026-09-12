#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H

#include "platform/native_canonical_state.h"
#include "platform/native_canonical_state_v3.h"

enum NativeReplaySchedulerCanonicalKind
{
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3
};

/* Tagged end-frame input prevents a v1 caller from being silently accepted
 * by a v3 transport (or vice versa).  Each union arm is a typed pointer. */
struct NativeReplaySchedulerCanonicalRequest
{
	enum NativeReplaySchedulerCanonicalKind kind;
	union
	{
		const struct NativeCanonicalStateV1 *v1;
		const struct NativeCanonicalStateV3 *v3;
	} state;
};

/* The scheduler owns transport; this seam only states which modes must supply
 * an already-projected, complete canonical value at EndFrame. */
enum NativeReplaySchedulerCanonicalMode
{
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V2,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V2,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V2,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V3,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V3,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V3
};

enum NativeReplaySchedulerSelector
{
	NATIVE_REPLAY_SCHEDULER_SELECTOR_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V1,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V2,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V3,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V3
};

struct NativeReplaySchedulerArgs
{
	enum NativeReplaySchedulerSelector selector;
	const char *replayPath;
	int toggle;
	int detailed;
	int bypassHeaderIdentity;
};

/* Pure parser: it neither opens files nor requests deterministic identity. */
int NativeReplayScheduler_ParseArgs(int argc, char **argv, struct NativeReplaySchedulerArgs *args);

int NativeReplayScheduler_ModeRequiresCanonicalState(enum NativeReplaySchedulerCanonicalMode mode);

/* Validates the exact expected replay frame and value-copies the complete record before EndFrame proceeds.
 * A mode that does not require canonical state accepts NULL and copies nothing. */
int NativeReplayScheduler_CopyCanonicalEndState(int required, uint32_t expectedReplayFrame, const struct NativeCanonicalStateV1 *source,
                                                struct NativeCanonicalStateV1 *destination);

/* V3 is deliberately a separately typed gate: it checks the compatibility
 * identity before copying, validates DRIVERS, recomputes every domain digest,
 * and commits the destination only on success. */
int NativeReplayScheduler_CopyCanonicalEndStateV3(uint32_t expectedReplayFrame, const struct NativeIdentityV1 *expectedIdentity,
	                                               const struct NativeCanonicalStateV3 *source, struct NativeCanonicalStateV3 *destination);

/* Small value-only lifecycle gates used by the live scheduler and exercised
 * headlessly: playback retains each supplied packet, and a poisoned record
 * never finalizes its provisional replay header. */
int NativeReplayScheduler_CopyConsumedV2VSyncPacket(uint16_t *packets, uint32_t capacity, uint32_t index, uint16_t packet);
int NativeReplayScheduler_V2BeginObservationNeedsValidation(int playbackV2, int pending);
int NativeReplayScheduler_V2RecordMayFinalize(int poisoned, int checkpointClosed);

#endif
