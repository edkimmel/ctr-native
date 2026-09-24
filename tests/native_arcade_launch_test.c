#include "platform/native_arcade_launch.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define RECORD_BYTES NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES
#define BODY_BYTES NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET
#define DIGEST_BYTES NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES

/*
 * Frozen wire format. These 64 bytes are written out by hand from the offset
 * table in native_arcade_launch.h, not produced by the encoder. The trailer
 * is FNV-1a 64 (offset basis 0xcbf29ce484222325, prime 0x100000001b3) over
 * bytes 0..55: 0x4813ae0f5d92bcf3, stored little-endian at offset 56.
 *
 * How the trailer was computed: out of band, not with this repository's
 * codec, by a standalone Windows PowerShell FNV-1a 64 script. It starts from
 * h = 0xcbf29ce484222325 as a System.Numerics.BigInteger and, for each byte
 * b, sets h = ((h -bxor b) * 0x100000001b3) -band (2^64 - 1). The script
 * first self-checks against the published FNV-1a 64 vector for the ASCII
 * string "a": 0xaf63dc4c8601ec8c. It then hashes the 56 body bytes below,
 * typed in from the offset table (offsets 0..55), and prints
 * 0x4813ae0f5d92bcf3.
 *
 * A change to any byte here is a wire format change.
 */
static const uint8_t k_goldenBytes[RECORD_BYTES] = {
	/* 0: magic 0x314C414E "NAL1" */ 0x4Eu, 0x41u, 0x4Cu, 0x31u,
	/* 4: messageVersion 1 */ 0x01u, 0x00u,
	/* 6: encodedSize 64 */ 0x40u, 0x00u,
	/* 8: senderRole CAB1 */ 0x01u,
	/* 9: flags HEARD */ 0x01u,
	/* 10: reserved0 */ 0x00u, 0x00u,
	/* 12: sequence 7 */ 0x07u, 0x00u, 0x00u, 0x00u,
	/* 16: configDigest 0x00..0x1F */
	0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu,
	0x10u, 0x11u, 0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u, 0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Du, 0x1Eu, 0x1Fu,
	/* 48: reserved1 */ 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
	/* 56: FNV-1a 64 over bytes 0..55 */ 0xF3u, 0xBCu, 0x92u, 0x5Du, 0x0Fu, 0xAEu, 0x13u, 0x48u
};

/* The record the golden bytes encode. */
static void MakeGolden(struct NativeArcadeLaunchRecordV1 *record)
{
	memset(record, 0, sizeof(*record));
	record->senderRole = NATIVE_ARCADE_LAUNCH_ROLE_CAB1;
	record->flags = NATIVE_ARCADE_LAUNCH_FLAG_HEARD;
	record->sequence = 7;
	for (uint8_t i = 0; i < DIGEST_BYTES; i++)
	{
		record->configDigest[i] = i;
	}
}

static void MakeDigest(uint8_t digest[DIGEST_BYTES], uint8_t seed)
{
	for (uint32_t i = 0; i < DIGEST_BYTES; i++)
	{
		digest[i] = (uint8_t)(seed + (uint8_t)(i * 7u));
	}
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
 * Serializes any record, valid or not, by the offset table with no shape
 * check, then seals it. Used to craft records the encoder refuses to write.
 */
static void RawEncode(const struct NativeArcadeLaunchRecordV1 *record, uint8_t bytes[RECORD_BYTES])
{
	memset(bytes, 0, RECORD_BYTES);
	WriteLe(&bytes[0], NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC, 4u);
	WriteLe(&bytes[4], NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION, 2u);
	WriteLe(&bytes[6], NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES, 2u);
	bytes[8] = record->senderRole;
	bytes[9] = record->flags;
	memcpy(&bytes[10], record->reserved0, 2u);
	WriteLe(&bytes[12], record->sequence, 4u);
	memcpy(&bytes[16], record->configDigest, 32u);
	memcpy(&bytes[48], record->reserved1, 8u);
	Reseal(bytes);
}

static int EncodeRecord(const struct NativeArcadeLaunchRecordV1 *record, uint8_t bytes[RECORD_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, RECORD_BYTES, NULL);
	CHECK(NativeArcadeLaunchRecordV1_Encode(&writer, record));
	CHECK(NativeCodecWriter_Size(&writer) == RECORD_BYTES);
	return 0;
}

static int SameRecord(const struct NativeArcadeLaunchRecordV1 *a, const struct NativeArcadeLaunchRecordV1 *b)
{
	CHECK(a->senderRole == b->senderRole);
	CHECK(a->flags == b->flags);
	CHECK(memcmp(a->reserved0, b->reserved0, sizeof(a->reserved0)) == 0);
	CHECK(a->sequence == b->sequence);
	CHECK(memcmp(a->configDigest, b->configDigest, sizeof(a->configDigest)) == 0);
	CHECK(memcmp(a->reserved1, b->reserved1, sizeof(a->reserved1)) == 0);
	return 0;
}

static int DecodeOk(const uint8_t *bytes, const struct NativeArcadeLaunchRecordV1 *expected)
{
	struct NativeCodecReader reader;
	struct NativeArcadeLaunchRecordV1 out;
	uint32_t cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC;

	memset(&out, 0xCD, sizeof(out));
	NativeCodecReader_Init(&reader, bytes, RECORD_BYTES);
	CHECK(NativeArcadeLaunchRecordV1_Decode(&reader, &out, &cause));
	CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE);
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	CHECK(SameRecord(&out, expected) == 0);
	/* A NULL fault sink is accepted. */
	NativeCodecReader_Init(&reader, bytes, RECORD_BYTES);
	CHECK(NativeArcadeLaunchRecordV1_Decode(&reader, &out, NULL));
	return 0;
}

/* Decode must fail with exactly the expected cause and leave both the
 * caller's reader and the output record untouched. */
