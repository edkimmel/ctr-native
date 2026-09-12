#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H

#include "platform/native_canonical_state.h"

/* Current scheduler modes are all v1/normal.  The test-only future mode
 * documents the required completeness gate without enabling v2 transport. */
enum NativeReplaySchedulerCanonicalMode
{
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_FUTURE_V2_TEST
};

int NativeReplayScheduler_ModeRequiresCanonicalState(enum NativeReplaySchedulerCanonicalMode mode);

/* Validates and value-copies the complete record before EndFrame proceeds.
 * A mode that does not require canonical state accepts NULL and copies nothing. */
int NativeReplayScheduler_CopyCanonicalEndState(int required, const struct NativeCanonicalStateV1 *source,
                                                struct NativeCanonicalStateV1 *destination);

#endif
