#include "platform/native_replay_v2.h"

#include <string.h>

#define NATIVE_REPLAY_V2_OBSERVATION_BYTES 64u
#define NATIVE_REPLAY_V2_PAD_BYTES 9u

static int NativeReplayV2_IdentityEquals(const struct NativeIdentityV1 *left, const struct NativeIdentityV1 *right)
{
	return (memcmp(left->build, right->build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(left->content, right->content, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
}

static int NativeReplayV2_WriterReady(const struct NativeCodecWriter *writer, size_t bytes)
{
	return (writer != NULL) && (writer->failed == 0) && (writer->offset <= writer->capacity) &&
	       !((writer->data == NULL) && (writer->capacity != 0)) && (bytes <= writer->capacity - writer->offset);
}

static int NativeReplayV2_ReaderReady(const struct NativeCodecReader *reader, size_t bytes)
{
	return (reader != NULL) && (reader->failed == 0) && (reader->offset <= reader->size) &&
	       !((reader->data == NULL) && (reader->size != 0)) && (bytes <= reader->size - reader->offset);
}

static int NativeReplayV2_EncodeObservation(struct NativeCodecWriter *writer, const struct NativeReplayV2FrameObservation *value)
{
	return NativeCodecWriter_WriteS32(writer, value->frameTimer) && NativeCodecWriter_WriteS32(writer, value->frameCounter) &&
	       NativeCodecWriter_WriteS32(writer, value->timer) && NativeCodecWriter_WriteS32(writer, value->framesInThisLEV) &&
	       NativeCodecWriter_WriteS32(writer, value->elapsedTimeMS) && NativeCodecWriter_WriteS32(writer, value->msInThisLEV) &&
	       NativeCodecWriter_WriteS32(writer, value->elapsedEventTime) && NativeCodecWriter_WriteS32(writer, value->mainGameState) &&
	       NativeCodecWriter_WriteS32(writer, value->loadingStage) && NativeCodecWriter_WriteS32(writer, value->levelID) &&
	       NativeCodecWriter_WriteU32(writer, value->mixRandomNumber) && NativeCodecWriter_WriteU32(writer, value->audioRNG) &&
	       NativeCodecWriter_WriteU32(writer, value->deadcoed0) && NativeCodecWriter_WriteU32(writer, value->deadcoed1) &&
	       NativeCodecWriter_WriteU32(writer, value->advRng0) && NativeCodecWriter_WriteU32(writer, value->advRng1);
}

static int NativeReplayV2_DecodeObservation(struct NativeCodecReader *reader, struct NativeReplayV2FrameObservation *value)
{
	return NativeCodecReader_ReadS32(reader, &value->frameTimer) && NativeCodecReader_ReadS32(reader, &value->frameCounter) &&
	       NativeCodecReader_ReadS32(reader, &value->timer) && NativeCodecReader_ReadS32(reader, &value->framesInThisLEV) &&
	       NativeCodecReader_ReadS32(reader, &value->elapsedTimeMS) && NativeCodecReader_ReadS32(reader, &value->msInThisLEV) &&
	       NativeCodecReader_ReadS32(reader, &value->elapsedEventTime) && NativeCodecReader_ReadS32(reader, &value->mainGameState) &&
	       NativeCodecReader_ReadS32(reader, &value->loadingStage) && NativeCodecReader_ReadS32(reader, &value->levelID) &&
	       NativeCodecReader_ReadU32(reader, &value->mixRandomNumber) && NativeCodecReader_ReadU32(reader, &value->audioRNG) &&
	       NativeCodecReader_ReadU32(reader, &value->deadcoed0) && NativeCodecReader_ReadU32(reader, &value->deadcoed1) &&
	       NativeCodecReader_ReadU32(reader, &value->advRng0) && NativeCodecReader_ReadU32(reader, &value->advRng1);
}

static int NativeReplayV2_EncodePad(struct NativeCodecWriter *writer, const struct NativeReplayV2Pad *pad)
{
	return NativeCodecWriter_WriteU8(writer, pad->status) && NativeCodecWriter_WriteU8(writer, pad->id) &&
	       NativeCodecWriter_WriteU8(writer, pad->buttons[0]) && NativeCodecWriter_WriteU8(writer, pad->buttons[1]) &&
	       NativeCodecWriter_WriteU8(writer, pad->analog[0]) && NativeCodecWriter_WriteU8(writer, pad->analog[1]) &&
	       NativeCodecWriter_WriteU8(writer, pad->analog[2]) && NativeCodecWriter_WriteU8(writer, pad->analog[3]) &&
	       NativeCodecWriter_WriteU8(writer, pad->connected);
}

static int NativeReplayV2_DecodePad(struct NativeCodecReader *reader, struct NativeReplayV2Pad *pad)
{
	return NativeCodecReader_ReadU8(reader, &pad->status) && NativeCodecReader_ReadU8(reader, &pad->id) &&
	       NativeCodecReader_ReadU8(reader, &pad->buttons[0]) && NativeCodecReader_ReadU8(reader, &pad->buttons[1]) &&
	       NativeCodecReader_ReadU8(reader, &pad->analog[0]) && NativeCodecReader_ReadU8(reader, &pad->analog[1]) &&
	       NativeCodecReader_ReadU8(reader, &pad->analog[2]) && NativeCodecReader_ReadU8(reader, &pad->analog[3]) &&
	       NativeCodecReader_ReadU8(reader, &pad->connected);
}

void NativeReplayV2Header_Init(struct NativeReplayV2Header *header)
{
	if (header != NULL)
	{
		memset(header, 0, sizeof(*header));
	}
}

int NativeReplayV2Header_Validate(const struct NativeReplayV2Header *header)
{
	return (header != NULL) && ((header->flags & ~NATIVE_REPLAY_V2_HEADER_KNOWN_FLAGS) == 0);
}

size_t NativeReplayV2Header_EncodedSize(void) { return NATIVE_REPLAY_V2_HEADER_BYTES; }
size_t NativeReplayV2Frame_EncodedSize(void) { return NATIVE_REPLAY_V2_FRAME_BYTES; }

int NativeReplayV2_StreamSize(const struct NativeReplayV2Header *header, size_t *sizeOut)
{
	if (!NativeReplayV2Header_Validate(header) || (sizeOut == NULL) || (header->frameCount > (SIZE_MAX - NATIVE_REPLAY_V2_HEADER_BYTES) / NATIVE_REPLAY_V2_FRAME_BYTES))
	{
		return 0;
	}
	*sizeOut = NATIVE_REPLAY_V2_HEADER_BYTES + (size_t)header->frameCount * NATIVE_REPLAY_V2_FRAME_BYTES;
	return 1;
}

int NativeReplayV2Header_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV2Header *header)
{
	struct NativeCodecWriter encoded;

	if (!NativeReplayV2Header_Validate(header) || !NativeReplayV2_WriterReady(writer, NATIVE_REPLAY_V2_HEADER_BYTES)) return 0;
	encoded = *writer;
	if (!NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_FILE_MAGIC) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_FORMAT_VERSION) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_HEADER_BYTES) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_FRAME_BYTES) ||
	    !NativeCodecWriter_WriteU32(&encoded, header->flags) || !NativeCodecWriter_WriteU32(&encoded, header->frameCount) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_CANONICAL_STATE_SCHEMA_VERSION_V2) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_CANONICAL_REPLAY_FORMAT_VERSION_V2) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_CANONICAL_DOMAIN_COUNT) ||
	    !NativeCodecWriter_WriteU32(&encoded, (uint32_t)NativeCanonicalStateV1_EncodedSize()) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_PROFILE_NTSC_U) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_TICK_RATE) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_ELAPSED_TICK_MS) ||
	    !NativeCodecWriter_WriteU64(&encoded, NATIVE_REPLAY_V2_VBLANK_CYCLES) || !NativeCodecWriter_WriteU64(&encoded, NATIVE_REPLAY_V2_GPU_CLOCK_HZ) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_PAD_COUNT) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS) ||
	    !NativeCodecWriter_WriteBytes(&encoded, header->identity.build, NATIVE_IDENTITY_DIGEST_BYTES) ||
	    !NativeCodecWriter_WriteBytes(&encoded, header->identity.content, NATIVE_IDENTITY_DIGEST_BYTES)) return 0;
	*writer = encoded;
	return 1;
}