static int DecodeFails(const uint8_t *bytes, size_t size, uint32_t expectedCause)
{
	struct NativeCodecReader reader;
	struct NativeCodecReader readerBefore;
	struct NativeArcadeLaunchRecordV1 out;
	struct NativeArcadeLaunchRecordV1 outBefore;
	uint32_t cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE;

	memset(&out, 0xCD, sizeof(out));
	outBefore = out;
	NativeCodecReader_Init(&reader, bytes, size);
	readerBefore = reader;
	CHECK(!NativeArcadeLaunchRecordV1_Decode(&reader, &out, &cause));
	CHECK(cause == expectedCause);
	CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
	CHECK(memcmp(&out, &outBefore, sizeof(out)) == 0);
	NativeCodecReader_Init(&reader, bytes, size);
	CHECK(!NativeArcadeLaunchRecordV1_Decode(&reader, &out, NULL));
	CHECK(memcmp(&out, &outBefore, sizeof(out)) == 0);
	return 0;
}

/* Encode must refuse and leave the writer and its buffer untouched. */
static int EncodeFails(struct NativeCodecWriter *writer, const struct NativeArcadeLaunchRecordV1 *record)
{
	struct NativeCodecWriter before = *writer;
	uint8_t bufferBefore[RECORD_BYTES + 8u];
	size_t span = writer->capacity < sizeof(bufferBefore) ? writer->capacity : sizeof(bufferBefore);

	if (writer->data != NULL)
	{
		memcpy(bufferBefore, writer->data, span);
	}
	CHECK(!NativeArcadeLaunchRecordV1_Encode(writer, record));
	CHECK(memcmp(writer, &before, sizeof(before)) == 0);
	if (writer->data != NULL)
	{
		CHECK(memcmp(bufferBefore, writer->data, span) == 0);
	}
	return 0;
}

/* One shape fault: ShapeCause reports it, Encode refuses it, and a sealed
 * raw record carrying it decodes to exactly that cause. */
static int ShapeFault(const struct NativeArcadeLaunchRecordV1 *record, uint32_t expectedCause)
{
	uint8_t buffer[RECORD_BYTES];
	uint8_t bytes[RECORD_BYTES];
	struct NativeCodecWriter writer;

	CHECK(NativeArcadeLaunchRecordV1_ShapeCause(record) == expectedCause);
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	CHECK(EncodeFails(&writer, record) == 0);
	RawEncode(record, bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), expectedCause) == 0);
	return 0;
}

/* A valid record: ShapeCause NONE, and it round-trips. */
static int ShapeOk(const struct NativeArcadeLaunchRecordV1 *record)
{
	uint8_t bytes[RECORD_BYTES];
	uint8_t raw[RECORD_BYTES];

	CHECK(NativeArcadeLaunchRecordV1_ShapeCause(record) == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE);
	CHECK(EncodeRecord(record, bytes) == 0);
	RawEncode(record, raw);
	CHECK(memcmp(bytes, raw, sizeof(bytes)) == 0);
	CHECK(DecodeOk(bytes, record) == 0);
	return 0;
}

static int TestGolden(void)
{
	struct NativeArcadeLaunchRecordV1 golden;
	uint8_t bytes[RECORD_BYTES];
	uint8_t raw[RECORD_BYTES];

	CHECK(NativeArcadeLaunchRecordV1_EncodedSize() == RECORD_BYTES);
	CHECK(RECORD_BYTES == 64u);
	CHECK(BODY_BYTES == 56u);
	CHECK(DIGEST_BYTES == 32u);
	CHECK(NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC == UINT32_C(0x314C414E));
	CHECK(NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION == 1u);
	CHECK(NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS == 300u);

	MakeGolden(&golden);
	memset(bytes, 0xEE, sizeof(bytes));
	CHECK(EncodeRecord(&golden, bytes) == 0);
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
	struct NativeArcadeLaunchRecordV1 record;

	MakeGolden(&record);
	CHECK(ShapeOk(&record) == 0);

	/* CAB2, no HEARD, both sequence bounds, opaque digests. */
	record.senderRole = NATIVE_ARCADE_LAUNCH_ROLE_CAB2;
	record.flags = 0;
	record.sequence = 1;
	CHECK(ShapeOk(&record) == 0);
	record.sequence = UINT32_MAX;
	CHECK(ShapeOk(&record) == 0);
	memset(record.configDigest, 0, sizeof(record.configDigest));
	CHECK(ShapeOk(&record) == 0);
	memset(record.configDigest, 0xFF, sizeof(record.configDigest));
	CHECK(ShapeOk(&record) == 0);
	return 0;
}

/* Wire-level faults before the shape checks: size, magic, version, encoded
 * size, digest. */
