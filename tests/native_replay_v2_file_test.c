#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include "platform/native_replay_v2_file.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define NATIVE_REPLAY_V2_TEST_PATH_BYTES MAX_PATH
#else
#include <sys/types.h>
#include <unistd.h>
#define NATIVE_REPLAY_V2_TEST_PATH_BYTES 512
#endif

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                         \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static void FillIdentity(struct NativeIdentityV1 *identity)
{
	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		identity->build[i] = (uint8_t)i;
		identity->content[i] = (uint8_t)(0x80u + i);
	}
}

static int MakePath(char path[NATIVE_REPLAY_V2_TEST_PATH_BYTES])
{
#if defined(_WIN32)
	char directory[MAX_PATH];
	DWORD size = GetTempPathA((DWORD)sizeof(directory), directory);
	return (size > 0) && (size < sizeof(directory)) && (GetTempFileNameA(directory, "cr2", 0, path) != 0) && (remove(path) == 0);
#else
	char template[] = "/tmp/ctr-native-v2-XXXXXX";
	int descriptor = mkstemp(template);
	if ((descriptor < 0) || (close(descriptor) != 0))
	{
		if (descriptor >= 0) (void)unlink(template);
		return 0;
	}
	if (unlink(template) != 0) return 0;
	memcpy(path, template, sizeof(template));
	return 1;
#endif
}

static int FillFrame(const struct NativeReplayV2Header *header, uint32_t replayFrame, struct NativeReplayV2Frame *frame)
{
	memset(frame, 0, sizeof(*frame));
	frame->replayFrame = replayFrame;
	frame->begin.frameTimer = -(int32_t)replayFrame;
	frame->begin.frameCounter = (int32_t)(100u + replayFrame);
	frame->end.timer = (int32_t)(200u + replayFrame);
	frame->padCount = NATIVE_REPLAY_V2_PAD_COUNT;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_PAD_COUNT; i++)
	{
		frame->pads[i].status = (uint8_t)(0x20u + i);
		frame->pads[i].id = (uint8_t)(0x30u + i);
		frame->pads[i].buttons[0] = (uint8_t)(0x40u + i);
		frame->pads[i].buttons[1] = (uint8_t)(0x50u + i);
		for (uint32_t j = 0; j < 4; j++) frame->pads[i].analog[j] = (uint8_t)(0x60u + (4u * i) + j);
		frame->pads[i].connected = (uint8_t)(i != 3u);
	}
	frame->vsyncPacketCount = 1;
	frame->vsyncPackets[0] = 1;
	frame->vsyncTotal = 1;
	NativeCanonicalStateV1_Init(&frame->canonical);
	frame->canonical.frameNumber = replayFrame;
	memcpy(frame->canonical.identity.build, header->identity.build, NATIVE_IDENTITY_DIGEST_BYTES);
	memcpy(frame->canonical.identity.content, header->identity.content, NATIVE_IDENTITY_DIGEST_BYTES);
	frame->canonical.control.frameCounter = frame->begin.frameCounter;
	frame->canonical.rng.mixRandomNumber = UINT32_C(0x11223344) + replayFrame;
	frame->canonical.input.pads[0].connected = 1;
	return NativeCanonicalStateV1_ComputeDigests(&frame->canonical);
}

static int WriteReplayFile(const char *path, const struct NativeIdentityV1 *identity, uint32_t count)
{
	struct NativeReplayV2RecordSession record;
	struct NativeReplayV2Frame frame;
	NativeReplayV2Record_Init(&record);
	if (!NativeReplayV2Record_Open(&record, path, identity)) return 0;
	for (uint32_t i = 0; i < count; i++)
	{
		if (!FillFrame(&record.header, i, &frame) || !NativeReplayV2Record_AppendFrame(&record, &frame))
		{
			NativeReplayV2Record_Close(&record);
			return 0;
		}
	}
	if (!NativeReplayV2Record_Finalize(&record))
	{
		NativeReplayV2Record_Close(&record);
		return 0;
	}
	NativeReplayV2Record_Close(&record);
	return 1;
}

static int SeekAbsolute(FILE *file, uint64_t offset)
{
	if ((file == NULL) || (offset > (uint64_t)INT64_MAX)) return 0;
#if defined(_WIN32)
	return _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
#else
	return fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}

static int FileLength(FILE *file, uint64_t *lengthOut)
{
#if defined(_WIN32)
	__int64 length;
#else
	off_t length;
#endif
	if ((file == NULL) || (lengthOut == NULL)) return 0;
#if defined(_WIN32)
	if (_fseeki64(file, 0, SEEK_END) != 0) return 0;
	length = _ftelli64(file);
#else
	if (fseeko(file, (off_t)0, SEEK_END) != 0) return 0;
	length = ftello(file);
#endif
	if (length < 0) return 0;
	*lengthOut = (uint64_t)length;
	return SeekAbsolute(file, 0);
}