int NativeReplayV2Header_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity, struct NativeReplayV2Header *header)
{
	struct NativeCodecReader encoded;
	struct NativeReplayV2Header decoded;
	uint32_t values[15];
	uint64_t vblankCycles;
	uint64_t gpuClock;

	if ((expectedIdentity == NULL) || (header == NULL) || !NativeReplayV2_ReaderReady(reader, NATIVE_REPLAY_V2_HEADER_BYTES)) return 0;
	encoded = *reader;
	for (uint32_t i = 0; i < 13; i++) if (!NativeCodecReader_ReadU32(&encoded, &values[i])) return 0;
	if (!NativeCodecReader_ReadU64(&encoded, &vblankCycles) || !NativeCodecReader_ReadU64(&encoded, &gpuClock) ||
	    !NativeCodecReader_ReadU32(&encoded, &values[13]) || !NativeCodecReader_ReadU32(&encoded, &values[14]) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.identity.build, NATIVE_IDENTITY_DIGEST_BYTES) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.identity.content, NATIVE_IDENTITY_DIGEST_BYTES)) return 0;
	decoded.flags = values[4];
	decoded.frameCount = values[5];
	if ((values[0] != NATIVE_REPLAY_V2_FILE_MAGIC) || (values[1] != NATIVE_REPLAY_V2_FORMAT_VERSION) || (values[2] != NATIVE_REPLAY_V2_HEADER_BYTES) ||
	    (values[3] != NATIVE_REPLAY_V2_FRAME_BYTES) || ((values[4] & ~NATIVE_REPLAY_V2_HEADER_KNOWN_FLAGS) != 0) ||
	    (values[6] != NATIVE_CANONICAL_STATE_SCHEMA_VERSION_V2) || (values[7] != NATIVE_CANONICAL_REPLAY_FORMAT_VERSION_V2) ||
	    (values[8] != NATIVE_CANONICAL_DOMAIN_COUNT) || (values[9] != NativeCanonicalStateV1_EncodedSize()) ||
	    (values[10] != NATIVE_REPLAY_V2_PROFILE_NTSC_U) || (values[11] != NATIVE_REPLAY_V2_TICK_RATE) ||
	    (values[12] != NATIVE_REPLAY_V2_ELAPSED_TICK_MS) ||
	    (vblankCycles != NATIVE_REPLAY_V2_VBLANK_CYCLES) || (gpuClock != NATIVE_REPLAY_V2_GPU_CLOCK_HZ) ||
	    (values[13] != NATIVE_REPLAY_V2_PAD_COUNT) || (values[14] != NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS) ||
	    !NativeReplayV2_IdentityEquals(&decoded.identity, expectedIdentity)) return 0;
	*header = decoded;
	*reader = encoded;
	return 1;
}