static int TestWireFaults(void)
{
	uint8_t bytes[RECORD_BYTES];
	uint8_t longer[RECORD_BYTES + 1u];

	/* Remaining length other than 64: 63, 65, and every shorter length. */
	CHECK(DecodeFails(k_goldenBytes, 63u, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE) == 0);
	memcpy(longer, k_goldenBytes, RECORD_BYTES);
	longer[RECORD_BYTES] = 0;
	CHECK(DecodeFails(longer, sizeof(longer), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE) == 0);
	for (size_t size = 0; size < RECORD_BYTES; size++)
	{
		CHECK(DecodeFails(k_goldenBytes, size, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE) == 0);
	}

	/* NULL arguments and a failed reader are size faults and touch nothing. */
	{
		struct NativeCodecReader reader;
		struct NativeArcadeLaunchRecordV1 record;
		struct NativeArcadeLaunchRecordV1 recordBefore;
		uint32_t cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE;

		memset(&record, 0xCD, sizeof(record));
		recordBefore = record;
		CHECK(!NativeArcadeLaunchRecordV1_Decode(NULL, &record, &cause));
		CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE);
		CHECK(memcmp(&record, &recordBefore, sizeof(record)) == 0);
		CHECK(!NativeArcadeLaunchRecordV1_Decode(NULL, NULL, NULL));

		NativeCodecReader_Init(&reader, k_goldenBytes, RECORD_BYTES);
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE;
		CHECK(!NativeArcadeLaunchRecordV1_Decode(&reader, NULL, &cause));
		CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE);
		CHECK(NativeCodecReader_Remaining(&reader) == RECORD_BYTES);

		NativeCodecReader_Init(&reader, k_goldenBytes, RECORD_BYTES);
		reader.failed = 1;
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE;
		CHECK(!NativeArcadeLaunchRecordV1_Decode(&reader, &record, &cause));
		CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE);
		CHECK(memcmp(&record, &recordBefore, sizeof(record)) == 0);
		CHECK(reader.offset == 0);
	}

	/* A hand-built reader with NULL data, not marked failed, with exactly 64
	 * bytes remaining: a size fault, never dereferenced. */
	{
		struct NativeCodecReader reader;
		struct NativeCodecReader readerBefore;
		struct NativeArcadeLaunchRecordV1 record;
		struct NativeArcadeLaunchRecordV1 recordBefore;
		uint32_t cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE;

		memset(&reader, 0, sizeof(reader));
		reader.data = NULL;
		reader.failed = 0;
		reader.offset = 8u;
		reader.size = 8u + RECORD_BYTES;
		CHECK(NativeCodecReader_Ok(&reader));
		CHECK(NativeCodecReader_Remaining(&reader) == RECORD_BYTES);
		readerBefore = reader;
		memset(&record, 0xCD, sizeof(record));
		recordBefore = record;
		CHECK(!NativeArcadeLaunchRecordV1_Decode(&reader, &record, &cause));
		CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE);
		CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
		CHECK(memcmp(&record, &recordBefore, sizeof(record)) == 0);
	}

	/* Magic, version (either byte), encoded size (either byte), each resealed
	 * so the check under test is the one that fires. */
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[3] = 0x32u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC) == 0);
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[4] = 0x02u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_VERSION) == 0);
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[5] = 0x01u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_VERSION) == 0);
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[6] = 0x41u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ENCODED_SIZE) == 0);
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[7] = 0x01u;
	Reseal(bytes);
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ENCODED_SIZE) == 0);

	/* One flipped payload bit under a stale trailer (configDigest is opaque,
	 * so only the digest can catch it), and a wrong trailer. */
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[20] ^= 0x10u;
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST) == 0);
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[RECORD_BYTES - 1u] ^= 0x80u;
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST) == 0);

	/* Fixed order: magic before version, encoded size, and digest. */
	memcpy(bytes, k_goldenBytes, RECORD_BYTES);
	bytes[0] = 0u;
	bytes[4] = 9u;
	bytes[6] = 9u;
	bytes[RECORD_BYTES - 1u] ^= 0x01u;
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC) == 0);
	bytes[0] = k_goldenBytes[0];
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_VERSION) == 0);
	bytes[4] = k_goldenBytes[4];
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ENCODED_SIZE) == 0);
	bytes[6] = k_goldenBytes[6];
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST) == 0);
	return 0;
}

/*
 * Every single-byte change of a valid record is rejected: bytes 0..3 by the
 * magic check, 4..5 by the version check, 6..7 by the encoded-size check,
 * and everything from 8 on by the digest.
 */
static int TestEverySingleByteFlip(void)
{
	uint8_t bytes[RECORD_BYTES];

	for (uint32_t offset = 0; offset < RECORD_BYTES; offset++)
	{
		uint32_t expected = offset < 4u   ? NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC
		                    : offset < 6u ? NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_VERSION
		                    : offset < 8u ? NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ENCODED_SIZE
		                                  : NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST;

		for (uint32_t mask = 1; mask <= 0xFFu; mask++)
		{
			memcpy(bytes, k_goldenBytes, RECORD_BYTES);
			bytes[offset] ^= (uint8_t)mask;
			CHECK(DecodeFails(bytes, sizeof(bytes), expected) == 0);
		}
	}
	return 0;
}

/* Each shape fault cause, one defect at a time, from a valid record. */
static int TestShapeFaults(void)
{
	struct NativeArcadeLaunchRecordV1 record;

	/* BAD_RESERVED: every reserved0 and reserved1 byte. */
	for (uint32_t i = 0; i < NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED0_BYTES; i++)
	{
		MakeGolden(&record);
		record.reserved0[i] = 0x01u;
		CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_RESERVED) == 0);
	}
	for (uint32_t i = 0; i < NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED1_BYTES; i++)
	{
		MakeGolden(&record);
		record.reserved1[i] = 0x80u;
		CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_RESERVED) == 0);
	}

	/* BAD_ROLE: 0, 3, and every other value. */
	for (uint32_t role = 0; role <= 0xFFu; role++)
	{
		if ((role == NATIVE_ARCADE_LAUNCH_ROLE_CAB1) || (role == NATIVE_ARCADE_LAUNCH_ROLE_CAB2))
		{
			continue;
		}
		MakeGolden(&record);
		record.senderRole = (uint8_t)role;
		CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ROLE) == 0);
	}

	/* BAD_FLAGS: 0x02, and every bit above HEARD with and without HEARD. */
	MakeGolden(&record);
	record.flags = 0x02u;
	CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_FLAGS) == 0);
	for (uint32_t bit = 1; bit < 8u; bit++)
	{
		MakeGolden(&record);
		record.flags = (uint8_t)(1u << bit);
		CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_FLAGS) == 0);
		record.flags = (uint8_t)(NATIVE_ARCADE_LAUNCH_FLAG_HEARD | (1u << bit));
		CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_FLAGS) == 0);
	}

	/* BAD_SEQUENCE. */
	MakeGolden(&record);
	record.sequence = 0;
	CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SEQUENCE) == 0);

	CHECK(NativeArcadeLaunchRecordV1_ShapeCause(NULL) == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE);
	return 0;
}

/* The documented check order: a record with every shape fault at once
 * reports each cause in check order as each is repaired in turn, and the
 * digest is checked before any shape check. */
