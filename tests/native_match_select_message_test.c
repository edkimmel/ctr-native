#include "platform/native_match_select_message.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define MESSAGE_BYTES NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES
#define BODY_BYTES NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET

/*
 * Frozen wire format. These 64 bytes are written out by hand from the offset
 * table in native_match_select_message.h, not produced by the encoder. The
 * trailer is FNV-1a 64 (offset basis 0xcbf29ce484222325, prime
 * 0x100000001b3) over bytes 0..55: 0x3e8bef6a6091637f, stored little-endian
 * at offset 56.
 *
 * How the trailer was computed: out of band, not with this repository's
 * codec, by a standalone Windows PowerShell FNV-1a 64 script. It starts from
 * h = 0xcbf29ce484222325 as a System.Numerics.BigInteger and, for each byte
 * b, sets h = ((h -bxor b) * 0x100000001b3) -band (2^64 - 1); BigInteger
 * avoids PowerShell's silent promotion of an overflowing uint64 product to
 * double. The script first self-checks against the published FNV-1a 64
 * vector for the ASCII string "a": 0xaf63dc4c8601ec8c. It then hashes the
 * 56 body bytes below, typed in from the offset table (offsets 0..55), and
 * prints 0x3e8bef6a6091637f.
 *
 * A change to any byte here is a wire format change.
 */
static const uint8_t k_goldenBytes[MESSAGE_BYTES] = {
	/* 0: magic 0x31534d4e "NMS1" */ 0x4Eu, 0x4Du, 0x53u, 0x31u,
	/* 4: messageVersion 1 */ 0x01u, 0x00u,
	/* 6: encodedSize 64 */ 0x40u, 0x00u,
	/* 8: senderHuman 1 */ 0x01u,
	/* 9: humanCount 2 */ 0x02u,
	/* 10: phase RESOLVED */ 0x01u,
	/* 11: lockMask character|track|laps */ 0x07u,
	/* 12: sequence 0x01020304 */ 0x04u, 0x03u, 0x02u, 0x01u,
	/* 16: baseDigest */ 0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
	/* 24: nonce 0x8877665544332211 */ 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u,
	/* 32: characterID 5 */ 0x05u,
	/* 33: trackID 14 */ 0x0Eu,
	/* 34: lapCount 7 */ 0x07u,
	/* 35: currentItem DONE */ 0x03u,
	/* 36: reserved0 */ 0x00u, 0x00u, 0x00u, 0x00u,
	/* 40: resolvedDigest */ 0xA0u, 0xA1u, 0xA2u, 0xA3u, 0xA4u, 0xA5u, 0xA6u, 0xA7u,
	/* 48: reserved1 */ 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
	/* 56: FNV-1a 64 over bytes 0..55 */ 0x7Fu, 0x63u, 0x91u, 0x60u, 0x6Au, 0xEFu, 0x8Bu, 0x3Eu
};

/* The message the golden bytes encode. */
static void MakeGolden(struct NativeMatchSelectMessageV1 *message)
{
	memset(message, 0, sizeof(*message));
	message->senderHuman = 1;
	message->humanCount = 2;
	message->phase = NATIVE_MATCH_SELECT_PHASE_RESOLVED;
	message->lockMask = NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK | NATIVE_MATCH_SELECT_LOCK_LAPS;
	message->sequence = UINT32_C(0x01020304);
	for (uint8_t i = 0; i < NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES; i++)
	{
		message->baseDigest[i] = (uint8_t)(0x10u + i);
	}
	message->nonce = UINT64_C(0x8877665544332211);
	message->characterID = 5;
	message->trackID = 14;
	message->lapCount = 7;
	message->currentItem = NATIVE_MATCH_SELECT_ITEM_DONE;
	for (uint8_t i = 0; i < NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES; i++)
	{
		message->resolvedDigest[i] = (uint8_t)(0xA0u + i);
	}
}

/* A PICKING message on the track item: character locked, track and laps cursors. */
static void MakePicking(struct NativeMatchSelectMessageV1 *message)
{
	memset(message, 0, sizeof(*message));
	message->senderHuman = 0;
	message->humanCount = 2;
	message->phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
	message->lockMask = NATIVE_MATCH_SELECT_LOCK_CHARACTER;
	message->sequence = 17;
	for (uint8_t i = 0; i < NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES; i++)
	{
		message->baseDigest[i] = (uint8_t)(0xF0u - i);
	}
	message->nonce = UINT64_C(0xfedcba9876543210);
	message->characterID = 2;
	message->trackID = 3;
	message->lapCount = 3;
	message->currentItem = NATIVE_MATCH_SELECT_ITEM_TRACK;
}