int NativeReplayV2Frame_Validate(const struct NativeReplayV2Header *header, const struct NativeReplayV2Frame *frame)
{
	uint32_t total = 0;
	uint8_t canonicalBytes[NATIVE_REPLAY_V2_FRAME_BYTES];
	struct NativeCodecWriter canonicalWriter;
	if (!NativeReplayV2Header_Validate(header) || (frame == NULL) || (frame->padCount != NATIVE_REPLAY_V2_PAD_COUNT) ||
	    (frame->vsyncPacketCount > NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS) || !NativeCanonicalStateV1_Validate(&frame->canonical) ||
	    !NativeReplayV2_IdentityEquals(&frame->canonical.identity, &header->identity) || (frame->canonical.frameNumber != frame->replayFrame)) return 0;
	for (uint32_t i = 0; i < frame->vsyncPacketCount; i++)
	{
		if (frame->vsyncPackets[i] == 0) return 0;
		total += frame->vsyncPackets[i];
	}
	for (uint32_t i = frame->vsyncPacketCount; i < NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS; i++)
	{
		if (frame->vsyncPackets[i] != 0) return 0;
	}
	if (total != frame->vsyncTotal) return 0;
	NativeCodecWriter_Init(&canonicalWriter, canonicalBytes, NativeCanonicalStateV1_EncodedSize(), NULL);
	return NativeCanonicalStateV1_Encode(&canonicalWriter, &frame->canonical) &&
	       (NativeCodecWriter_Size(&canonicalWriter) == NativeCanonicalStateV1_EncodedSize());
}

