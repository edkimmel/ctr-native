#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_V4_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_V4_H

/* Source-only CRV4 scheduler foundation.  It intentionally has no selector,
 * runtime, UI, or legacy scheduler dependency. */
#include "platform/native_replay_v4_file.h"

enum NativeReplaySchedulerV4Mode { NATIVE_REPLAY_SCHEDULER_V4_IDLE, NATIVE_REPLAY_SCHEDULER_V4_RECORD,
	NATIVE_REPLAY_SCHEDULER_V4_PLAYBACK, NATIVE_REPLAY_SCHEDULER_V4_MISMATCH, NATIVE_REPLAY_SCHEDULER_V4_POISON };
enum NativeReplaySchedulerV4MismatchMask { NATIVE_REPLAY_SCHEDULER_V4_MISMATCH_OBSERVATION=1u,
	NATIVE_REPLAY_SCHEDULER_V4_MISMATCH_PAD=2u, NATIVE_REPLAY_SCHEDULER_V4_MISMATCH_VSYNC=4u,
	NATIVE_REPLAY_SCHEDULER_V4_MISMATCH_CANONICAL_DOMAIN=8u, NATIVE_REPLAY_SCHEDULER_V4_MISMATCH_COMBINED=16u };

/* This request is deliberately a value, not a V1/V3 union. */
struct NativeReplaySchedulerV4Request { uint32_t replayFrame; struct NativeIdentityV1 identity;
	struct NativeMatchConfigV1 config; uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES]; };
struct NativeReplaySchedulerV4MismatchReport { uint32_t mask, canonicalDomainMask; uint32_t expectedFrame, liveFrame;
	uint32_t expectedVsyncCount, liveVsyncCount; };
struct NativeReplaySchedulerV4 {
	enum NativeReplaySchedulerV4Mode mode; int frameOpen, submitted, elapsedTaken, beginMismatch, vsyncOverflow;
	uint32_t vsyncConsumed;
	struct NativeReplaySchedulerV4Request request;
	struct NativeReplayV4Frame pending;
	struct NativeCanonicalStateV4 submittedCanonical;
	struct NativeReplayV4RecordSession record;
	struct NativeReplayV4PlaybackSession playback;
	struct NativeReplaySchedulerV4MismatchReport mismatch;
};

void NativeReplaySchedulerV4_Init(struct NativeReplaySchedulerV4 *scheduler);
int NativeReplaySchedulerV4_OpenRecord(struct NativeReplaySchedulerV4 *scheduler, const char *path,
	const struct NativeIdentityV1 *identity, const struct NativeMatchConfigV1 *config);
int NativeReplaySchedulerV4_OpenPlayback(struct NativeReplaySchedulerV4 *scheduler, const char *path,
	const struct NativeIdentityV1 *identity, const struct NativeMatchConfigV1 *config);
/* Begin copies all ingress values.  Playback retains the next CRF4 before it
 * accepts the request, so EOF and malformed records are terminal poison. */
int NativeReplaySchedulerV4_BeginFrame(struct NativeReplaySchedulerV4 *scheduler,
	const struct NativeReplaySchedulerV4Request *request, const struct NativeReplayV2FrameObservation *begin,
	const struct NativeReplayV2Pad pads[NATIVE_REPLAY_V2_PAD_COUNT]);
/* Record accepts one nonzero VSync packet. Playback returns exactly one saved
 * packet per call; a caller cannot consume the same packet twice. */
int NativeReplaySchedulerV4_ConsumeVSync(struct NativeReplaySchedulerV4 *scheduler, uint16_t *packet);
/* Playback exposes the saved end elapsed time once; record merely marks the
 * corresponding timing ingress consumed. */
int NativeReplaySchedulerV4_ConsumeElapsed(struct NativeReplaySchedulerV4 *scheduler, uint32_t *elapsedTimeMS);
/* Typed NCV4-only canonical submission. */
int NativeReplaySchedulerV4_SubmitCanonical(struct NativeReplaySchedulerV4 *scheduler,
	const struct NativeCanonicalStateV4 *canonical);
int NativeReplaySchedulerV4_EndFrame(struct NativeReplaySchedulerV4 *scheduler,
	const struct NativeReplayV2FrameObservation *end);
/* Only a healthy, frame-closed record can publish its provisional CRV4. */
int NativeReplaySchedulerV4_Finalize(struct NativeReplaySchedulerV4 *scheduler);
void NativeReplaySchedulerV4_Close(struct NativeReplaySchedulerV4 *scheduler);
const struct NativeReplaySchedulerV4MismatchReport *NativeReplaySchedulerV4_FirstMismatch(const struct NativeReplaySchedulerV4 *scheduler);

#endif