static void WriteLe(uint8_t *bytes, uint64_t value, uint32_t size)
{
	for (uint32_t i = 0; i < size; i++)
	{
		bytes[i] = (uint8_t)(value >> (8u * i));
	}
}

/* Recomputes the FNV-1a 64 body digest so a patched record is well sealed. */
static void Reseal(uint8_t *bytes)
{
	struct NativeCodecDigest64 digest;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, BODY_BYTES);
	WriteLe(&bytes[BODY_BYTES], digest.value, 8u);
}

/*
 * Serializes any message, valid or not, by the offset table with no shape
 * check, then seals it. Used to craft records the encoder refuses to write.
 */
static void RawEncode(const struct NativeMatchSelectMessageV1 *message, uint8_t bytes[MESSAGE_BYTES])
{
	memset(bytes, 0, MESSAGE_BYTES);
	WriteLe(&bytes[0], NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC, 4u);
	WriteLe(&bytes[4], NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION, 2u);
	WriteLe(&bytes[6], NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES, 2u);
	bytes[8] = message->senderHuman;
	bytes[9] = message->humanCount;
	bytes[10] = message->phase;
	bytes[11] = message->lockMask;
	WriteLe(&bytes[12], message->sequence, 4u);
	memcpy(&bytes[16], message->baseDigest, 8u);
	WriteLe(&bytes[24], message->nonce, 8u);
	bytes[32] = message->characterID;
	bytes[33] = message->trackID;
	bytes[34] = message->lapCount;
	bytes[35] = message->currentItem;
	memcpy(&bytes[36], message->reserved0, 4u);
	memcpy(&bytes[40], message->resolvedDigest, 8u);
	memcpy(&bytes[48], message->reserved1, 8u);
	Reseal(bytes);
}

static int EncodeMessage(const struct NativeMatchSelectMessageV1 *message, uint8_t bytes[MESSAGE_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, MESSAGE_BYTES, NULL);
	CHECK(NativeMatchSelectMessageV1_Encode(&writer, message));
	CHECK(NativeCodecWriter_Size(&writer) == MESSAGE_BYTES);
	return 0;
}

static int SameMessage(const struct NativeMatchSelectMessageV1 *a, const struct NativeMatchSelectMessageV1 *b)
{
	CHECK(a->senderHuman == b->senderHuman);
	CHECK(a->humanCount == b->humanCount);
	CHECK(a->phase == b->phase);
	CHECK(a->lockMask == b->lockMask);
	CHECK(a->sequence == b->sequence);
	CHECK(memcmp(a->baseDigest, b->baseDigest, sizeof(a->baseDigest)) == 0);
	CHECK(a->nonce == b->nonce);
	CHECK(a->characterID == b->characterID);
	CHECK(a->trackID == b->trackID);
	CHECK(a->lapCount == b->lapCount);
	CHECK(a->currentItem == b->currentItem);
	CHECK(memcmp(a->reserved0, b->reserved0, sizeof(a->reserved0)) == 0);
	CHECK(memcmp(a->resolvedDigest, b->resolvedDigest, sizeof(a->resolvedDigest)) == 0);
	CHECK(memcmp(a->reserved1, b->reserved1, sizeof(a->reserved1)) == 0);
	return 0;
}

static int DecodeOk(const uint8_t *bytes, const struct NativeMatchSelectMessageV1 *expected)
{
	struct NativeCodecReader reader;
	struct NativeMatchSelectMessageV1 out;
	uint32_t cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC;

	memset(&out, 0xCD, sizeof(out));
	NativeCodecReader_Init(&reader, bytes, MESSAGE_BYTES);
	CHECK(NativeMatchSelectMessageV1_Decode(&reader, &out, &cause));
	CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE);
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	CHECK(SameMessage(&out, expected) == 0);
	/* A NULL fault sink is accepted. */
	NativeCodecReader_Init(&reader, bytes, MESSAGE_BYTES);
	CHECK(NativeMatchSelectMessageV1_Decode(&reader, &out, NULL));
	return 0;
}

/* Decode must fail with exactly the expected cause and leave both the
 * caller's reader and the output message untouched. */
