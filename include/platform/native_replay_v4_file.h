#ifndef PLATFORM_NATIVE_REPLAY_V4_FILE_H
#define PLATFORM_NATIVE_REPLAY_V4_FILE_H

/* Isolated CRV4 disk transaction and sequential reader.  This is deliberately
 * not wired to the replay scheduler or any runtime path. */
#include "platform/native_replay_v4.h"

#include <stdint.h>

struct NativeReplayV4RecordSession {
	void *stream;
	struct NativeReplayV4Header header;
	uint32_t nextFrame;
	int failed;
	int finalized;
	char *path;
	char *temporaryPath;
};
struct NativeReplayV4PlaybackSession {
	void *stream;
	struct NativeReplayV4Header header;
	struct NativeIdentityV1 expectedIdentity;
	uint32_t nextFrame;
	int failed;
};
enum NativeReplayV4ReadResult { NATIVE_REPLAY_V4_READ_ERROR=-1, NATIVE_REPLAY_V4_READ_EOF=0, NATIVE_REPLAY_V4_READ_FRAME=1 };

int NativeReplayV4File_ExpectedLength(uint32_t frameCount, uint64_t *lengthOut);
void NativeReplayV4Record_Init(struct NativeReplayV4RecordSession *session);
int NativeReplayV4Record_Open(struct NativeReplayV4RecordSession *session, const char *path,
	const struct NativeIdentityV1 *identity, const struct NativeMatchConfigV1 *config);
int NativeReplayV4Record_AppendFrame(struct NativeReplayV4RecordSession *session, const struct NativeReplayV4Frame *frame);
int NativeReplayV4Record_Finalize(struct NativeReplayV4RecordSession *session);
void NativeReplayV4Record_Close(struct NativeReplayV4RecordSession *session);
void NativeReplayV4Playback_Init(struct NativeReplayV4PlaybackSession *session);
int NativeReplayV4Playback_Open(struct NativeReplayV4PlaybackSession *session, const char *path,
	const struct NativeIdentityV1 *expectedIdentity, const struct NativeMatchConfigV1 *expectedConfig,
	struct NativeReplayV4Header *headerOut);
int NativeReplayV4Playback_ReadNext(struct NativeReplayV4PlaybackSession *session, struct NativeReplayV4Frame *frame);
void NativeReplayV4Playback_Close(struct NativeReplayV4PlaybackSession *session);

/* Test-only deterministic I/O seam.  The selected operation fails once. */
enum NativeReplayV4FileTestFault { NATIVE_REPLAY_V4_FILE_TEST_FAULT_NONE, NATIVE_REPLAY_V4_FILE_TEST_FAULT_APPEND,
	NATIVE_REPLAY_V4_FILE_TEST_FAULT_SYNC, NATIVE_REPLAY_V4_FILE_TEST_FAULT_CLOSE,
	NATIVE_REPLAY_V4_FILE_TEST_FAULT_REWRITE, NATIVE_REPLAY_V4_FILE_TEST_FAULT_RENAME };
void NativeReplayV4File_TestSetFault(enum NativeReplayV4FileTestFault fault);

#endif
