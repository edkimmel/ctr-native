#ifndef PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H
#define PLATFORM_NATIVE_REPLAY_SCHEDULER_SEAM_H

#include "platform/native_canonical_state.h"
#include "platform/native_canonical_state_v3.h"
#include "platform/native_canonical_state_v4.h"
#include "platform/native_replay_v3.h"

enum NativeReplaySchedulerCanonicalKind
{
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V1,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V4
};

/* Pointer-bearing post-projection input prevents a v1 caller from being
 * silently accepted by a v3 transport (or vice versa). */
struct NativeReplaySchedulerCanonicalSubmission
{
	enum NativeReplaySchedulerCanonicalKind kind;
	union
	{
		const struct NativeCanonicalStateV1 *v1;
		const struct NativeCanonicalStateV3 *v3;
		const struct NativeCanonicalStateV4 *v4;
	} state;
};

/* Stateful, transport-neutral V3 frame lifecycle.  The production scheduler
 * uses this same tiny state machine; the dedicated temp-file harness therefore
 * exercises the conditions that decide whether a provisional replay can seal. */
struct NativeReplaySchedulerV3Lifecycle
{
	int beginOpen;
	int poisoned;
	uint32_t checkpointCount;
	int checkpointClosed;
};

/* Retained verbatim by V3 playback on the first divergence.  Every component
 * is independent so diagnostics remain useful when several observations differ. */
struct NativeReplaySchedulerV3MismatchReport
{
	int observationMismatch;
	/* Preserve the full values, not merely their comparison result, so an
	 * artifact can identify which observation changed without replaying it. */
	struct NativeReplayV2FrameObservation expectedObservation;
	struct NativeReplayV2FrameObservation liveObservation;
	int vsyncTotalMismatch;
	int vsyncPacketCountMismatch;
	int vsyncFirstPacketMismatch;
	uint32_t expectedVsyncTotal;
	uint32_t liveVsyncTotal;
	uint32_t expectedVsyncPacketCount;
	uint32_t liveVsyncPacketCount;
	uint32_t firstVsyncPacketIndex;
	uint16_t expectedVsyncPacket;
	uint16_t liveVsyncPacket;
	uint32_t padMask;
	uint32_t firstDomainID;
	uint64_t expectedDomainDigest;
	uint64_t liveDomainDigest;
	int combinedMismatch;
	uint64_t expectedCombinedDigest;
	uint64_t liveCombinedDigest;
	struct NativeCanonicalDriversCompareMask drivers;
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
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V3,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_ARMED_V4,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_RECORD_V4,
	NATIVE_REPLAY_SCHEDULER_CANONICAL_MODE_PLAYBACK_V4
};

enum NativeReplaySchedulerSelector
{
	NATIVE_REPLAY_SCHEDULER_SELECTOR_NONE = 0,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V1,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V1,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V2,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V2,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V3,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V3,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_RECORD_V4,
	NATIVE_REPLAY_SCHEDULER_SELECTOR_PLAYBACK_V4
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

/* V4 is a separately typed gate that also checks the configuration digest,
 * validates, recomputes every domain digest, and commits only on success. */
int NativeReplayScheduler_CopyCanonicalEndStateV4(uint32_t expectedReplayFrame, const struct NativeIdentityV1 *expectedIdentity,
	                                               const uint8_t expectedConfigDigest[NATIVE_SHA256_DIGEST_BYTES],
	                                               const struct NativeCanonicalStateV4 *source, struct NativeCanonicalStateV4 *destination);

/* Small value-only lifecycle gates used by the live scheduler and exercised
 * headlessly: playback retains each supplied packet, and a poisoned record
 * never finalizes its provisional replay header. */
int NativeReplayScheduler_CopyConsumedV2VSyncPacket(uint16_t *packets, uint32_t capacity, uint32_t index, uint16_t packet);
int NativeReplayScheduler_V2BeginObservationNeedsValidation(int playbackV2, int pending);
int NativeReplayScheduler_V2RecordMayFinalize(int poisoned, int checkpointClosed);
/* V3 cannot seal a prefix: exactly one bootstrap checkpoint must have been
 * written, no frame may remain open, and every prior failure must be clear. */
int NativeReplayScheduler_V3RecordMayFinalize(int poisoned, int beginOpen, uint32_t checkpointCount, int checkpointClosed);
void NativeReplaySchedulerV3Lifecycle_Init(struct NativeReplaySchedulerV3Lifecycle *lifecycle);
int NativeReplaySchedulerV3Lifecycle_BeginFrame(struct NativeReplaySchedulerV3Lifecycle *lifecycle);
int NativeReplaySchedulerV3Lifecycle_Submit(struct NativeReplaySchedulerV3Lifecycle *lifecycle,
	                                          enum NativeReplaySchedulerCanonicalKind requiredKind,
	                                          enum NativeReplaySchedulerCanonicalKind submittedKind);
int NativeReplaySchedulerV3Lifecycle_EndFrame(struct NativeReplaySchedulerV3Lifecycle *lifecycle);
void NativeReplaySchedulerV3Lifecycle_Abort(struct NativeReplaySchedulerV3Lifecycle *lifecycle);
int NativeReplaySchedulerV3Lifecycle_CheckpointClosed(struct NativeReplaySchedulerV3Lifecycle *lifecycle, int success);
int NativeReplaySchedulerV3Lifecycle_MayFinalize(const struct NativeReplaySchedulerV3Lifecycle *lifecycle);
/* Pure, transactional diagnostic value builder used by live V3 playback and
 * the temp-file scheduler harness. */
int NativeReplayScheduler_BuildV3MismatchReport(const struct NativeReplayV3Frame *expected,
	                                              const struct NativeReplayV2FrameObservation *liveEnd,
	                                              uint32_t liveVsyncTotal, uint32_t liveVsyncPacketCount,
	                                              const uint16_t *liveVsyncPackets, int playbackVsyncMismatch,
	                                              const struct NativeCanonicalStateV3 *liveCanonical,
	                                              struct NativeReplaySchedulerV3MismatchReport *report);

#endif