static int DecodeFails(const uint8_t *bytes, size_t size, uint32_t expectedCause)
{
	struct NativeCodecReader reader;
	struct NativeCodecReader readerBefore;
	struct NativeMatchSelectMessageV1 out;
	struct NativeMatchSelectMessageV1 outBefore;
	uint32_t cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE;

	memset(&out, 0xCD, sizeof(out));
	outBefore = out;
	NativeCodecReader_Init(&reader, bytes, size);
	readerBefore = reader;
	CHECK(!NativeMatchSelectMessageV1_Decode(&reader, &out, &cause));
	CHECK(cause == expectedCause);
	CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
	CHECK(memcmp(&out, &outBefore, sizeof(out)) == 0);
	NativeCodecReader_Init(&reader, bytes, size);
	CHECK(!NativeMatchSelectMessageV1_Decode(&reader, &out, NULL));
	CHECK(memcmp(&out, &outBefore, sizeof(out)) == 0);
	return 0;
}

/* Encode must refuse and leave the writer and its buffer untouched. */
static int EncodeFails(struct NativeCodecWriter *writer, const struct NativeMatchSelectMessageV1 *message)
{
	struct NativeCodecWriter before = *writer;
	uint8_t bufferBefore[MESSAGE_BYTES + 8u];
	size_t span = writer->capacity < sizeof(bufferBefore) ? writer->capacity : sizeof(bufferBefore);

	if (writer->data != NULL)
	{
		memcpy(bufferBefore, writer->data, span);
	}
	CHECK(!NativeMatchSelectMessageV1_Encode(writer, message));
	CHECK(memcmp(writer, &before, sizeof(before)) == 0);
	if (writer->data != NULL)
	{
		CHECK(memcmp(bufferBefore, writer->data, span) == 0);
	}
	return 0;
}

/* One shape fault: ShapeCause reports it, Encode refuses it, and a sealed
 * raw record carrying it decodes to exactly that cause. */
static int ShapeFault(const struct NativeMatchSelectMessageV1 *message, uint32_t expectedCause)
{
	uint8_t buffer[MESSAGE_BYTES];
	uint8_t bytes[MESSAGE_BYTES];
	struct NativeCodecWriter writer;

	CHECK(NativeMatchSelectMessageV1_ShapeCause(message) == expectedCause);
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	CHECK(EncodeFails(&writer, message) == 0);
	RawEncode(message, bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), expectedCause) == 0);
	return 0;
}

/* A valid message: ShapeCause NONE, and it round-trips. */
static int ShapeOk(const struct NativeMatchSelectMessageV1 *message)
{
	uint8_t bytes[MESSAGE_BYTES];
	uint8_t raw[MESSAGE_BYTES];

	CHECK(NativeMatchSelectMessageV1_ShapeCause(message) == NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE);
	CHECK(EncodeMessage(message, bytes) == 0);
	RawEncode(message, raw);
	CHECK(memcmp(bytes, raw, sizeof(bytes)) == 0);
	CHECK(DecodeOk(bytes, message) == 0);
	return 0;
}

static int TestGolden(void)
{
	struct NativeMatchSelectMessageV1 golden;
	uint8_t bytes[MESSAGE_BYTES];
	uint8_t raw[MESSAGE_BYTES];

	CHECK(NativeMatchSelectMessageV1_EncodedSize() == MESSAGE_BYTES);
	CHECK(MESSAGE_BYTES == 64u);
	CHECK(BODY_BYTES == 56u);
	CHECK(NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC == UINT32_C(0x31534d4e));
	CHECK(NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION == 1u);

	MakeGolden(&golden);
	memset(bytes, 0xEE, sizeof(bytes));
	CHECK(EncodeMessage(&golden, bytes) == 0);
	/* Frozen wire format: byte for byte. */
	CHECK(memcmp(bytes, k_goldenBytes, sizeof(bytes)) == 0);
	CHECK(DecodeOk(k_goldenBytes, &golden) == 0);

	/* The test's own raw serializer agrees with the frozen bytes, so the
	 * crafted fault records below use the same layout. */
	RawEncode(&golden, raw);
	CHECK(memcmp(raw, k_goldenBytes, sizeof(raw)) == 0);
	return 0;
}