int NativeReplayV2Frame_Encode(struct NativeCodecWriter *writer, const struct NativeReplayV2Header *header, const struct NativeReplayV2Frame *frame)
{
	struct NativeCodecWriter encoded;
	if (!NativeReplayV2Frame_Validate(header, frame) || !NativeReplayV2_WriterReady(writer, NATIVE_REPLAY_V2_FRAME_BYTES)) return 0;
	encoded = *writer;
	if (!NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_FRAME_MAGIC) || !NativeCodecWriter_WriteU32(&encoded, NATIVE_REPLAY_V2_FRAME_BYTES) ||
	    !NativeCodecWriter_WriteU32(&encoded, frame->replayFrame) || !NativeReplayV2_EncodeObservation(&encoded, &frame->begin) ||
	    !NativeReplayV2_EncodeObservation(&encoded, &frame->end) || !NativeCodecWriter_WriteU32(&encoded, frame->padCount)) return 0;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_PAD_COUNT; i++) if (!NativeReplayV2_EncodePad(&encoded, &frame->pads[i])) return 0;
	if (!NativeCodecWriter_WriteU32(&encoded, frame->vsyncTotal) || !NativeCodecWriter_WriteU32(&encoded, frame->vsyncPacketCount)) return 0;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS; i++) if (!NativeCodecWriter_WriteU16(&encoded, frame->vsyncPackets[i])) return 0;
	if (!NativeCodecWriter_WriteU32(&encoded, (uint32_t)NativeCanonicalStateV1_EncodedSize()) || !NativeCanonicalStateV1_Encode(&encoded, &frame->canonical)) return 0;
	*writer = encoded;
	return 1;
}

int NativeReplayV2Frame_Decode(struct NativeCodecReader *reader, const struct NativeReplayV2Header *header, const struct NativeIdentityV1 *expectedIdentity,
                               struct NativeReplayV2Frame *frame)
{
	struct NativeCodecReader encoded;
	struct NativeReplayV2Frame decoded;
	uint32_t magic, recordSize, canonicalSize;
	if ((expectedIdentity == NULL) || (frame == NULL) || !NativeReplayV2Header_Validate(header) ||
	    !NativeReplayV2_IdentityEquals(&header->identity, expectedIdentity) || !NativeReplayV2_ReaderReady(reader, NATIVE_REPLAY_V2_FRAME_BYTES)) return 0;
	memset(&decoded, 0, sizeof(decoded)); encoded = *reader;
	if (!NativeCodecReader_ReadU32(&encoded, &magic) || !NativeCodecReader_ReadU32(&encoded, &recordSize) || !NativeCodecReader_ReadU32(&encoded, &decoded.replayFrame) ||
	    (magic != NATIVE_REPLAY_V2_FRAME_MAGIC) || (recordSize != NATIVE_REPLAY_V2_FRAME_BYTES) || !NativeReplayV2_DecodeObservation(&encoded, &decoded.begin) ||
	    !NativeReplayV2_DecodeObservation(&encoded, &decoded.end) || !NativeCodecReader_ReadU32(&encoded, &decoded.padCount)) return 0;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_PAD_COUNT; i++) if (!NativeReplayV2_DecodePad(&encoded, &decoded.pads[i])) return 0;
	if (!NativeCodecReader_ReadU32(&encoded, &decoded.vsyncTotal) || !NativeCodecReader_ReadU32(&encoded, &decoded.vsyncPacketCount)) return 0;
	for (uint32_t i = 0; i < NATIVE_REPLAY_V2_MAX_VSYNC_PACKETS; i++) if (!NativeCodecReader_ReadU16(&encoded, &decoded.vsyncPackets[i])) return 0;
	if (!NativeCodecReader_ReadU32(&encoded, &canonicalSize) || (canonicalSize != NativeCanonicalStateV1_EncodedSize()) ||
	    !NativeCanonicalStateV1_Decode(&encoded, expectedIdentity, &decoded.canonical) || !NativeReplayV2Frame_Validate(header, &decoded)) return 0;
	*frame = decoded; *reader = encoded;
	return 1;
}
