#ifndef PLATFORM_NATIVE_REPLAY_V3_FILE_H
#define PLATFORM_NATIVE_REPLAY_V3_FILE_H

#include "platform/native_replay_v3.h"

#include <stdint.h>

struct NativeReplayV3RecordSession { void *stream; struct NativeReplayV3Header header; uint32_t nextFrame; int failed; int finalized; };
struct NativeReplayV3PlaybackSession { void *stream; struct NativeReplayV3Header header; struct NativeIdentityV1 expectedIdentity; uint32_t nextFrame; int failed; };
enum NativeReplayV3ReadResult { NATIVE_REPLAY_V3_READ_ERROR=-1, NATIVE_REPLAY_V3_READ_EOF=0, NATIVE_REPLAY_V3_READ_FRAME=1 };
int NativeReplayV3File_ExpectedLength(uint32_t frameCount,uint64_t *lengthOut);
void NativeReplayV3Record_Init(struct NativeReplayV3RecordSession *session);
int NativeReplayV3Record_Open(struct NativeReplayV3RecordSession *session,const char *path,const struct NativeIdentityV1 *identity);
int NativeReplayV3Record_AppendFrame(struct NativeReplayV3RecordSession *session,const struct NativeReplayV3Frame *frame);
int NativeReplayV3Record_Finalize(struct NativeReplayV3RecordSession *session);
void NativeReplayV3Record_Close(struct NativeReplayV3RecordSession *session);
void NativeReplayV3Playback_Init(struct NativeReplayV3PlaybackSession *session);
int NativeReplayV3Playback_Open(struct NativeReplayV3PlaybackSession *session,const char *path,const struct NativeIdentityV1 *expectedIdentity,struct NativeReplayV3Header *headerOut);
int NativeReplayV3Playback_ReadNext(struct NativeReplayV3PlaybackSession *session,struct NativeReplayV3Frame *frame);
void NativeReplayV3Playback_Close(struct NativeReplayV3PlaybackSession *session);

#endif