static int TestRoundTrip(void)
{
	struct NativeMatchSelectMessageV1 message;

	MakePicking(&message);
	CHECK(ShapeOk(&message) == 0);
	MakeGolden(&message);
	CHECK(ShapeOk(&message) == 0);

	/* A RESOLVED message with an all-zero resolvedDigest is well-shaped:
	 * the digest bytes are opaque once RESOLVED. */
	memset(message.resolvedDigest, 0, sizeof(message.resolvedDigest));
	CHECK(ShapeOk(&message) == 0);

	/* Every item with its matching lockMask, while PICKING. */
	for (uint8_t item = 0; item <= NATIVE_MATCH_SELECT_ITEM_DONE; item++)
	{
		MakePicking(&message);
		message.currentItem = item;
		message.lockMask = (uint8_t)((1u << item) - 1u);
		CHECK(ShapeOk(&message) == 0);
	}

	/* Four humans, the highest sender; one human, sender 0. */
	MakePicking(&message);
	message.humanCount = 4;
	message.senderHuman = 3;
	CHECK(ShapeOk(&message) == 0);
	message.humanCount = 1;
	message.senderHuman = 0;
	CHECK(ShapeOk(&message) == 0);

	/* Sequence bounds. */
	MakePicking(&message);
	message.sequence = UINT32_MAX;
	CHECK(ShapeOk(&message) == 0);
	message.sequence = 1;
	CHECK(ShapeOk(&message) == 0);

	/* baseDigest and nonce are opaque. */
	MakePicking(&message);
	memset(message.baseDigest, 0, sizeof(message.baseDigest));
	message.nonce = 0;
	CHECK(ShapeOk(&message) == 0);
	memset(message.baseDigest, 0xFF, sizeof(message.baseDigest));
	message.nonce = UINT64_MAX;
	CHECK(ShapeOk(&message) == 0);

	/* Every table entry. */
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_CHARACTER_COUNT; i++)
	{
		MakePicking(&message);
		message.characterID = NativeMatchSelect_CharacterAt(i);
		CHECK(ShapeOk(&message) == 0);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_TRACK_COUNT; i++)
	{
		MakePicking(&message);
		message.trackID = NativeMatchSelect_TrackAt(i);
		CHECK(ShapeOk(&message) == 0);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_LAP_OPTION_COUNT; i++)
	{
		MakePicking(&message);
		message.lapCount = NativeMatchSelect_LapOptionAt(i);
		CHECK(ShapeOk(&message) == 0);
	}
	return 0;
}

/* Wire-level faults before the shape checks: size, magic, version, encoded
 * size, digest. */
static int TestWireFaults(void)
{
	uint8_t bytes[MESSAGE_BYTES];
	uint8_t longer[MESSAGE_BYTES + 1u];

	/* Remaining length other than 64. */
	CHECK(DecodeFails(k_goldenBytes, 63u, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE) == 0);
	CHECK(DecodeFails(k_goldenBytes, 0u, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE) == 0);
	for (size_t size = 1; size < MESSAGE_BYTES; size++)
	{
		CHECK(DecodeFails(k_goldenBytes, size, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE) == 0);
	}
	memcpy(longer, k_goldenBytes, MESSAGE_BYTES);
	longer[MESSAGE_BYTES] = 0;
	CHECK(DecodeFails(longer, sizeof(longer), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE) == 0);

	/* NULL arguments and a failed reader are size faults and touch nothing. */
	{
		struct NativeCodecReader reader;
		struct NativeMatchSelectMessageV1 message;
		struct NativeMatchSelectMessageV1 messageBefore;
		uint32_t cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE;

		memset(&message, 0xCD, sizeof(message));
		messageBefore = message;
		CHECK(!NativeMatchSelectMessageV1_Decode(NULL, &message, &cause));
		CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE);
		CHECK(memcmp(&message, &messageBefore, sizeof(message)) == 0);
		CHECK(!NativeMatchSelectMessageV1_Decode(NULL, NULL, NULL));

		NativeCodecReader_Init(&reader, k_goldenBytes, MESSAGE_BYTES);
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE;
		CHECK(!NativeMatchSelectMessageV1_Decode(&reader, NULL, &cause));
		CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE);
		CHECK(NativeCodecReader_Remaining(&reader) == MESSAGE_BYTES);

		NativeCodecReader_Init(&reader, k_goldenBytes, MESSAGE_BYTES);
		reader.failed = 1;
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE;
		CHECK(!NativeMatchSelectMessageV1_Decode(&reader, &message, &cause));
		CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE);
		CHECK(memcmp(&message, &messageBefore, sizeof(message)) == 0);
		CHECK(reader.offset == 0);
	}

	/*
	 * A hand-built reader with NULL data that is not marked failed, at offset
	 * 8 of a claimed 72 bytes: exactly 64 remain, so the up-front size check
	 * passes, and Decode must still never form or follow a pointer from it.
	 * The field reads refuse it, so it is a size fault, with the reader and
	 * message untouched.
	 */
	{
		struct NativeCodecReader reader;
		struct NativeCodecReader readerBefore;
		struct NativeMatchSelectMessageV1 message;
		struct NativeMatchSelectMessageV1 messageBefore;
		uint32_t cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE;

		memset(&reader, 0, sizeof(reader));
		reader.data = NULL;
		reader.failed = 0;
		reader.offset = 8u;
		reader.size = 8u + MESSAGE_BYTES;
		CHECK(NativeCodecReader_Ok(&reader));
		CHECK(NativeCodecReader_Remaining(&reader) == MESSAGE_BYTES);
		readerBefore = reader;
		memset(&message, 0xCD, sizeof(message));
		messageBefore = message;
		CHECK(!NativeMatchSelectMessageV1_Decode(&reader, &message, &cause));
		CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE);
		CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
		CHECK(memcmp(&message, &messageBefore, sizeof(message)) == 0);
		CHECK(!NativeMatchSelectMessageV1_Decode(&reader, &message, NULL));
		CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
	}

	/* Magic, version (either byte), encoded size (either byte), each resealed
	 * so the check under test is the one that fires. */
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[3] = 0x32u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC) == 0);
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[4] = 0x02u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_VERSION) == 0);
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[5] = 0x01u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_VERSION) == 0);
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[6] = 0x41u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ENCODED_SIZE) == 0);
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[7] = 0x01u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ENCODED_SIZE) == 0);

	/* A wrong trailer, and a body byte changed under a stale trailer. */
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[MESSAGE_BYTES - 1u] ^= 0x80u;
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST) == 0);
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[24] ^= 0x01u; /* nonce: opaque, so only the digest can catch it */
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST) == 0);

	/* Fixed order: magic is reported before a bad version, size, or digest. */
	memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
	bytes[0] = 0u;
	bytes[4] = 9u;
	bytes[6] = 9u;
	bytes[MESSAGE_BYTES - 1u] ^= 0x01u;
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC) == 0);
	bytes[0] = k_goldenBytes[0];
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_VERSION) == 0);
	bytes[4] = k_goldenBytes[4];
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ENCODED_SIZE) == 0);
	bytes[6] = k_goldenBytes[6];
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST) == 0);
	return 0;
}