static int TestShapeFaultOrder(void)
{
	struct NativeArcadeLaunchRecordV1 record;
	uint8_t bytes[RECORD_BYTES];

	MakeGolden(&record);
	record.reserved0[1] = 1;
	record.senderRole = 3;
	record.flags = 0x80u;
	record.sequence = 0;

	CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_RESERVED) == 0);
	record.reserved0[1] = 0;
	CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ROLE) == 0);
	record.senderRole = NATIVE_ARCADE_LAUNCH_ROLE_CAB2;
	CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_FLAGS) == 0);
	record.flags = NATIVE_ARCADE_LAUNCH_FLAG_HEARD;
	CHECK(ShapeFault(&record, NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SEQUENCE) == 0);
	record.sequence = 1;
	CHECK(ShapeOk(&record) == 0);

	MakeGolden(&record);
	record.senderRole = 0;
	RawEncode(&record, bytes);
	bytes[BODY_BYTES] ^= 0x01u;
	CHECK(DecodeFails(bytes, sizeof(bytes), NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST) == 0);
	return 0;
}

/* Encode and Decode are transactional. */
static int TestTransactional(void)
{
	struct NativeArcadeLaunchRecordV1 golden;
	struct NativeCodecWriter writer;
	struct NativeCodecDigest64 digest;
	struct NativeCodecDigest64 expected;
	uint8_t buffer[RECORD_BYTES + 8u];

	MakeGolden(&golden);

	/* Writer one byte too small. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, RECORD_BYTES - 1u, NULL);
	CHECK(EncodeFails(&writer, &golden) == 0);

	/* Writer with room overall but not after its current offset. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, RECORD_BYTES + 3u, NULL);
	writer.offset = 4u;
	CHECK(EncodeFails(&writer, &golden) == 0);

	/* A failed writer, and NULL arguments. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.failed = 1;
	CHECK(EncodeFails(&writer, &golden) == 0);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	CHECK(EncodeFails(&writer, NULL) == 0);
	CHECK(!NativeArcadeLaunchRecordV1_Encode(NULL, &golden));

	/* A caller digest is untouched by a failure and advanced by a success
	 * exactly as a direct write of the 64 bytes would. */
	NativeCodecDigest64_Init(&digest);
	expected = digest;
	NativeCodecWriter_Init(&writer, buffer, RECORD_BYTES - 1u, &digest);
	CHECK(EncodeFails(&writer, &golden) == 0);
	CHECK(digest.value == expected.value);
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), &digest);
	CHECK(NativeArcadeLaunchRecordV1_Encode(&writer, &golden));
	NativeCodecDigest64_Update(&expected, k_goldenBytes, RECORD_BYTES);
	CHECK(digest.value == expected.value);

	/* Appending at an offset writes exactly the record there. */
	memset(buffer, 0x5A, sizeof(buffer));
	NativeCodecWriter_Init(&writer, buffer, sizeof(buffer), NULL);
	writer.offset = 3u;
	CHECK(NativeArcadeLaunchRecordV1_Encode(&writer, &golden));
	CHECK(writer.offset == 3u + RECORD_BYTES);
	CHECK(memcmp(&buffer[3], k_goldenBytes, RECORD_BYTES) == 0);
	CHECK(buffer[0] == 0x5Au && buffer[1] == 0x5Au && buffer[2] == 0x5Au);
	for (size_t i = 3u + RECORD_BYTES; i < sizeof(buffer); i++)
	{
		CHECK(buffer[i] == 0x5Au);
	}

	/* Decoding from an offset: exactly 64 remaining succeeds and consumes
	 * them; 70 remaining is a size fault that leaves the reader alone. */
	{
		struct NativeCodecReader reader;
		struct NativeArcadeLaunchRecordV1 out;
		uint32_t cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC;

		NativeCodecReader_Init(&reader, buffer, 3u + RECORD_BYTES);
		reader.offset = 3u;
		CHECK(NativeArcadeLaunchRecordV1_Decode(&reader, &out, &cause));
		CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE);
		CHECK(reader.offset == 3u + RECORD_BYTES);
		CHECK(SameRecord(&out, &golden) == 0);

		NativeCodecReader_Init(&reader, buffer, sizeof(buffer));
		reader.offset = 2u;
		CHECK(!NativeArcadeLaunchRecordV1_Decode(&reader, &out, &cause));
		CHECK(cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE);
		CHECK(reader.offset == 2u);
	}
	return 0;
}

/* ---- Agreement state ---- */

static int IsZero(const void *bytes, size_t size)
{
	const uint8_t *p = (const uint8_t *)bytes;

	for (size_t i = 0; i < size; i++)
	{
		if (p[i] != 0u)
		{
			return 0;
		}
	}
	return 1;
}

static int Decode(const uint8_t *bytes, struct NativeArcadeLaunchRecordV1 *out)
{
	struct NativeCodecReader reader;

	NativeCodecReader_Init(&reader, bytes, RECORD_BYTES);
	CHECK(NativeArcadeLaunchRecordV1_Decode(&reader, out, NULL));
	return 0;
}

/* Composes one record and checks it against the agreement. */
static int Compose(struct NativeArcadeLaunchAgreement *agreement, uint8_t out[RECORD_BYTES])
{
	struct NativeArcadeLaunchRecordV1 record;
	uint32_t sequenceBefore = agreement->sequence;
	int committed = agreement->status == NATIVE_ARCADE_LAUNCH_COMMITTED;
	size_t size = 0;

	CHECK(NativeArcadeLaunch_Compose(agreement, out, RECORD_BYTES, &size));
	CHECK(size == RECORD_BYTES);
	CHECK(agreement->sequence == sequenceBefore + 1u);
	CHECK(Decode(out, &record) == 0);
	CHECK(record.senderRole == agreement->localRole);
	CHECK(record.flags == (committed ? NATIVE_ARCADE_LAUNCH_FLAG_HEARD : 0u));
	CHECK(record.sequence == agreement->sequence);
	CHECK(memcmp(record.configDigest, agreement->configDigest, DIGEST_BYTES) == 0);
	CHECK(IsZero(record.reserved0, sizeof(record.reserved0)));
	CHECK(IsZero(record.reserved1, sizeof(record.reserved1)));
	if (committed)
	{
		CHECK(agreement->heardSent == 1u);
	}
	return 0;
}

