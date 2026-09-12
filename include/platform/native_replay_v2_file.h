#ifndef PLATFORM_NATIVE_REPLAY_V2_FILE_H
#define PLATFORM_NATIVE_REPLAY_V2_FILE_H

#include "platform/native_replay_v2.h"

/*
 * Explicit v2 file sessions.  Their stream field is private FILE state; no
 * native record layout is ever written.  Init/Close are idempotent cleanup
 * operations.  A failed record session is sticky and cannot finalize.
 */
struct NativeReplayV2RecordSession
{
	void *stream;
	struct NativeReplayV2Header header;
	uint32_t nextFrame;
	int failed;
	int finalized;
};

struct NativeReplayV2PlaybackSession
{
	void *stream;
	struct NativeReplayV2Header header;
	struct NativeIdentityV1 expectedIdentity;
	uint32_t nextFrame;
	int failed;
};

enum NativeReplayV2ReadResult
{
	NATIVE_REPLAY_V2_READ_ERROR = -1,
	NATIVE_REPLAY_V2_READ_EOF = 0,
	NATIVE_REPLAY_V2_READ_FRAME = 1
};

void NativeReplayV2Record_Init(struct NativeReplayV2RecordSession *session);
int NativeReplayV2Record_Open(struct NativeReplayV2RecordSession *session, const char *path, const struct NativeIdentityV1 *identity);
int NativeReplayV2Record_AppendFrame(struct NativeReplayV2RecordSession *session, const struct NativeReplayV2Frame *frame);
int NativeReplayV2Record_Finalize(struct NativeReplayV2RecordSession *session);
void NativeReplayV2Record_Close(struct NativeReplayV2RecordSession *session);

void NativeReplayV2Playback_Init(struct NativeReplayV2PlaybackSession *session);
int NativeReplayV2Playback_Open(struct NativeReplayV2PlaybackSession *session, const char *path,
                                const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV2Header *headerOut);
int NativeReplayV2Playback_ReadNext(struct NativeReplayV2PlaybackSession *session, struct NativeReplayV2Frame *frame);
void NativeReplayV2Playback_Close(struct NativeReplayV2PlaybackSession *session);

#endif