/*
 * Every single-byte change of a valid record is rejected: bytes 0..3 by the
 * magic check, 4..5 by the version check, 6..7 by the encoded-size check,
 * and everything from 8 on by the digest (FNV-1a is a bijection per input
 * byte, so one changed body byte always changes the trailer).
 */
static int TestEverySingleByteFlip(void)
{
	uint8_t bytes[MESSAGE_BYTES];

	for (uint32_t offset = 0; offset < MESSAGE_BYTES; offset++)
	{
		uint32_t expected = offset < 4u   ? NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC
		                    : offset < 6u ? NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_VERSION
		                    : offset < 8u ? NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ENCODED_SIZE
		                                  : NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST;

		for (uint32_t mask = 1; mask <= 0xFFu; mask++)
		{
			memcpy(bytes, k_goldenBytes, MESSAGE_BYTES);
			bytes[offset] ^= (uint8_t)mask;
			CHECK(DecodeFails(bytes, sizeof(bytes), expected) == 0);
		}
	}
	return 0;
}

/* Each shape fault cause, one at a time, from a valid message. */
static int TestShapeFaults(void)
{
	struct NativeMatchSelectMessageV1 message;

	/* BAD_RESERVED: every reserved0 and reserved1 byte. */
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED0_BYTES; i++)
	{
		MakeGolden(&message);
		message.reserved0[i] = 0x01u;
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESERVED) == 0);
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED1_BYTES; i++)
	{
		MakeGolden(&message);
		message.reserved1[i] = 0x80u;
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESERVED) == 0);
	}

	/* BAD_HUMAN_COUNT: 0 and above the maximum. */
	MakeGolden(&message);
	message.humanCount = 0;
	message.senderHuman = 0;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_HUMAN_COUNT) == 0);
	message.humanCount = (uint8_t)(NATIVE_MATCH_SELECT_MAX_HUMANS + 1u);
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_HUMAN_COUNT) == 0);
	message.humanCount = 0xFFu;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_HUMAN_COUNT) == 0);

	/* BAD_SENDER: senderHuman == humanCount, and far above it. */
	MakeGolden(&message);
	message.senderHuman = 2;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SENDER) == 0);
	message.humanCount = 4;
	message.senderHuman = 4;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SENDER) == 0);
	message.senderHuman = 0xFFu;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SENDER) == 0);

	/* BAD_PHASE. */
	MakeGolden(&message);
	message.phase = 2;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_PHASE) == 0);
	message.phase = 0xFFu;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_PHASE) == 0);

	/* BAD_LOCK_MASK: any bit above the three item bits. */
	for (uint32_t bit = 3; bit < 8u; bit++)
	{
		MakeGolden(&message);
		message.lockMask = (uint8_t)(message.lockMask | (1u << bit));
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LOCK_MASK) == 0);
	}

	/* BAD_SEQUENCE. */
	MakeGolden(&message);
	message.sequence = 0;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SEQUENCE) == 0);

	/* BAD_CHARACTER: the save-unlock characters, NITROS_OXIDE, and 0xff. */
	for (uint32_t id = NATIVE_MATCH_SELECT_CHARACTER_COUNT; id <= 0xFFu; id++)
	{
		MakeGolden(&message);
		message.characterID = (uint8_t)id;
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_CHARACTER) == 0);
	}

	/* BAD_TRACK: every byte value outside the table (Oxide Station 13 and
	 * Turbo Track 17 included). */
	for (uint32_t id = 0; id <= 0xFFu; id++)
	{
		uint32_t index = 0;

		if (NativeMatchSelect_TrackIndex((uint8_t)id, &index))
		{
			continue;
		}
		MakeGolden(&message);
		message.trackID = (uint8_t)id;
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_TRACK) == 0);
	}
	{
		uint32_t index = 0;

		CHECK(!NativeMatchSelect_TrackIndex(13u, &index));
		CHECK(!NativeMatchSelect_TrackIndex(17u, &index));
	}

	/* BAD_LAPS: every byte value other than 3, 5, 7. */
	for (uint32_t laps = 0; laps <= 0xFFu; laps++)
	{
		if ((laps == 3u) || (laps == 5u) || (laps == 7u))
		{
			continue;
		}
		MakeGolden(&message);
		message.lapCount = (uint8_t)laps;
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LAPS) == 0);
	}

	/* BAD_ITEM: currentItem out of range, whatever the lockMask. */
	for (uint32_t item = NATIVE_MATCH_SELECT_ITEM_DONE + 1u; item <= 0xFFu; item++)
	{
		for (uint8_t mask = 0; mask <= 7u; mask++)
		{
			MakeGolden(&message);
			message.currentItem = (uint8_t)item;
			message.lockMask = mask;
			CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM) == 0);
		}
	}

	/* BAD_ITEM_LOCK_MISMATCH: every in-range currentItem with every other
	 * lockMask, in both phases. */
	for (uint8_t item = 0; item <= NATIVE_MATCH_SELECT_ITEM_DONE; item++)
	{
		for (uint8_t mask = 0; mask <= 7u; mask++)
		{
			if (mask == (uint8_t)((1u << item) - 1u))
			{
				continue;
			}
			MakePicking(&message);
			message.currentItem = item;
			message.lockMask = mask;
			CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM_LOCK_MISMATCH) == 0);
			MakeGolden(&message);
			message.currentItem = item;
			message.lockMask = mask;
			CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM_LOCK_MISMATCH) == 0);
		}
	}

	/* BAD_RESOLVED_DIGEST: PICKING with any nonzero resolvedDigest byte, at
	 * every item. */
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES; i++)
	{
		MakePicking(&message);
		message.resolvedDigest[i] = 0x01u;
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_DIGEST) == 0);
	}
	MakeGolden(&message);
	message.phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_DIGEST) == 0);
	for (uint8_t item = 0; item < NATIVE_MATCH_SELECT_ITEM_DONE; item++)
	{
		MakeGolden(&message);
		message.phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
		message.currentItem = item;
		message.lockMask = (uint8_t)((1u << item) - 1u);
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_DIGEST) == 0);
	}

	/* BAD_RESOLVED_ITEM: RESOLVED before DONE (with a consistent lockMask),
	 * whether or not the resolvedDigest is zero. */
	for (uint8_t item = 0; item < NATIVE_MATCH_SELECT_ITEM_DONE; item++)
	{
		MakeGolden(&message);
		message.currentItem = item;
		message.lockMask = (uint8_t)((1u << item) - 1u);
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_ITEM) == 0);
		memset(message.resolvedDigest, 0, sizeof(message.resolvedDigest));
		CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_ITEM) == 0);
	}

	CHECK(NativeMatchSelectMessageV1_ShapeCause(NULL) == NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE);
	return 0;
}