/* Builds a sealed record from any role, flags, sequence, and digest. */
static void CraftRecord(uint8_t bytes[RECORD_BYTES], uint8_t role, uint8_t flags, uint32_t sequence, const uint8_t digest[DIGEST_BYTES])
{
	struct NativeArcadeLaunchRecordV1 record;

	memset(&record, 0, sizeof(record));
	record.senderRole = role;
	record.flags = flags;
	record.sequence = sequence;
	memcpy(record.configDigest, digest, DIGEST_BYTES);
	RawEncode(&record, bytes);
}

static int TestBegin(void)
{
	struct NativeArcadeLaunchAgreement agreement;
	struct NativeArcadeLaunchAgreement before;
	uint8_t digest[DIGEST_BYTES];

	MakeDigest(digest, 0x40u);
	memset(&agreement, 0xA5, sizeof(agreement));
	before = agreement;

	CHECK(!NativeArcadeLaunch_Begin(NULL, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 10u));
	CHECK(!NativeArcadeLaunch_Begin(&agreement, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, NULL, 10u));
	CHECK(memcmp(&agreement, &before, sizeof(agreement)) == 0);
	CHECK(!NativeArcadeLaunch_Begin(&agreement, 0u, digest, 10u));
	CHECK(memcmp(&agreement, &before, sizeof(agreement)) == 0);
	CHECK(!NativeArcadeLaunch_Begin(&agreement, 3u, digest, 10u));
	CHECK(memcmp(&agreement, &before, sizeof(agreement)) == 0);
	CHECK(!NativeArcadeLaunch_Begin(&agreement, 0xFFu, digest, 10u));
	CHECK(memcmp(&agreement, &before, sizeof(agreement)) == 0);
	CHECK(!NativeArcadeLaunch_Begin(&agreement, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 0u));
	CHECK(memcmp(&agreement, &before, sizeof(agreement)) == 0);

	/* A fresh PENDING agreement over any previous contents. */
	CHECK(NativeArcadeLaunch_Begin(&agreement, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, digest, 10u));
	CHECK(agreement.active == 1u);
	CHECK(agreement.localRole == NATIVE_ARCADE_LAUNCH_ROLE_CAB2);
	CHECK(agreement.status == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(agreement.peerHeard == 0u);
	CHECK(agreement.heardSent == 0u);
	CHECK(IsZero(agreement.reserved, sizeof(agreement.reserved)));
	CHECK(memcmp(agreement.configDigest, digest, DIGEST_BYTES) == 0);
	CHECK(agreement.sequence == 0u);
	CHECK(agreement.lingerTicks == 10u);
	CHECK(agreement.ticksSinceCommit == 0u);
	CHECK(agreement.acceptedCount == 0u && agreement.foreignCount == 0u && agreement.malformedCount == 0u);
	CHECK(agreement.selfCount == 0u && agreement.mismatchCount == 0u);
	CHECK(NativeArcadeLaunch_Active(&agreement));
	CHECK(NativeArcadeLaunch_Status(&agreement) == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(NativeArcadeLaunch_ShouldSend(&agreement) == 1);

	/* Begin on a committed agreement starts over. */
	agreement.status = NATIVE_ARCADE_LAUNCH_COMMITTED;
	agreement.peerHeard = 1;
	agreement.sequence = 9;
	CHECK(NativeArcadeLaunch_Begin(&agreement, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS));
	CHECK(agreement.status == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(agreement.peerHeard == 0u);
	CHECK(agreement.sequence == 0u);
	CHECK(agreement.lingerTicks == NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS);
	return 0;
}

/* Two agreements on the same digest, every record delivered: both commit on
 * the first exchange, then send HEARD once and stop (RL-4). */
static int TestSymmetricCommit(void)
{
	struct NativeArcadeLaunchAgreement a;
	struct NativeArcadeLaunchAgreement b;
	uint8_t digest[DIGEST_BYTES];
	uint8_t fromA[RECORD_BYTES];
	uint8_t fromB[RECORD_BYTES];
	uint32_t sentA = 0;
	uint32_t sentB = 0;

	MakeDigest(digest, 0x11u);
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS));
	CHECK(NativeArcadeLaunch_Begin(&b, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, digest, NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS));

	for (uint32_t tick = 0; tick < 20u; tick++)
	{
		int sendA = NativeArcadeLaunch_ShouldSend(&a);
		int sendB = NativeArcadeLaunch_ShouldSend(&b);

		if (sendA)
		{
			CHECK(Compose(&a, fromA) == 0);
			sentA++;
		}
		if (sendB)
		{
			CHECK(Compose(&b, fromB) == 0);
			sentB++;
		}
		if (sendA)
		{
			CHECK(NativeArcadeLaunch_Accept(&b, fromA, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		}
		if (sendB)
		{
			CHECK(NativeArcadeLaunch_Accept(&a, fromB, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		}
		if (tick == 0u)
		{
			/* The first valid matching record from the other role commits. */
			CHECK(NativeArcadeLaunch_Status(&a) == NATIVE_ARCADE_LAUNCH_COMMITTED);
			CHECK(NativeArcadeLaunch_Status(&b) == NATIVE_ARCADE_LAUNCH_COMMITTED);
			CHECK(a.ticksSinceCommit == 0u && b.ticksSinceCommit == 0u);
			CHECK(!a.peerHeard && !b.peerHeard);
			CHECK(!a.heardSent && !b.heardSent);
		}
		NativeArcadeLaunch_Tick(&a);
		NativeArcadeLaunch_Tick(&b);
	}

	/* One PENDING record and one HEARD record each, then silence. */
	CHECK(sentA == 2u && sentB == 2u);
	CHECK(a.sequence == 2u && b.sequence == 2u);
	CHECK(a.peerHeard && a.heardSent && b.peerHeard && b.heardSent);
	CHECK(a.acceptedCount == 2u && b.acceptedCount == 2u);
	CHECK(a.ticksSinceCommit == 20u && b.ticksSinceCommit == 20u);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0 && NativeArcadeLaunch_ShouldSend(&b) == 0);
	CHECK(a.foreignCount == 0u && a.malformedCount == 0u && a.selfCount == 0u && a.mismatchCount == 0u);

	/* Duplicated and reordered records change nothing; HEARD stays latched
	 * after an older PENDING record. */
	{
		uint8_t old[RECORD_BYTES];

		CraftRecord(old, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, 0u, 1u, digest);
		CHECK(NativeArcadeLaunch_Accept(&a, old, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		CHECK(NativeArcadeLaunch_Accept(&a, fromB, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		CHECK(NativeArcadeLaunch_Accept(&a, fromB, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		CHECK(NativeArcadeLaunch_Accept(&a, old, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		CHECK(a.status == NATIVE_ARCADE_LAUNCH_COMMITTED);
		CHECK(a.peerHeard == 1u);
		CHECK(a.ticksSinceCommit == 20u);
		CHECK(a.acceptedCount == 6u);
		CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);
	}
	return 0;
}

/* SELF, MISMATCH, FOREIGN, and MALFORMED are ignored and counted and never
 * commit, whatever their flags. */
static int TestIgnoredRecords(void)
{
	struct NativeArcadeLaunchAgreement a;
	uint8_t digest[DIGEST_BYTES];
	uint8_t other[DIGEST_BYTES];
	uint8_t bytes[RECORD_BYTES + 1u];

	MakeDigest(digest, 0x22u);
	memcpy(other, digest, DIGEST_BYTES);
	other[DIGEST_BYTES - 1u] ^= 0x01u;
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 10u));

	/* SELF: our own role echoed back, with and without HEARD, and even with
	 * another digest (SELF is checked first). */
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 3u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_SELF);
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, 0u, 4u, other);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_SELF);
	CHECK(a.selfCount == 2u);

	/* MISMATCH: the other role on another digest. */
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 5u, other);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MISMATCH);
	CHECK(a.mismatchCount == 1u);

	/* FOREIGN: a 64-byte record whose magic is the select record's
	 * (little-endian 0x31534d4e), and a sealed record with another magic. */
	memset(bytes, 0, sizeof(bytes));
	WriteLe(bytes, UINT32_C(0x31534d4e), 4u);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_FOREIGN);
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, 0u, 1u, digest);
	bytes[0] ^= 0x01u;
	Reseal(bytes);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_FOREIGN);
	CHECK(a.foreignCount == 2u);

	/* MALFORMED: NULL bytes, a wrong size (63, 65, 0), a stale trailer, a bad
	 * version, and each shape fault, all from a would-be valid record. */
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 1u, digest);
	bytes[RECORD_BYTES] = 0;
	CHECK(NativeArcadeLaunch_Accept(&a, NULL, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES - 1u) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES + 1u) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, 0u) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	bytes[20] ^= 0x01u;
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 1u, digest);
	bytes[4] = 2u;
	Reseal(bytes);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CraftRecord(bytes, 3u, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 1u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, 0x03u, 1u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 0u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 1u, digest);
	bytes[50] = 1u;
	Reseal(bytes);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
	CHECK(a.malformedCount == 10u);

	/* None of these committed, heard, or moved anything else. */
	CHECK(a.status == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(a.peerHeard == 0u && a.heardSent == 0u);
	CHECK(a.acceptedCount == 0u);
	CHECK(a.sequence == 0u);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
	NativeArcadeLaunch_Tick(&a);
	CHECK(a.ticksSinceCommit == 0u);

	/* A valid record still commits afterwards. */
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, 0u, 1u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
	CHECK(a.status == NATIVE_ARCADE_LAUNCH_COMMITTED);
	return 0;
}

