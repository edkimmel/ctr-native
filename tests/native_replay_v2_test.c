#include "platform/native_replay_v2.h"

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

static int FillFrame(struct NativeReplayV2Header *header, struct NativeReplayV2Frame *frame)
{
	NativeReplayV2Header_Init(header);
	FillIdentity(&header->identity);
	header->flags = NATIVE_REPLAY_V2_HEADER_FLAG_FINALIZED;
	header->frameCount = 1;
	memset(frame, 0, sizeof(*frame));
	frame->replayFrame = 7;
	frame->begin.frameTimer = -1;
	frame->begin.frameCounter = 2;
	frame->begin.timer = -3;
	frame->begin.framesInThisLEV = 4;
	frame->begin.elapsedTimeMS = 32;
	frame->begin.mainGameState = 6;
	frame->begin.mixRandomNumber = UINT32_C(0x11223344);
	frame->begin.audioRNG = UINT32_C(0x55667788);
	frame->end.frameTimer = 9;
	frame->end.elapsedTimeMS = 64;
	frame->end.deadcoed1 = UINT32_C(0xaabbccdd);
	frame->padCount = NATIVE_REPLAY_V2_PAD_COUNT;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_PAD_COUNT; i++)
	{
		frame->pads[i].status = (uint8_t)(0x10u + i);
		frame->pads[i].id = (uint8_t)(0x70u + i);
		frame->pads[i].buttons[0] = (uint8_t)(0x20u + i);
		frame->pads[i].buttons[1] = (uint8_t)(0x30u + i);
		frame->pads[i].analog[0] = (uint8_t)(0x40u + i);
		frame->pads[i].analog[1] = (uint8_t)(0x50u + i);
		frame->pads[i].analog[2] = (uint8_t)(0x60u + i);
		frame->pads[i].analog[3] = (uint8_t)(0x70u + i);
		frame->pads[i].connected = 1;
	}
	frame->vsyncPacketCount = 2;
	frame->vsyncPackets[0] = 2;
	frame->vsyncPackets[1] = 3;
	frame->vsyncTotal = 5;
	NativeCanonicalStateV1_Init(&frame->canonical);
	frame->canonical.identity = header->identity;
	frame->canonical.frameNumber = frame->replayFrame;
	frame->canonical.control.frameTimer = frame->end.frameTimer;
	frame->canonical.rng.mixRandomNumber = frame->end.mixRandomNumber;
	frame->canonical.input.pads[0].status = frame->pads[0].status;
	return NativeCanonicalStateV1_ComputeDigests(&frame->canonical);
}