/*
 * The documented check order: a message with every shape fault at once
 * reports each of the thirteen shape causes (6 through 18) exactly once, in
 * check order (not numeric order), as each is repaired in turn. Two causes
 * depend on the phase and cannot coexist: BAD_RESOLVED_DIGEST (PICKING with
 * a digest) is repaired by moving to RESOLVED, which exposes
 * BAD_RESOLVED_ITEM (RESOLVED before DONE).
 */
static int TestShapeFaultOrder(void)
{
	struct NativeMatchSelectMessageV1 message;

	MakeGolden(&message);
	message.reserved1[7] = 1;
	message.humanCount = 0;
	message.senderHuman = 2;
	message.phase = 2;
	message.lockMask = 8;
	message.sequence = 0;
	message.characterID = 8;
	message.trackID = 13;
	message.lapCount = 4;
	message.currentItem = 4;

	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESERVED) == 0);
	message.reserved1[7] = 0;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_HUMAN_COUNT) == 0);
	message.humanCount = 2;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SENDER) == 0);
	message.senderHuman = 1;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_PHASE) == 0);
	message.phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LOCK_MASK) == 0);
	message.lockMask = NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SEQUENCE) == 0);
	message.sequence = 1;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_CHARACTER) == 0);
	message.characterID = 0;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_TRACK) == 0);
	message.trackID = 3;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LAPS) == 0);
	message.lapCount = 3;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM) == 0);
	message.currentItem = NATIVE_MATCH_SELECT_ITEM_TRACK; /* lockMask 0x3 needs LAPS */
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM_LOCK_MISMATCH) == 0);
	message.currentItem = NATIVE_MATCH_SELECT_ITEM_LAPS;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_DIGEST) == 0);
	message.phase = NATIVE_MATCH_SELECT_PHASE_RESOLVED;
	CHECK(ShapeFault(&message, NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_ITEM) == 0);
	message.currentItem = NATIVE_MATCH_SELECT_ITEM_DONE;
	message.lockMask = NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK | NATIVE_MATCH_SELECT_LOCK_LAPS;
	CHECK(ShapeOk(&message) == 0);

	/* The digest is checked before any shape check. */
	{
		uint8_t bytes[MESSAGE_BYTES];

		MakeGolden(&message);
		message.humanCount = 0;
		RawEncode(&message, bytes);
		bytes[BODY_BYTES] ^= 0x01u;
		CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST) == 0);
	}
	return 0;
}