/*
 * One-direction loss: A hears B, B never hears A. A commits and keeps sending
 * HEARD (heardSent, never peerHeard) until ticksSinceCommit reaches
 * lingerTicks, then stops. B stays PENDING and keeps sending (RL-7).
 */
static int OneDirectionLoss(uint32_t linger)
{
	struct NativeArcadeLaunchAgreement a;
	struct NativeArcadeLaunchAgreement b;
	uint8_t digest[DIGEST_BYTES];
	uint8_t fromA[RECORD_BYTES];
	uint8_t fromB[RECORD_BYTES];
	uint32_t sentAfterCommit = 0;
	uint32_t ticks = linger + 40u;

	MakeDigest(digest, 0x33u);
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, linger));
	CHECK(NativeArcadeLaunch_Begin(&b, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, digest, linger));

	/* A starts a few ticks before B's first record arrives. */
	for (uint32_t tick = 0; tick < 3u; tick++)
	{
		CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
		CHECK(Compose(&a, fromA) == 0);
		NativeArcadeLaunch_Tick(&a);
		CHECK(a.status == NATIVE_ARCADE_LAUNCH_PENDING);
	}

	for (uint32_t tick = 0; tick < ticks; tick++)
	{
		CHECK(NativeArcadeLaunch_ShouldSend(&b) == 1);
		CHECK(Compose(&b, fromB) == 0);
		CHECK(NativeArcadeLaunch_Accept(&a, fromB, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		CHECK(a.status == NATIVE_ARCADE_LAUNCH_COMMITTED);
		if (NativeArcadeLaunch_ShouldSend(&a))
		{
			CHECK(a.ticksSinceCommit < linger);
			CHECK(Compose(&a, fromA) == 0); /* lost */
			sentAfterCommit++;
		}
		else
		{
			CHECK(a.ticksSinceCommit >= linger);
		}
		NativeArcadeLaunch_Tick(&a);
		NativeArcadeLaunch_Tick(&b);
	}

	CHECK(sentAfterCommit == linger);
	CHECK(a.heardSent == 1u && a.peerHeard == 0u);
	CHECK(a.ticksSinceCommit == ticks);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);
	CHECK(b.status == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(b.acceptedCount == 0u);
	CHECK(b.ticksSinceCommit == 0u);
	CHECK(NativeArcadeLaunch_ShouldSend(&b) == 1);
	return 0;
}

static int TestOneDirectionLoss(void)
{
	CHECK(OneDirectionLoss(1u) == 0);
	CHECK(OneDirectionLoss(5u) == 0);
	CHECK(OneDirectionLoss(NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS) == 0);
	return 0;
}

/* Either half of the RL-4 stop condition alone keeps sending. */
static int TestHeardHalves(void)
{
	struct NativeArcadeLaunchAgreement a;
	uint8_t digest[DIGEST_BYTES];
	uint8_t bytes[RECORD_BYTES];

	MakeDigest(digest, 0x44u);

	/* heardSent without peerHeard: the peer's PENDING records keep coming. */
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, digest, 10u));
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, 0u, 1u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
	CHECK(Compose(&a, bytes) == 0);
	CHECK(a.heardSent == 1u && a.peerHeard == 0u);
	for (uint32_t tick = 0; tick < 9u; tick++)
	{
		NativeArcadeLaunch_Tick(&a);
		CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
	}
	/* The peer's HEARD arrives: both halves set, sending stops early. */
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 2u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
	CHECK(a.ticksSinceCommit == 9u);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);

	/* peerHeard without heardSent: the first record we hear already carries
	 * HEARD, so we commit heard but still owe one HEARD record. */
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, digest, 10u));
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 8u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
	CHECK(a.status == NATIVE_ARCADE_LAUNCH_COMMITTED);
	CHECK(a.peerHeard == 1u && a.heardSent == 0u);
	for (uint32_t tick = 0; tick < 5u; tick++)
	{
		CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
		NativeArcadeLaunch_Tick(&a);
	}
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
	CHECK(Compose(&a, bytes) == 0);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);

	/* A PENDING compose does not set heardSent. */
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 10u));
	CHECK(Compose(&a, bytes) == 0);
	CHECK(a.heardSent == 0u);
	return 0;
}

