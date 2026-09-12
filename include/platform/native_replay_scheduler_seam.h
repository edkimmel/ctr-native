#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H

#include "platform/native_canonical_state.h"

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
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V2
};

enum NativeReplaySchedulerSelector
{
	NATIVE_REPLAY_SCHEDULER_SELECTOR_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V1,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V2
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

#endif