static int TruncateLastByte(const char *path)
{
	FILE *file = fopen(path, "r+b");
	uint64_t length;
	int result;
	if ((file == NULL) || !FileLength(file, &length) || (length == 0))
	{
		if (file != NULL) (void)fclose(file);
		return 0;
	}
#if defined(_WIN32)
	result = _chsize_s(_fileno(file), length - 1) == 0;
#else
	result = ftruncate(fileno(file), (off_t)(length - 1)) == 0;
#endif
	return result && (fclose(file) == 0);
}

static int PatchByte(const char *path, uint64_t offset, uint8_t value)
{
	FILE *file = fopen(path, "r+b");
	if (file == NULL) return 0;
	if (!SeekAbsolute(file, offset) || (fputc(value, file) == EOF))
	{
		(void)fclose(file);
		return 0;
	}
	return fclose(file) == 0;
}

static int TestEmptyOneManyRoundTrip(void)
{
	char path[NATIVE_REPLAY_V2_TEST_PATH_BYTES];
	struct NativeIdentityV1 identity;
	struct NativeReplayV2PlaybackSession playback;
	struct NativeReplayV2Header header;
	struct NativeReplayV2Frame frame;
	FILE *file;
	uint64_t length;

	FillIdentity(&identity);
	CHECK(MakePath(path));
	CHECK(WriteReplayFile(path, &identity, 0));
	NativeReplayV2Playback_Init(&playback);
	CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	CHECK(header.frameCount == 0 && header.flags == NATIVE_REPLAY_V2_HEADER_FLAG_FINALIZED);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_EOF);
	NativeReplayV2Playback_Close(&playback); NativeReplayV2Playback_Close(&playback);
	CHECK(remove(path) == 0);

	CHECK(MakePath(path));
	CHECK(WriteReplayFile(path, &identity, 2));
	file = fopen(path, "rb"); CHECK(file != NULL); CHECK(FileLength(file, &length)); CHECK(fclose(file) == 0);
	CHECK(length == NATIVE_REPLAY_V2_HEADER_BYTES + UINT64_C(2) * NATIVE_REPLAY_V2_FRAME_BYTES);
	NativeReplayV2Playback_Init(&playback);
	CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	CHECK(header.frameCount == 2);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_FRAME && frame.replayFrame == 0 && frame.canonical.frameNumber == 0);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_FRAME && frame.replayFrame == 1 && frame.canonical.frameNumber == 1);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_EOF);
	NativeReplayV2Playback_Close(&playback); NativeReplayV2Playback_Close(&playback);
	CHECK(remove(path) == 0);
	return 0;
}

static int TestRecordSequenceAndStickyFailure(void)
{
	char path[NATIVE_REPLAY_V2_TEST_PATH_BYTES];
	struct NativeIdentityV1 identity;
	struct NativeReplayV2RecordSession record;
	struct NativeReplayV2Frame frame;
	struct NativeReplayV2Header overflow;

	FillIdentity(&identity);
	CHECK(MakePath(path));
	NativeReplayV2Record_Init(&record);
	CHECK(NativeReplayV2Record_Open(&record, path, &identity));
	CHECK(FillFrame(&record.header, 1, &frame));
	CHECK(!NativeReplayV2Record_AppendFrame(&record, &frame));
	CHECK(record.nextFrame == 0 && record.header.frameCount == 0 && record.failed != 0);
	CHECK(!NativeReplayV2Record_AppendFrame(&record, &frame));
	CHECK(!NativeReplayV2Record_Finalize(&record));
	NativeReplayV2Record_Close(&record); NativeReplayV2Record_Close(&record);
	CHECK(remove(path) == 0);
	NativeReplayV2Header_Init(&overflow); overflow.frameCount = UINT32_MAX;
	CHECK(!NativeReplayV2_StreamSize(&overflow, &(size_t){0}));
	NativeReplayV2Record_Init(&record);
	CHECK(!NativeReplayV2Record_Open(&record, "", &identity));
	return 0;
}

static int TestIdentityGateDoesNotCreateFile(void)
{
	char path[NATIVE_REPLAY_V2_TEST_PATH_BYTES];
	struct NativeReplayV2RecordSession record;
	FILE *file;

	CHECK(MakePath(path));
	NativeReplayV2Record_Init(&record);
	CHECK(!NativeReplayV2Record_Open(&record, path, NULL));
	file = fopen(path, "rb");
	CHECK(file == NULL);
	NativeReplayV2Record_Close(&record);
	return 0;
}

static int OpenMustFail(const char *path, const struct NativeIdentityV1 *identity)
{
	struct NativeReplayV2PlaybackSession playback;
	struct NativeReplayV2Header header;
	struct NativeReplayV2Header before;
	NativeReplayV2Playback_Init(&playback);
	memset(&header, 0xa5, sizeof(header)); before = header;
	CHECK(!NativeReplayV2Playback_Open(&playback, path, identity, &header));
	CHECK(memcmp(&header, &before, sizeof(header)) == 0);
	CHECK(playback.stream == NULL && playback.nextFrame == 0 && playback.failed == 0);
	NativeReplayV2Playback_Close(&playback);
	return 0;
}