/* Compose refuses, changing nothing: bad arguments, inactive, and the
 * sequence at UINT32_MAX (no wrap). */
static int ComposeRefused(struct NativeArcadeLaunchAgreement *agreement, uint8_t *out, size_t capacity, size_t *sizeOut)
{
	struct NativeArcadeLaunchAgreement before;
	uint8_t outBefore[RECORD_BYTES + 1u];
	size_t sizeBefore = (sizeOut != NULL) ? *sizeOut : 0u;
	size_t span = (capacity < sizeof(outBefore)) ? capacity : sizeof(outBefore);

	memset(&before, 0, sizeof(before));
	if (agreement != NULL)
	{
		before = *agreement;
	}
	if (out != NULL)
	{
		memcpy(outBefore, out, span);
	}
	CHECK(!NativeArcadeLaunch_Compose(agreement, out, capacity, sizeOut));
	if (agreement != NULL)
	{
		CHECK(memcmp(agreement, &before, sizeof(before)) == 0);
	}
	if (out != NULL)
	{
		CHECK(memcmp(outBefore, out, span) == 0);
	}
	if (sizeOut != NULL)
	{
		CHECK(*sizeOut == sizeBefore);
	}
	return 0;
}

static int TestCompose(void)
{
	struct NativeArcadeLaunchAgreement a;
	struct NativeArcadeLaunchRecordV1 record;
	uint8_t digest[DIGEST_BYTES];
	uint8_t out[RECORD_BYTES + 1u];
	size_t size = 0x77u;

	MakeDigest(digest, 0x55u);
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 10u));
	memset(out, 0x5A, sizeof(out));

	CHECK(ComposeRefused(NULL, out, sizeof(out), &size) == 0);
	CHECK(ComposeRefused(&a, NULL, sizeof(out), &size) == 0);
	CHECK(ComposeRefused(&a, out, sizeof(out), NULL) == 0);
	CHECK(ComposeRefused(&a, out, RECORD_BYTES - 1u, &size) == 0);
	CHECK(ComposeRefused(&a, out, 0u, &size) == 0);

	/* A larger buffer: exactly 64 bytes are written. */
	CHECK(NativeArcadeLaunch_Compose(&a, out, sizeof(out), &size));
	CHECK(size == RECORD_BYTES);
	CHECK(out[RECORD_BYTES] == 0x5Au);
	CHECK(a.sequence == 1u);

	/* The last sequence is UINT32_MAX; the next compose is refused. */
	a.sequence = UINT32_MAX - 1u;
	CHECK(NativeArcadeLaunch_Compose(&a, out, RECORD_BYTES, &size));
	CHECK(Decode(out, &record) == 0);
	CHECK(record.sequence == UINT32_MAX);
	CHECK(a.sequence == UINT32_MAX);
	CHECK(ComposeRefused(&a, out, RECORD_BYTES, &size) == 0);
	CHECK(ComposeRefused(&a, out, RECORD_BYTES, &size) == 0);

	/* A corrupted role cannot encode: refused, nothing changed. */
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 10u));
	a.localRole = 0;
	CHECK(ComposeRefused(&a, out, RECORD_BYTES, &size) == 0);

	/* Inactive. */
	NativeArcadeLaunch_Reset(&a);
	CHECK(ComposeRefused(&a, out, RECORD_BYTES, &size) == 0);
	return 0;
}

