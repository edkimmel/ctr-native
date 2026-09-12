#include "platform/native_replay_v2_file.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

static FILE *NativeReplayV2File_Stream(void *stream) { return (FILE *)stream; }

static int NativeReplayV2File_WriteExact(FILE *stream, const uint8_t *bytes, size_t size)
{
	return (stream != NULL) && (fwrite(bytes, 1, size, stream) == size);
}

static int NativeReplayV2File_ReadExact(FILE *stream, uint8_t *bytes, size_t size)
{
	return (stream != NULL) && (fread(bytes, 1, size, stream) == size);
}

static int NativeReplayV2File_FlushSync(FILE *stream)
{
	if ((stream == NULL) || (fflush(stream) != 0)) return 0;
#if defined(_WIN32)
	return _commit(_fileno(stream)) == 0;
#else
	return fsync(fileno(stream)) == 0;
#endif
}

static int NativeReplayV2File_EncodeHeader(const struct NativeReplayV2Header *header, uint8_t bytes[NATIVE_REPLAY_V2_HEADER_BYTES])
{
	struct NativeCodecWriter writer;
	NativeCodecWriter_Init(&writer, bytes, NATIVE_REPLAY_V2_HEADER_BYTES, NULL);
	return NativeReplayV2Header_Encode(&writer, header) && (NativeCodecWriter_Size(&writer) == NATIVE_REPLAY_V2_HEADER_BYTES);
}

static int NativeReplayV2File_EncodeFrame(const struct NativeReplayV2Header *header, const struct NativeReplayV2Frame *frame,
                                          uint8_t bytes[NATIVE_REPLAY_V2_FRAME_BYTES])
{
	struct NativeCodecWriter writer;
	NativeCodecWriter_Init(&writer, bytes, NATIVE_REPLAY_V2_FRAME_BYTES, NULL);
	return NativeReplayV2Frame_Encode(&writer, header, frame) && (NativeCodecWriter_Size(&writer) == NATIVE_REPLAY_V2_FRAME_BYTES);
}

void NativeReplayV2Record_Init(struct NativeReplayV2RecordSession *session)
{
	if (session != NULL) memset(session, 0, sizeof(*session));
}

void NativeReplayV2Record_Close(struct NativeReplayV2RecordSession *session)
{
	FILE *stream;
	if (session == NULL) return;
	stream = NativeReplayV2File_Stream(session->stream);
	if (stream != NULL) (void)fclose(stream);
	memset(session, 0, sizeof(*session));
}

int NativeReplayV2Record_Open(struct NativeReplayV2RecordSession *session, const char *path, const struct NativeIdentityV1 *identity)
{
	struct NativeReplayV2Header header;
	uint8_t bytes[NATIVE_REPLAY_V2_HEADER_BYTES];
	FILE *stream;

	if ((session == NULL) || (session->stream != NULL) || (path == NULL) || (identity == NULL)) return 0;
	NativeReplayV2Header_Init(&header);
	memcpy(header.identity.build, identity->build, NATIVE_IDENTITY_DIGEST_BYTES);
	memcpy(header.identity.content, identity->content, NATIVE_IDENTITY_DIGEST_BYTES);
	if (!NativeReplayV2Header_Validate(&header) || !NativeReplayV2File_EncodeHeader(&header, bytes)) return 0;
	stream = fopen(path, "wb+");
	if ((stream == NULL) || !NativeReplayV2File_WriteExact(stream, bytes, sizeof(bytes)) || !NativeReplayV2File_FlushSync(stream))
	{
		if (stream != NULL) (void)fclose(stream);
		return 0;
	}
	session->stream = stream;
	session->header = header;
	session->nextFrame = 0;
	session->failed = 0;
	session->finalized = 0;
	return 1;
}

int NativeReplayV2Record_AppendFrame(struct NativeReplayV2RecordSession *session, const struct NativeReplayV2Frame *frame)
{
	struct NativeReplayV2Header candidateHeader;
	uint8_t bytes[NATIVE_REPLAY_V2_FRAME_BYTES];
	FILE *stream;

	if ((session == NULL) || (session->failed != 0) || (session->finalized != 0) || (session->stream == NULL) || (frame == NULL)) return 0;
	if ((frame->replayFrame != session->nextFrame) || (session->header.frameCount == UINT32_MAX))
	{
		session->failed = 1;
		return 0;
	}
	candidateHeader = session->header;
	candidateHeader.frameCount++;
	if (!NativeReplayV2_StreamSize(&candidateHeader, &(size_t){0}) || !NativeReplayV2File_EncodeFrame(&session->header, frame, bytes))
	{
		session->failed = 1;
		return 0;
	}
	stream = NativeReplayV2File_Stream(session->stream);
	if (!NativeReplayV2File_WriteExact(stream, bytes, sizeof(bytes)))
	{
		session->failed = 1;
		return 0;
	}
	session->header = candidateHeader;
	session->nextFrame++;
	return 1;
}