static int TestProvisionalFilesAreUnplayable(void)
{
	char path[NATIVE_REPLAY_V2_TEST_PATH_BYTES];
	struct NativeIdentityV1 identity;
	struct NativeReplayV2RecordSession record;
	struct NativeReplayV2Frame frame;

	FillIdentity(&identity);
	/* A poisoned zero-frame record still has a syntactically complete header,
	 * but its clear FINALIZED flag makes Playback_Open reject it. */
	CHECK(MakePath(path)); NativeReplayV2Record_Init(&record);
	CHECK(NativeReplayV2Record_Open(&record, path, &identity));
	CHECK(FillFrame(&record.header, 1u, &frame));
	CHECK(!NativeReplayV2Record_AppendFrame(&record, &frame));
	CHECK(record.failed != 0 && record.header.frameCount == 0);
	NativeReplayV2Record_Close(&record);
	CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	/* A prefix with appended frames remains unplayable until Finalize rewrites
	 * its header with a matching count and the FINALIZED bit. */
	CHECK(MakePath(path)); NativeReplayV2Record_Init(&record);
	CHECK(NativeReplayV2Record_Open(&record, path, &identity));
	CHECK(FillFrame(&record.header, 0u, &frame));
	CHECK(NativeReplayV2Record_AppendFrame(&record, &frame));
	NativeReplayV2Record_Close(&record);
	CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);
	return 0;
}

static int TestPlaybackPreflightAndFrameGates(void)
{
	char path[NATIVE_REPLAY_V2_TEST_PATH_BYTES];
	struct NativeIdentityV1 identity, wrongIdentity;
	struct NativeReplayV2PlaybackSession playback;
	struct NativeReplayV2Header header;
	struct NativeReplayV2Frame frame, before;
	FILE *file;

	FillIdentity(&identity); wrongIdentity = identity; wrongIdentity.build[0] ^= 1;
	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(OpenMustFail(path, &wrongIdentity) == 0);
	CHECK(PatchByte(path, 0, 0)); CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(PatchByte(path, 24, 0)); CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 0));
	CHECK(PatchByte(path, 16, 2)); CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(TruncateLastByte(path));
	CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	file = fopen(path, "ab"); CHECK(file != NULL); CHECK(fputc(0xff, file) != EOF); CHECK(fclose(file) == 0);
	CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	/* Canonical CONTROL digest is 140 bytes into the canonical record, whose frame offset is 320. */
	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(PatchByte(path, NATIVE_REPLAY_V2_HEADER_BYTES + UINT64_C(320) + UINT64_C(140), 0));
	NativeReplayV2Playback_Init(&playback); CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	memset(&frame, 0xa5, sizeof(frame)); before = frame;
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_ERROR);
	CHECK(memcmp(&frame, &before, sizeof(frame)) == 0);
	CHECK(playback.nextFrame == 0 && playback.failed != 0);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_ERROR);
	NativeReplayV2Playback_Close(&playback); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(PatchByte(path, NATIVE_REPLAY_V2_HEADER_BYTES + UINT64_C(8), 1));
	NativeReplayV2Playback_Init(&playback); CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	memset(&frame, 0xa5, sizeof(frame)); before = frame;
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_ERROR);
	CHECK(memcmp(&frame, &before, sizeof(frame)) == 0);
	NativeReplayV2Playback_Close(&playback); CHECK(remove(path) == 0);
	return 0;
}

static int TestLengthBoundaries(void)
{
	/* Windows long is 32 bits; these prove the sealed size math no longer is. */
	const uint64_t legacyLongMax = UINT64_C(2147483647);
	const uint64_t countAtLegacyLimit = (legacyLongMax - NATIVE_REPLAY_V2_HEADER_BYTES) / NATIVE_REPLAY_V2_FRAME_BYTES;
	uint64_t lengthAtLegacyLimit;
	uint64_t lengthPastLegacyLimit;
	uint64_t largestLength;

	CHECK(countAtLegacyLimit < UINT32_MAX);
	CHECK(NativeReplayV2File_ExpectedLength((uint32_t)countAtLegacyLimit, &lengthAtLegacyLimit));
	CHECK(NativeReplayV2File_ExpectedLength((uint32_t)(countAtLegacyLimit + 1), &lengthPastLegacyLimit));
	CHECK(lengthAtLegacyLimit <= legacyLongMax);
	CHECK(lengthPastLegacyLimit > legacyLongMax);
	CHECK(NativeReplayV2File_ExpectedLength(UINT32_MAX, &largestLength));
	CHECK(largestLength == NATIVE_REPLAY_V2_HEADER_BYTES + ((uint64_t)UINT32_MAX * NATIVE_REPLAY_V2_FRAME_BYTES));
	CHECK(!NativeReplayV2File_ExpectedLength(0, NULL));
	return 0;
}

int main(void)
{
	if ((TestEmptyOneManyRoundTrip() != 0) || (TestRecordSequenceAndStickyFailure() != 0) || (TestIdentityGateDoesNotCreateFile() != 0) ||
	    (TestProvisionalFilesAreUnplayable() != 0) || (TestPlaybackPreflightAndFrameGates() != 0) ||
	    (TestLengthBoundaries() != 0)) return 1;
	puts("native_replay_v2_file_test: passed");
	return 0;
}