static int TestReset(void)
{
	struct NativeArcadeLaunchAgreement a;
	uint8_t digest[DIGEST_BYTES];
	uint8_t bytes[RECORD_BYTES];

	MakeDigest(digest, 0x66u);
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, 10u));
	CraftRecord(bytes, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, NATIVE_ARCADE_LAUNCH_FLAG_HEARD, 1u, digest);
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
	CHECK(NativeArcadeLaunch_Status(&a) == NATIVE_ARCADE_LAUNCH_COMMITTED);

	NativeArcadeLaunch_Reset(&a);
	CHECK(IsZero(&a, sizeof(a)));
	CHECK(!NativeArcadeLaunch_Active(&a));
	CHECK(NativeArcadeLaunch_Status(&a) == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);

	/* Inactive: Accept is INACTIVE and counts nothing; Tick is a no-op. */
	CHECK(NativeArcadeLaunch_Accept(&a, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_INACTIVE);
	CHECK(NativeArcadeLaunch_Accept(&a, NULL, 0u) == NATIVE_ARCADE_LAUNCH_ACCEPT_INACTIVE);
	NativeArcadeLaunch_Tick(&a);
	CHECK(IsZero(&a, sizeof(a)));

	/* An inactive struct that still says COMMITTED reports PENDING and does
	 * not tick. */
	a.status = NATIVE_ARCADE_LAUNCH_COMMITTED;
	CHECK(NativeArcadeLaunch_Status(&a) == NATIVE_ARCADE_LAUNCH_PENDING);
	NativeArcadeLaunch_Tick(&a);
	CHECK(a.ticksSinceCommit == 0u);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);
	return 0;
}

static int TestNullSafety(void)
{
	uint8_t bytes[RECORD_BYTES];
	size_t size = 0;

	memcpy(bytes, k_goldenBytes, sizeof(bytes));
	NativeArcadeLaunch_Reset(NULL);
	NativeArcadeLaunch_Tick(NULL);
	CHECK(NativeArcadeLaunch_Accept(NULL, bytes, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_INACTIVE);
	CHECK(NativeArcadeLaunch_Accept(NULL, NULL, 0u) == NATIVE_ARCADE_LAUNCH_ACCEPT_INACTIVE);
	CHECK(NativeArcadeLaunch_ShouldSend(NULL) == 0);
	CHECK(!NativeArcadeLaunch_Compose(NULL, bytes, sizeof(bytes), &size));
	CHECK(!NativeArcadeLaunch_Compose(NULL, NULL, 0u, NULL));
	CHECK(size == 0u);
	CHECK(NativeArcadeLaunch_Status(NULL) == NATIVE_ARCADE_LAUNCH_PENDING);
	CHECK(!NativeArcadeLaunch_Active(NULL));
	CHECK(!NativeArcadeLaunch_Begin(NULL, 0u, NULL, 0u));
	return 0;
}

static int TestSaturation(void)
{
	struct NativeArcadeLaunchAgreement a;
	uint8_t digest[DIGEST_BYTES];
	uint8_t other[DIGEST_BYTES];
	uint8_t foreign[RECORD_BYTES];
	uint8_t valid[RECORD_BYTES];
	uint8_t self[RECORD_BYTES];
	uint8_t mismatch[RECORD_BYTES];

	MakeDigest(digest, 0x77u);
	MakeDigest(other, 0x78u);
	CHECK(NativeArcadeLaunch_Begin(&a, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, digest, UINT32_MAX));
	memset(foreign, 0, sizeof(foreign));
	WriteLe(foreign, UINT32_C(0x31534d4e), 4u);
	CraftRecord(valid, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, 0u, 1u, digest);
	CraftRecord(self, NATIVE_ARCADE_LAUNCH_ROLE_CAB1, 0u, 1u, digest);
	CraftRecord(mismatch, NATIVE_ARCADE_LAUNCH_ROLE_CAB2, 0u, 1u, other);

	a.acceptedCount = UINT32_MAX - 1u;
	a.foreignCount = UINT32_MAX - 1u;
	a.malformedCount = UINT32_MAX - 1u;
	a.selfCount = UINT32_MAX - 1u;
	a.mismatchCount = UINT32_MAX - 1u;
	for (uint32_t i = 0; i < 3u; i++)
	{
		CHECK(NativeArcadeLaunch_Accept(&a, valid, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED);
		CHECK(NativeArcadeLaunch_Accept(&a, foreign, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_FOREIGN);
		CHECK(NativeArcadeLaunch_Accept(&a, valid, 1u) == NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED);
		CHECK(NativeArcadeLaunch_Accept(&a, self, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_SELF);
		CHECK(NativeArcadeLaunch_Accept(&a, mismatch, RECORD_BYTES) == NATIVE_ARCADE_LAUNCH_ACCEPT_MISMATCH);
	}
	CHECK(a.acceptedCount == UINT32_MAX);
	CHECK(a.foreignCount == UINT32_MAX);
	CHECK(a.malformedCount == UINT32_MAX);
	CHECK(a.selfCount == UINT32_MAX);
	CHECK(a.mismatchCount == UINT32_MAX);

	/* ticksSinceCommit saturates too; with lingerTicks UINT32_MAX the cap is
	 * reached exactly there. */
	CHECK(a.status == NATIVE_ARCADE_LAUNCH_COMMITTED);
	a.ticksSinceCommit = UINT32_MAX - 1u;
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 1);
	NativeArcadeLaunch_Tick(&a);
	CHECK(a.ticksSinceCommit == UINT32_MAX);
	CHECK(NativeArcadeLaunch_ShouldSend(&a) == 0);
	NativeArcadeLaunch_Tick(&a);
	CHECK(a.ticksSinceCommit == UINT32_MAX);
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
	CHECK(TestBegin() == 0);
	CHECK(TestSymmetricCommit() == 0);
	CHECK(TestIgnoredRecords() == 0);
	CHECK(TestOneDirectionLoss() == 0);
	CHECK(TestHeardHalves() == 0);
	CHECK(TestCompose() == 0);
	CHECK(TestReset() == 0);
	CHECK(TestNullSafety() == 0);
	CHECK(TestSaturation() == 0);
	puts("native_arcade_launch_test: ok");
	return 0;
}
