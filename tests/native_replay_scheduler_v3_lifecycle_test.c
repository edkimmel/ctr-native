#include "platform/native_replay_scheduler_seam.h"
#include "platform/native_replay_v3_file.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define PATH_BYTES MAX_PATH
#else
#include <unistd.h>
#define PATH_BYTES 512
#endif

#define CHECK(expression)                                                                                                                 \
	do                                                                                                                                    \
	{                                                                                                                                     \
		if (!(expression))                                                                                                                  \
		{                                                                                                                                   \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                            \
			return 1;                                                                                                                         \
		}                                                                                                                                   \
	} while (0)

static int TemporaryPath(char path[PATH_BYTES])
{
#if defined(_WIN32)
	char directory[MAX_PATH];
	DWORD length = GetTempPathA(sizeof(directory), directory);
	return (length > 0) && (length < sizeof(directory)) && (GetTempFileNameA(directory, "csv", 0, path) != 0) && (remove(path) == 0);
#else
	char templatePath[] = "/tmp/ctr-scheduler-v3-XXXXXX";
	int descriptor = mkstemp(templatePath);
	if ((descriptor < 0) || (close(descriptor) != 0) || (unlink(templatePath) != 0)) return 0;
	memcpy(path, templatePath, sizeof(templatePath));
	return 1;
#endif
}

static void MakeIdentity(struct NativeIdentityV1 *identity)
{
	for (uint32_t index = 0; index < NATIVE_IDENTITY_DIGEST_BYTES; index++)
	{
		identity->build[index] = (uint8_t)(index + 1u);
		identity->content[index] = (uint8_t)(0x80u + index);
	}
}

static int MakeFrame(const struct NativeReplayV3Header *header, uint32_t replayFrame, struct NativeReplayV3Frame *frame)
{
	uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES] = {0};
	memset(frame, 0, sizeof(*frame));
	frame->replayFrame = replayFrame;
	frame->padCount = NATIVE_REPLAY_V2_PAD_COUNT;
	frame->vsyncPacketCount = 1;
	frame->vsyncPackets[0] = 1;
	frame->vsyncTotal = 1;
	NativeCanonicalStateV3_Init(&frame->canonical);
	frame->canonical.identity = header->identity;
	frame->canonical.frameNumber = replayFrame;
	stream[NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES] = 1;
	return NativeCanonicalDriversV1_FromNormativeStream(&frame->canonical.drivers, 1u, stream, sizeof(stream)) &&
	       NativeCanonicalStateV3_ComputeDigests(&frame->canonical);
}

/* This is an actual temporary replay file harness around the same lifecycle
 * state machine used by the scheduler.  It proves an accepted prefix cannot
 * become playable after any scheduler-side lifecycle error. */
static int TestRecordPlaybackAndPoisonedPrefix(void)
{
	char path[PATH_BYTES];
	struct NativeIdentityV1 identity;
	struct NativeReplayV3RecordSession record;
	struct NativeReplayV3PlaybackSession playback;
	struct NativeReplayV3Header header;
	struct NativeReplayV3Frame frame;
	struct NativeReplaySchedulerV3Lifecycle lifecycle;

	MakeIdentity(&identity);
	CHECK(TemporaryPath(path));
	NativeReplayV3Record_Init(&record);
	NativeReplaySchedulerV3Lifecycle_Init(&lifecycle);
	CHECK(NativeReplayV3Record_Open(&record, path, &identity));
	CHECK(NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(NativeReplaySchedulerV3Lifecycle_Submit(&lifecycle, NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3,
	                                               NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3));
	CHECK(MakeFrame(&record.header, 0u, &frame));
	CHECK(NativeReplayV3Record_AppendFrame(&record, &frame));
	CHECK(NativeReplaySchedulerV3Lifecycle_EndFrame(&lifecycle));
	CHECK(NativeReplaySchedulerV3Lifecycle_CheckpointClosed(&lifecycle, 1));
	CHECK(NativeReplaySchedulerV3Lifecycle_MayFinalize(&lifecycle));
	CHECK(NativeReplayV3Record_Finalize(&record));
	NativeReplayV3Playback_Init(&playback);
	CHECK(NativeReplayV3Playback_Open(&playback, path, &identity, &header));
	CHECK(NativeReplayV3Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V3_READ_FRAME);
	CHECK(NativeReplayV3Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V3_READ_EOF);
	NativeReplayV3Playback_Close(&playback);
	CHECK(remove(path) == 0);

	/* A submission-kind error after an accepted append poisons its prefix; a
	 * shutdown close deliberately leaves the header provisional and unreadable. */
	CHECK(TemporaryPath(path));
	NativeReplayV3Record_Init(&record);
	NativeReplaySchedulerV3Lifecycle_Init(&lifecycle);
	CHECK(NativeReplayV3Record_Open(&record, path, &identity));
	CHECK(NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(MakeFrame(&record.header, 0u, &frame));
	CHECK(NativeReplayV3Record_AppendFrame(&record, &frame));
	CHECK(NativeReplaySchedulerV3Lifecycle_EndFrame(&lifecycle));
	CHECK(NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(!NativeReplaySchedulerV3Lifecycle_Submit(&lifecycle, NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3,
	                                                NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V1));
	CHECK(!NativeReplaySchedulerV3Lifecycle_MayFinalize(&lifecycle));
	NativeReplayV3Record_Close(&record);
	NativeReplayV3Playback_Init(&playback);
	CHECK(!NativeReplayV3Playback_Open(&playback, path, &identity, &header));
	CHECK(remove(path) == 0);
	return 0;
}

static int TestLifecycleFaults(void)
{
	struct NativeReplaySchedulerV3Lifecycle lifecycle;
	NativeReplaySchedulerV3Lifecycle_Init(&lifecycle);
	CHECK(NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(!NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(lifecycle.poisoned && lifecycle.beginOpen);
	CHECK(!NativeReplaySchedulerV3Lifecycle_MayFinalize(&lifecycle));

	NativeReplaySchedulerV3Lifecycle_Init(&lifecycle);
	CHECK(NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(!NativeReplaySchedulerV3Lifecycle_Submit(&lifecycle, NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3,
	                                                (enum NativeReplaySchedulerCanonicalKind)99));
	CHECK(lifecycle.poisoned);

	NativeReplaySchedulerV3Lifecycle_Init(&lifecycle);
	CHECK(NativeReplaySchedulerV3Lifecycle_BeginFrame(&lifecycle));
	CHECK(NativeReplaySchedulerV3Lifecycle_Submit(&lifecycle, NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3,
	                                               NATIVE_REPLAY_SCHEDULER_CANONICAL_KIND_V3));
	CHECK(NativeReplaySchedulerV3Lifecycle_EndFrame(&lifecycle));
	CHECK(!NativeReplaySchedulerV3Lifecycle_MayFinalize(&lifecycle)); /* zero checkpoints */
	CHECK(!NativeReplaySchedulerV3Lifecycle_CheckpointClosed(&lifecycle, 0)); /* close failure */
	CHECK(lifecycle.poisoned && !NativeReplaySchedulerV3Lifecycle_MayFinalize(&lifecycle));
	return 0;
}

int main(void)
{
	if ((TestRecordPlaybackAndPoisonedPrefix() != 0) || (TestLifecycleFaults() != 0)) return 1;
	puts("native_replay_scheduler_v3_lifecycle_test: passed");
	return 0;
}