int NativeReplayV2Record_Finalize(struct NativeReplayV2RecordSession *session)
{
	uint8_t bytes[NATIVE_REPLAY_V2_HEADER_BYTES];
	FILE *stream;
	int closeResult;

	if ((session == NULL) || (session->failed != 0) || (session->finalized != 0) || (session->stream == NULL) ||
	    !NativeReplayV2File_EncodeHeader(&session->header, bytes))
	{
		if (session != NULL) session->failed = 1;
		return 0;
	}
	stream = NativeReplayV2File_Stream(session->stream);
	if ((fseek(stream, 0, SEEK_SET) != 0) || !NativeReplayV2File_WriteExact(stream, bytes, sizeof(bytes)) || !NativeReplayV2File_FlushSync(stream))
	{
		session->failed = 1;
		return 0;
	}
	closeResult = fclose(stream);
	session->stream = NULL;
	if (closeResult != 0)
	{
		session->failed = 1;
		return 0;
	}
	session->finalized = 1;
	return 1;
}

void NativeReplayV2Playback_Init(struct NativeReplayV2PlaybackSession *session)
{
	if (session != NULL) memset(session, 0, sizeof(*session));
}

void NativeReplayV2Playback_Close(struct NativeReplayV2PlaybackSession *session)
{
	FILE *stream;
	if (session == NULL) return;
	stream = NativeReplayV2File_Stream(session->stream);
	if (stream != NULL) (void)fclose(stream);
	memset(session, 0, sizeof(*session));
}

int NativeReplayV2Playback_Open(struct NativeReplayV2PlaybackSession *session, const char *path,
                                const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV2Header *headerOut)
{
	uint8_t bytes[NATIVE_REPLAY_V2_HEADER_BYTES];
	struct NativeCodecReader reader;
	struct NativeReplayV2Header header;
	FILE *stream;
	long length;
	size_t expectedLength;

	if ((session == NULL) || (session->stream != NULL) || (path == NULL) || (expectedIdentity == NULL) || (headerOut == NULL)) return 0;
	stream = fopen(path, "rb");
	if ((stream == NULL) || (fseek(stream, 0, SEEK_END) != 0) || ((length = ftell(stream)) < 0) || (fseek(stream, 0, SEEK_SET) != 0) ||
	    !NativeReplayV2File_ReadExact(stream, bytes, sizeof(bytes)))
	{
		if (stream != NULL) (void)fclose(stream);
		return 0;
	}
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	if (!NativeReplayV2Header_Decode(&reader, expectedIdentity, &header) || (NativeCodecReader_Remaining(&reader) != 0) ||
	    !NativeReplayV2_StreamSize(&header, &expectedLength) || (expectedLength > (size_t)LONG_MAX) || ((size_t)length != expectedLength))
	{
		(void)fclose(stream);
		return 0;
	}
	session->stream = stream;
	session->header = header;
	memcpy(session->expectedIdentity.build, expectedIdentity->build, NATIVE_IDENTITY_DIGEST_BYTES);
	memcpy(session->expectedIdentity.content, expectedIdentity->content, NATIVE_IDENTITY_DIGEST_BYTES);
	session->nextFrame = 0;
	session->failed = 0;
	*headerOut = header;
	return 1;
}

int NativeReplayV2Playback_ReadNext(struct NativeReplayV2PlaybackSession *session, struct NativeReplayV2Frame *frame)
{
	uint8_t bytes[NATIVE_REPLAY_V2_FRAME_BYTES];
	struct NativeCodecReader reader;
	struct NativeReplayV2Frame candidate;
	FILE *stream;

	if ((session == NULL) || (frame == NULL) || (session->stream == NULL) || (session->failed != 0)) return NATIVE_REPLAY_V2_READ_ERROR;
	if (session->nextFrame == session->header.frameCount) return NATIVE_REPLAY_V2_READ_EOF;
	stream = NativeReplayV2File_Stream(session->stream);
	if (!NativeReplayV2File_ReadExact(stream, bytes, sizeof(bytes)))
	{
		session->failed = 1;
		return NATIVE_REPLAY_V2_READ_ERROR;
	}
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	if (!NativeReplayV2Frame_Decode(&reader, &session->header, &session->expectedIdentity, &candidate) ||
	    (NativeCodecReader_Remaining(&reader) != 0) || (candidate.replayFrame != session->nextFrame))
	{
		session->failed = 1;
		return NATIVE_REPLAY_V2_READ_ERROR;
	}
	*frame = candidate;
	session->nextFrame++;
	return NATIVE_REPLAY_V2_READ_FRAME;
}