static int TestGoldenHeaderAndRoundTrip(void)
{
	static const uint8_t goldenFramePrefix[] = {
		0x43, 0x52, 0x46, 0x32, 0x68, 0x02, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0x02, 0x00, 0x00, 0x00,
	};
	static const uint8_t goldenHeader[NATIVE_REPLAY_V2_HEADER_BYTES] = {
		0x43, 0x52, 0x56, 0x32, 0x02, 0x00, 0x00, 0x00, 0x8c, 0x00, 0x00, 0x00, 0x68, 0x02, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x00, 0x00, 0x28, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00,
		0x20, 0x00, 0x00, 0x00, 0x53, 0xb2, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf7, 0x4a, 0x33, 0x03,
		0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
		0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
		0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	};
	uint8_t bytes[NATIVE_REPLAY_V2_HEADER_BYTES + NATIVE_REPLAY_V2_FRAME_BYTES];
	uint8_t reencoded[NATIVE_REPLAY_V2_FRAME_BYTES];
	struct NativeReplayV2Header header, decodedHeader;
	struct NativeReplayV2Frame frame, decodedFrame;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	size_t streamSize;

	CHECK(FillFrame(&header, &frame));
	CHECK(NativeReplayV2Header_EncodedSize() == sizeof(goldenHeader));
	CHECK(NativeReplayV2Frame_EncodedSize() == NATIVE_REPLAY_V2_FRAME_BYTES);
	CHECK(NativeReplayV2_StreamSize(&header, &streamSize));
	CHECK(streamSize == sizeof(bytes));
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeReplayV2Header_Encode(&writer, &header));
	CHECK(memcmp(bytes, goldenHeader, sizeof(goldenHeader)) == 0);
	CHECK(NativeReplayV2Frame_Encode(&writer, &header, &frame));
	CHECK(NativeCodecWriter_Size(&writer) == sizeof(bytes));

	/* Golden frame layout: magic, fixed size, frame number, then signed begin fields in LE. */
	CHECK(memcmp(&bytes[NATIVE_REPLAY_V2_HEADER_BYTES], goldenFramePrefix, sizeof(goldenFramePrefix)) == 0);
	CHECK(bytes[NATIVE_REPLAY_V2_HEADER_BYTES + 320] == 0x4e && bytes[NATIVE_REPLAY_V2_HEADER_BYTES + 321] == 0x43 &&
	      bytes[NATIVE_REPLAY_V2_HEADER_BYTES + 322] == 0x56 && bytes[NATIVE_REPLAY_V2_HEADER_BYTES + 323] == 0x31);

	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeReplayV2Header_Decode(&reader, &header.identity, &decodedHeader));
	CHECK(NativeReplayV2Frame_Decode(&reader, &decodedHeader, &header.identity, &decodedFrame));
	CHECK(reader.offset == sizeof(bytes));
	NativeCodecWriter_Init(&writer, reencoded, sizeof(reencoded), NULL);
	CHECK(NativeReplayV2Frame_Encode(&writer, &decodedHeader, &decodedFrame));
	CHECK(memcmp(&bytes[NATIVE_REPLAY_V2_HEADER_BYTES], reencoded, sizeof(reencoded)) == 0);
	return 0;
}