/* Encode and Decode are transactional. */
static int TestTransactional(void)
{
	struct NativeMatchSelectMessageV1 golden;
	struct NativeCodecWriter writer;
	uint8_t buffer[MESSAGE_BYTES + 8u];

	MakeGolden(&golden);

	/* Writer one byte too small. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, MESSAGE_BYTES - 1u, NULL);
	CHECK(EncodeFails(&writer, &golden) == 0);

	/* Writer with room overall but not after its current offset. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, MESSAGE_BYTES + 3u, NULL);
	writer.offset = 4u;
	CHECK(EncodeFails(&writer, &golden) == 0);

	/* A failed writer, and NULL arguments. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.failed = 1;
	CHECK(EncodeFails(&writer, &golden) == 0);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	CHECK(EncodeFails(&writer, NULL) == 0);
	CHECK(!NativeMatchSelectMessageV1_Encode(NULL, &golden));

	/* Appending at an offset writes exactly the record there and advances the
	 * writer by 64. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.offset = 3u;
	CHECK(NativeMatchSelectMessageV1_Encode(&writer, &golden));
	CHECK(writer.offset == 3u + MESSAGE_BYTES);
	CHECK(memcmp(&buffer[3], k_goldenBytes, MESSAGE_BYTES) == 0);
	CHECK(buffer[0] == 0x5Au && buffer[1] == 0x5Au && buffer[2] == 0x5Au);
	for (size_t i = 3u + MESSAGE_BYTES; i < sizeof(buffer); i++)
	{
		CHECK(buffer[i] == 0x5Au);
	}

	/* Decoding from an offset: exactly 64 remaining succeeds and consumes
	 * them. */
	{
		struct NativeCodecReader reader;
		struct NativeMatchSelectMessageV1 out;
		uint32_t cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC;

		NativeCodecReader_Init(&reader, buffer, 3u + MESSAGE_BYTES);
		reader.offset = 3u;
		CHECK(NativeMatchSelectMessageV1_Decode(&reader, &out, &cause));
		CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE);
		CHECK(reader.offset == 3u + MESSAGE_BYTES);
		CHECK(SameMessage(&out, &golden) == 0);

		/* Six trailing bytes past the record: 70 remaining is a size fault. */
		NativeCodecReader_Init(&reader, buffer, sizeof(buffer));
		reader.offset = 2u;
		CHECK(NativeCodecReader_Remaining(&reader) == MESSAGE_BYTES + 6u);
		CHECK(!NativeMatchSelectMessageV1_Decode(&reader, &out, &cause));
		CHECK(cause == NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE);
		CHECK(reader.offset == 2u);
	}
	return 0;
}

