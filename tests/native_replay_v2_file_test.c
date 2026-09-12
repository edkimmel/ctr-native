#include "platform/native_replay_v2_file.h"

#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

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

static int MakePath(char path[MAX_PATH])
{
	char directory[MAX_PATH];
	DWORD size = GetTempPathA((DWORD)sizeof(directory), directory);
	return (size > 0) && (size < sizeof(directory)) && (GetTempFileNameA(directory, "cr2", 0, path) != 0) && (remove(path) == 0);
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

static int PatchByte(const char *path, long offset, uint8_t value)
{
	FILE *file = fopen(path, "r+b");
	if (file == NULL) return 0;
	if ((fseek(file, offset, SEEK_SET) != 0) || (fputc(value, file) == EOF))
	{
		(void)fclose(file);
		return 0;
	}
	return fclose(file) == 0;
}

static int TestEmptyOneManyRoundTrip(void)
{
	char path[MAX_PATH];
	struct NativeIdentityV1 identity;
	struct NativeReplayV2PlaybackSession playback;
	struct NativeReplayV2Header header;
	struct NativeReplayV2Frame frame;
	FILE *file;
	long length;

	FillIdentity(&identity);
	CHECK(MakePath(path));
	CHECK(WriteReplayFile(path, &identity, 0));
	NativeReplayV2Playback_Init(&playback);
	CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	CHECK(header.frameCount == 0);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_EOF);
	NativeReplayV2Playback_Close(&playback); NativeReplayV2Playback_Close(&playback);
	CHECK(remove(path) == 0);

	CHECK(MakePath(path));
	CHECK(WriteReplayFile(path, &identity, 2));
	file = fopen(path, "rb"); CHECK(file != NULL); CHECK(fseek(file, 0, SEEK_END) == 0); length = ftell(file); CHECK(fclose(file) == 0);
	CHECK(length == (long)(NATIVE_REPLAY_V2_HEADER_BYTES + 2u * NATIVE_REPLAY_V2_FRAME_BYTES));
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
	char path[MAX_PATH];
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

static int OpenMustFail(const char *path, const struct NativeIdentityV1 *identity)
{
	struct NativeReplayV2PlaybackSession playback;
	struct NativeReplayV2Header header;
	struct NativeReplayV2Header before;
	NativeReplayV2Playback_Init(&playback);
	memset(&header, 0xa5, sizeof(header)); before = header;
	CHECK(!NativeReplayV2Playback_Open(&playback, path, identity, &header));
	CHECK(memcmp(&header, &before, sizeof(header)) == 0);
	NativeReplayV2Playback_Close(&playback);
	return 0;
}

static int TestPlaybackPreflightAndFrameGates(void)
{
	char path[MAX_PATH];
	struct NativeIdentityV1 identity, wrongIdentity;
	struct NativeReplayV2PlaybackSession playback;
	struct NativeReplayV2Header header;
	struct NativeReplayV2Frame frame, before;
	FILE *file;
	long length;

	FillIdentity(&identity); wrongIdentity = identity; wrongIdentity.build[0] ^= 1;
	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(OpenMustFail(path, &wrongIdentity) == 0);
	CHECK(PatchByte(path, 0, 0)); CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(PatchByte(path, 20, 0)); CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	file = fopen(path, "r+b"); CHECK(file != NULL); CHECK(fseek(file, 0, SEEK_END) == 0); length = ftell(file); CHECK(_chsize_s(_fileno(file), (size_t)(length - 1)) == 0); CHECK(fclose(file) == 0);
	CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	file = fopen(path, "ab"); CHECK(file != NULL); CHECK(fputc(0xff, file) != EOF); CHECK(fclose(file) == 0);
	CHECK(OpenMustFail(path, &identity) == 0); CHECK(remove(path) == 0);

	/* Canonical CONTROL digest is 140 bytes into the canonical record, whose frame offset is 320. */
	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(PatchByte(path, (long)(NATIVE_REPLAY_V2_HEADER_BYTES + 320u + 140u), 0));
	NativeReplayV2Playback_Init(&playback); CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	memset(&frame, 0xa5, sizeof(frame)); before = frame;
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_ERROR);
	CHECK(memcmp(&frame, &before, sizeof(frame)) == 0);
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_ERROR);
	NativeReplayV2Playback_Close(&playback); CHECK(remove(path) == 0);

	CHECK(MakePath(path)); CHECK(WriteReplayFile(path, &identity, 1));
	CHECK(PatchByte(path, (long)(NATIVE_REPLAY_V2_HEADER_BYTES + 8u), 1));
	NativeReplayV2Playback_Init(&playback); CHECK(NativeReplayV2Playback_Open(&playback, path, &identity, &header));
	memset(&frame, 0xa5, sizeof(frame)); before = frame;
	CHECK(NativeReplayV2Playback_ReadNext(&playback, &frame) == NATIVE_REPLAY_V2_READ_ERROR);
	CHECK(memcmp(&frame, &before, sizeof(frame)) == 0);
	NativeReplayV2Playback_Close(&playback); CHECK(remove(path) == 0);
	return 0;
}

int main(void)
{
	if ((TestEmptyOneManyRoundTrip() != 0) || (TestRecordSequenceAndStickyFailure() != 0) || (TestPlaybackPreflightAndFrameGates() != 0)) return 1;
	puts("native_replay_v2_file_test: passed");
	return 0;
}