static int TestGatesAndTransactions(void)
{
	uint8_t headerBytes[NATIVE_REPLAY_V2_HEADER_BYTES];
	uint8_t frameBytes[NATIVE_REPLAY_V2_FRAME_BYTES];
	struct NativeReplayV2Header header, decodedHeader;
	struct NativeReplayV2Frame frame, untouched;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	struct NativeIdentityV1 wrongIdentity;
	size_t originalOffset;

	CHECK(FillFrame(&header, &frame));
	NativeCodecWriter_Init(&writer, headerBytes, sizeof(headerBytes), NULL);
	CHECK(NativeReplayV2Header_Encode(&writer, &header));
	NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL);
	CHECK(NativeReplayV2Frame_Encode(&writer, &header, &frame));
	FillIdentity(&wrongIdentity); wrongIdentity.build[0] ^= 1;

	static const uint32_t headerGateOffsets[] = {0u, 4u, 8u, 12u, 24u};
	for (uint32_t i = 0; i < sizeof(headerGateOffsets) / sizeof(headerGateOffsets[0]); i++)
	{
		uint8_t corrupt[NATIVE_REPLAY_V2_HEADER_BYTES];
		memcpy(corrupt, headerBytes, sizeof(corrupt)); corrupt[headerGateOffsets[i]] ^= 1;
		NativeCodecReader_Init(&reader, corrupt, sizeof(corrupt)); originalOffset = reader.offset;
		memset(&decodedHeader, 0xa5, sizeof(decodedHeader));
		CHECK(!NativeReplayV2Header_Decode(&reader, &header.identity, &decodedHeader));
		CHECK(reader.offset == originalOffset);
	}
	headerBytes[16] = 2; NativeCodecReader_Init(&reader, headerBytes, sizeof(headerBytes)); originalOffset = reader.offset;
	memset(&decodedHeader, 0xa5, sizeof(decodedHeader));
	CHECK(!NativeReplayV2Header_Decode(&reader, &header.identity, &decodedHeader));
	CHECK(reader.offset == originalOffset);
	headerBytes[16] = (uint8_t)NATIVE_REPLAY_V2_HEADER_FLAG_FINALIZED;
	NativeCodecReader_Init(&reader, headerBytes, sizeof(headerBytes)); originalOffset = reader.offset;
	CHECK(!NativeReplayV2Header_Decode(&reader, &wrongIdentity, &decodedHeader)); CHECK(reader.offset == originalOffset);
	NativeCodecReader_Init(&reader, headerBytes, sizeof(headerBytes) - 1); CHECK(!NativeReplayV2Header_Decode(&reader, &header.identity, &decodedHeader));

	frame.padCount = 3; NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL); CHECK(!NativeReplayV2Frame_Encode(&writer, &header, &frame)); CHECK(writer.offset == 0);
	CHECK(FillFrame(&header, &frame)); frame.vsyncPacketCount = NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS + 1; NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL); CHECK(!NativeReplayV2Frame_Encode(&writer, &header, &frame));
	CHECK(FillFrame(&header, &frame)); frame.vsyncPackets[0] = 0; NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL); CHECK(!NativeReplayV2Frame_Encode(&writer, &header, &frame));
	CHECK(FillFrame(&header, &frame)); frame.vsyncPackets[frame.vsyncPacketCount] = 1; NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL);
	CHECK(!NativeReplayV2Frame_Encode(&writer, &header, &frame)); CHECK(writer.offset == 0);
	CHECK(FillFrame(&header, &frame)); frame.canonical.domainDigests[0] ^= 1; memset(frameBytes, 0xcc, sizeof(frameBytes));
	{ uint8_t before[NATIVE_REPLAY_V2_FRAME_BYTES]; memcpy(before, frameBytes, sizeof(before)); NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL);
	  CHECK(!NativeReplayV2Frame_Encode(&writer, &header, &frame)); CHECK(writer.offset == 0); CHECK(memcmp(frameBytes, before, sizeof(before)) == 0); }

	CHECK(FillFrame(&header, &frame)); NativeCodecWriter_Init(&writer, frameBytes, sizeof(frameBytes), NULL); CHECK(NativeReplayV2Frame_Encode(&writer, &header, &frame));
	frameBytes[192] = 1; NativeCodecReader_Init(&reader, frameBytes, sizeof(frameBytes)); originalOffset = reader.offset; memset(&untouched, 0xa5, sizeof(untouched));
	{ struct NativeReplayV2Frame before = untouched;
	  CHECK(!NativeReplayV2Frame_Decode(&reader, &header, &header.identity, &untouched)); CHECK(reader.offset == originalOffset); CHECK(memcmp(&untouched, &before, sizeof(before)) == 0); }
	frameBytes[192] = 0;
	frameBytes[320] ^= 1; NativeCodecReader_Init(&reader, frameBytes, sizeof(frameBytes)); originalOffset = reader.offset; memset(&untouched, 0xa5, sizeof(untouched));
	CHECK(!NativeReplayV2Frame_Decode(&reader, &header, &header.identity, &untouched)); CHECK(reader.offset == originalOffset);
	frameBytes[320] ^= 1; frameBytes[324] ^= 1; NativeCodecReader_Init(&reader, frameBytes, sizeof(frameBytes)); CHECK(!NativeReplayV2Frame_Decode(&reader, &header, &header.identity, &untouched));
	frameBytes[324] ^= 1; NativeCodecReader_Init(&reader, frameBytes, sizeof(frameBytes) - 1); CHECK(!NativeReplayV2Frame_Decode(&reader, &header, &header.identity, &untouched));
	NativeCodecReader_Init(&reader, frameBytes, sizeof(frameBytes)); originalOffset = reader.offset; CHECK(!NativeReplayV2Frame_Decode(&reader, &header, &wrongIdentity, &untouched)); CHECK(reader.offset == originalOffset);
	header.frameCount = UINT32_MAX; CHECK(!NativeReplayV2_StreamSize(&header, &(size_t){0}));
	return 0;
}

int main(void)
{
	if ((TestGoldenHeaderAndRoundTrip() != 0) || (TestGatesAndTransactions() != 0)) return 1;
	puts("native_replay_v2_test: passed");
	return 0;
}