/*
 * A caller's writer that carries a NativeCodecDigest64 (a record embedded in
 * a larger digested stream) sees Encode update that digest exactly as if the
 * 64 output bytes had been written directly, and sees it untouched on any
 * failure.
 */
static int TestWriterDigest(void)
{
	struct NativeMatchSelectMessageV1 golden;
	struct NativeMatchSelectMessageV1 message;
	struct NativeCodecWriter writer;
	struct NativeCodecDigest64 digest;
	struct NativeCodecDigest64 expected;
	uint8_t buffer[MESSAGE_BYTES + 8u];
	static const uint8_t prefix[3] = { 0x01u, 0x02u, 0x03u };

	MakeGolden(&golden);

	/* From a fresh digest: equal to FNV-1a 64 over the 64 output bytes. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &digest);
	CHECK(NativeMatchSelectMessageV1_Encode(&writer, &golden));
	CHECK(writer.digest == &digest);
	CHECK(memcmp(buffer, k_goldenBytes, MESSAGE_BYTES) == 0);
	NativeCodecDigest64_Init(&expected);
	NativeCodecDigest64_Update(&expected, buffer, MESSAGE_BYTES);
	CHECK(digest.value == expected.value);

	/* Mid-stream: the digest already covers a prefix, and continues over the
	 * record as a direct write of the same bytes would. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &digest);
	CHECK(NativeCodecWriter_WriteBytes(&writer, prefix, sizeof(prefix)));
	MakePicking(&message);
	CHECK(NativeMatchSelectMessageV1_Encode(&writer, &message));
	CHECK(writer.offset == sizeof(prefix) + MESSAGE_BYTES);
	{
		struct NativeCodecDigest64 direct;
		struct NativeCodecWriter directWriter;
		uint8_t directBuffer[sizeof(prefix) + MESSAGE_BYTES];

		NativeCodecDigest64_Init(&direct);
		NativeCodecWriter_Init(&directWriter, directBuffer, sizeof(directBuffer), &direct);
		CHECK(NativeCodecWriter_WriteBytes(&directWriter, prefix, sizeof(prefix)));
		CHECK(NativeCodecWriter_WriteBytes(&directWriter, &buffer[sizeof(prefix)], MESSAGE_BYTES));
		CHECK(digest.value == direct.value);
		NativeCodecDigest64_Init(&expected);
		NativeCodecDigest64_Update(&expected, buffer, sizeof(prefix) + MESSAGE_BYTES);
		CHECK(digest.value == expected.value);
	}

	/* Failures leave the caller's digest untouched: a shape fault, a
	 * too-small writer, and a failed writer. */
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, prefix, sizeof(prefix));
	expected = digest;

	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &digest);
	MakeGolden(&message);
	message.sequence = 0;
	CHECK(EncodeFails(&writer, &message) == 0);
	CHECK(digest.value == expected.value);

	NativeCodecWriter_Init(&writer, buffer, MESSAGE_BYTES - 1u, &digest);
	CHECK(EncodeFails(&writer, &golden) == 0);
	CHECK(digest.value == expected.value);

	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &digest);
	writer.offset = sizeof(buffer) - MESSAGE_BYTES + 1u;
	CHECK(EncodeFails(&writer, &golden) == 0);
	CHECK(digest.value == expected.value);

	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &digest);
	writer.failed = 1;
	CHECK(EncodeFails(&writer, &golden) == 0);
	CHECK(digest.value == expected.value);
	return 0;
}

int main(void)
{
	CHECK(TestGolden() == 0);
	CHECK(TestRoundTrip() == 0);
	CHECK(TestWireFaults() == 0);
	CHECK(TestEverySingleByteFlip() == 0);
	CHECK(TestShapeFaults() == 0);
	CHECK(TestShapeFaultOrder() == 0);
	CHECK(TestTransactional() == 0);
	CHECK(TestWriterDigest() == 0);
	puts("native_match_select_message_test: ok");
	return 0;
}
