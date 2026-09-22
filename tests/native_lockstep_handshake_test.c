#include "platform/native_lockstep_handshake.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define HANDSHAKE_BYTES NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES
#define BODY_BYTES NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET
#define CONFIG_BYTES NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES
/* The nested config region starts right after the four-byte header fields. */
#define CONFIG_OFFSET 16u
/* Nested NativeMatchConfigV1 field offset of lapCount, relative to CONFIG_OFFSET. */
#define CONFIG_LAP_COUNT_OFFSET 32u

static void FillConfig(struct NativeMatchConfigV1 *config, uint32_t trackID)
{
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = trackID;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
	for (uint8_t i = 0; i <= 5; i++)
	{
		config->slots[i].characterID = i;
		config->slots[i].difficulty = i < 2 ? 2 : 3;
	}
}

static void FillOneCabConfig(struct NativeMatchConfigV1 *config, uint32_t trackID)
{
	NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = trackID;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
	for (uint8_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config->slots[i].characterID = i;
		config->slots[i].difficulty = 2;
	}
}

static void MakeMessage(struct NativeLockstepHandshakeMessageV1 *message, uint8_t messageType, uint8_t senderRole, uint8_t rejectReason,
                        const struct NativeMatchConfigV1 *config)
{
	memset(message, 0, sizeof(*message));
	message->messageType = messageType;
	message->senderRole = senderRole;
	message->rejectReason = rejectReason;
	message->config = *config;
}

static int EncodeConfig(const struct NativeMatchConfigV1 *config, uint8_t bytes[CONFIG_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, CONFIG_BYTES, NULL);
	CHECK(NativeMatchConfigV1_Encode(&writer, config));
	CHECK(NativeCodecWriter_Size(&writer) == CONFIG_BYTES);
	return 0;
}

static int EncodeMessage(const struct NativeLockstepHandshakeMessageV1 *message, uint8_t bytes[HANDSHAKE_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, HANDSHAKE_BYTES, NULL);
	CHECK(NativeLockstepHandshakeMessageV1_Encode(&writer, message));
	CHECK(NativeCodecWriter_Size(&writer) == HANDSHAKE_BYTES);
	return 0;
}

/*
 * Field-for-field, comparing the nested config by its own re-encoded bytes
 * rather than a raw struct memcmp, so a false pass from interior padding is
 * impossible.
 */
static int DecodeOk(const uint8_t *bytes, const struct NativeLockstepHandshakeMessageV1 *expected)
{
	struct NativeCodecReader reader;
	struct NativeLockstepHandshakeMessageV1 out;
	uint32_t cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MAGIC;
	uint8_t outConfigBytes[CONFIG_BYTES];
	uint8_t expectedConfigBytes[CONFIG_BYTES];

	memset(&out, 0xCD, sizeof(out));
	NativeCodecReader_Init(&reader, bytes, HANDSHAKE_BYTES);
	CHECK(NativeLockstepHandshakeMessageV1_Decode(&reader, &out, &cause));
	CHECK(cause == NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE);
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	CHECK(out.messageType == expected->messageType);
	CHECK(out.senderRole == expected->senderRole);
	CHECK(out.rejectReason == expected->rejectReason);
	CHECK(out.reserved0 == expected->reserved0);
	CHECK(memcmp(out.reserved1, expected->reserved1, sizeof(out.reserved1)) == 0);
	CHECK(EncodeConfig(&out.config, outConfigBytes) == 0);
	CHECK(EncodeConfig(&expected->config, expectedConfigBytes) == 0);
	CHECK(memcmp(outConfigBytes, expectedConfigBytes, sizeof(outConfigBytes)) == 0);
	return 0;
}

/* Decode must fail with exactly the expected cause and leave both the
 * caller's reader and the output message untouched. */
static int DecodeFails(const uint8_t *bytes, size_t size, uint32_t expectedCause)
{
	struct NativeCodecReader reader;
	struct NativeCodecReader readerBefore;
	struct NativeLockstepHandshakeMessageV1 out;
	struct NativeLockstepHandshakeMessageV1 outBefore;
	uint32_t cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;

	memset(&out, 0xCD, sizeof(out));
	outBefore = out;
	NativeCodecReader_Init(&reader, bytes, size);
	readerBefore = reader;
	CHECK(!NativeLockstepHandshakeMessageV1_Decode(&reader, &out, &cause));
	CHECK(cause == expectedCause);
	CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
	CHECK(memcmp(&out, &outBefore, sizeof(out)) == 0);
	/* A NULL fault sink is accepted and behaves identically. */
	NativeCodecReader_Init(&reader, bytes, size);
	CHECK(!NativeLockstepHandshakeMessageV1_Decode(&reader, &out, NULL));
	CHECK(memcmp(&out, &outBefore, sizeof(out)) == 0);
	return 0;
}

/* Recomputes the FNV-1a 64 body digest so a patched record is well sealed. */
static void Reseal(uint8_t *bytes)
{
	struct NativeCodecDigest64 digest;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, BODY_BYTES);
	for (uint32_t i = 0; i < 8u; i++)
	{
		bytes[BODY_BYTES + i] = (uint8_t)(digest.value >> (8u * i));
	}
}

/* One patched byte, resealed, must report exactly one cause. */
static int PatchedCause(const uint8_t *valid, size_t offset, uint8_t value, uint32_t expectedCause)
{
	uint8_t bytes[HANDSHAKE_BYTES];

	memcpy(bytes, valid, HANDSHAKE_BYTES);
	bytes[offset] = value;
	Reseal(bytes);
	return DecodeFails(bytes, sizeof(bytes), expectedCause);
}

/*
 * Codec round trip, byte-exactness, and truncation for a HELLO message.
 */
static int TestRoundTripAndByteExactness(void)
{
	static const uint8_t expectedHeader[16] = {
		/* 0: magic 0x31484c4e "NLH1" */ 0x4Eu, 0x4Cu, 0x48u, 0x31u,
		/* 4: messageVersion 1 */ 0x01u, 0x00u, 0x00u, 0x00u,
		/* 8: encodedSize 284 */ 0x1Cu, 0x01u, 0x00u, 0x00u,
		/* 12: messageType HELLO */ 0x01u,
		/* 13: senderRole CAB2_HUMAN */ 0x02u,
		/* 14: rejectReason NONE */ 0x00u,
		/* 15: reserved0 */ 0x00u
	};
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshakeMessageV1 hello;
	uint8_t bytes[HANDSHAKE_BYTES];
	uint8_t expectedConfigBytes[CONFIG_BYTES];

	CHECK(NativeLockstepHandshakeMessageV1_EncodedSize() == HANDSHAKE_BYTES);
	CHECK(HANDSHAKE_BYTES == 284u);
	CHECK(BODY_BYTES == 276u);

	FillConfig(&config, UINT32_C(0x01020304));
	MakeMessage(&hello, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
	           (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE, &config);
	CHECK(EncodeMessage(&hello, bytes) == 0);
	CHECK(DecodeOk(bytes, &hello) == 0);

	/* Byte-exact little-endian layout at the header offsets. */
	CHECK(memcmp(bytes, expectedHeader, sizeof(expectedHeader)) == 0);

	/* The nested config region re-decodes independently and matches the
	 * proposed config's own re-encoded bytes. */
	{
		struct NativeCodecReader nestedReader;
		struct NativeMatchConfigV1 nestedConfig;

		NativeCodecReader_Init(&nestedReader, &bytes[CONFIG_OFFSET], CONFIG_BYTES);
		CHECK(NativeMatchConfigV1_Decode(&nestedReader, &nestedConfig));
		CHECK(NativeCodecReader_Remaining(&nestedReader) == 0);
	}
	CHECK(EncodeConfig(&config, expectedConfigBytes) == 0);
	CHECK(memcmp(&bytes[CONFIG_OFFSET], expectedConfigBytes, sizeof(expectedConfigBytes)) == 0);

	/* reserved1 is all zero. */
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_HANDSHAKE_V1_RESERVED1_BYTES; i++)
	{
		CHECK(bytes[272u + i] == 0u);
	}

	/* The trailing digest is FNV-1a 64 over bytes 0..275 and never digests
	 * itself. */
	{
		struct NativeCodecDigest64 digest;

		NativeCodecDigest64_Init(&digest);
		NativeCodecDigest64_Update(&digest, bytes, BODY_BYTES);
		for (uint32_t i = 0; i < 8u; i++)
		{
			CHECK(bytes[BODY_BYTES + i] == (uint8_t)(digest.value >> (8u * i)));
		}
		CHECK(digest.value != 0u);
	}

	/* Truncation: every short length is rejected as a size fault, and an
	 * overlong reader is rejected too. */
	for (size_t size = 0; size < HANDSHAKE_BYTES; size++)
	{
		CHECK(DecodeFails(bytes, size, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE) == 0);
	}
	{
		uint8_t overlong[HANDSHAKE_BYTES + 1u];

		memcpy(overlong, bytes, HANDSHAKE_BYTES);
		overlong[HANDSHAKE_BYTES] = 0u;
		CHECK(DecodeFails(overlong, sizeof(overlong), NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE) == 0);
	}

	/* NULL arguments are size faults and touch nothing. */
	{
		struct NativeCodecReader reader;
		struct NativeLockstepHandshakeMessageV1 message;
		uint32_t cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;

		CHECK(!NativeLockstepHandshakeMessageV1_Decode(NULL, &message, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE);
		NativeCodecReader_Init(&reader, bytes, HANDSHAKE_BYTES);
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;
		CHECK(!NativeLockstepHandshakeMessageV1_Decode(&reader, NULL, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE);
		CHECK(NativeCodecReader_Remaining(&reader) == HANDSHAKE_BYTES);
	}
	CHECK(!NativeLockstepHandshakeMessageV1_Encode(NULL, &hello));
	return 0;
}

/*
 * Every codec fault cause is independently reachable and distinct: bad
 * magic/version/size/digest, a corrupted body byte with a stale digest,
 * nonzero reserved0/reserved1, an out-of-range messageType/senderRole, a
 * rejectReason inconsistent with messageType, and the two nested-config
 * causes (structurally malformed vs. merely Validate-failing).
 */
static int TestFaultCauses(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshakeMessageV1 hello;
	struct NativeLockstepHandshakeMessageV1 accept;
	struct NativeLockstepHandshakeMessageV1 reject;
	uint8_t bytes[HANDSHAKE_BYTES];
	uint8_t acceptBytes[HANDSHAKE_BYTES];
	uint8_t rejectBytes[HANDSHAKE_BYTES];
	uint8_t patched[HANDSHAKE_BYTES];
	uint32_t causes[10];
	size_t causeCount = 0u;

	FillConfig(&config, UINT32_C(0x01020304));
	MakeMessage(&hello, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
	           (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE, &config);
	CHECK(EncodeMessage(&hello, bytes) == 0);
	MakeMessage(&accept, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
	           (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE, &config);
	CHECK(EncodeMessage(&accept, acceptBytes) == 0);
	MakeMessage(&reject, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
	           (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED, &config);
	CHECK(EncodeMessage(&reject, rejectBytes) == 0);

	/* Bad magic, message version, and encoded size. */
	CHECK(PatchedCause(bytes, 3u, 0x32u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MAGIC) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MAGIC;
	CHECK(PatchedCause(bytes, 4u, 0x02u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_VERSION) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_VERSION;
	CHECK(PatchedCause(bytes, 8u, 0x1Du, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE;

	/* A corrupted digest, and a corrupted body byte with a stale digest. */
	memcpy(patched, bytes, sizeof(patched));
	patched[BODY_BYTES] ^= 0x01u;
	CHECK(DecodeFails(patched, sizeof(patched), NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_DIGEST) == 0);
	memcpy(patched, bytes, sizeof(patched));
	patched[HANDSHAKE_BYTES - 1u] ^= 0x80u;
	CHECK(DecodeFails(patched, sizeof(patched), NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_DIGEST) == 0);
	memcpy(patched, bytes, sizeof(patched));
	patched[CONFIG_OFFSET] ^= 0x01u; /* nested config body, digest left stale. */
	CHECK(DecodeFails(patched, sizeof(patched), NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_DIGEST) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_DIGEST;

	/* Nonzero reserved0 and every reserved1 byte. */
	CHECK(PatchedCause(bytes, 15u, 0x01u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_RESERVED) == 0);
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_HANDSHAKE_V1_RESERVED1_BYTES; i++)
	{
		CHECK(PatchedCause(bytes, 272u + i, 0x01u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_RESERVED) == 0);
	}
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_RESERVED;

	/* messageType outside {HELLO, ACCEPT, REJECT}. */
	CHECK(PatchedCause(bytes, 12u, 0x00u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE) == 0);
	CHECK(PatchedCause(bytes, 12u, 0x04u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE) == 0);
	CHECK(PatchedCause(bytes, 12u, 0xFFu, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE;

	/* senderRole outside {CAB1_HUMAN, CAB2_HUMAN}. */
	CHECK(PatchedCause(bytes, 13u, (uint8_t)NATIVE_MATCH_SLOT_ROLE_INACTIVE, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE) == 0);
	CHECK(PatchedCause(bytes, 13u, (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE) == 0);
	CHECK(PatchedCause(bytes, 13u, 0xFFu, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE;

	/* rejectReason shape: nonzero on HELLO/ACCEPT, and zero or out of range
	 * on REJECT. */
	CHECK(PatchedCause(bytes, 14u, 0x01u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON) == 0);
	CHECK(PatchedCause(acceptBytes, 14u, 0x01u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON) == 0);
	CHECK(PatchedCause(rejectBytes, 14u, 0x00u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON) == 0);
	CHECK(PatchedCause(rejectBytes, 14u, 0x06u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON;

	/* A structurally malformed nested config (bad nested magic byte). */
	CHECK(PatchedCause(bytes, CONFIG_OFFSET, 0x00u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_DECODE) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_DECODE;

	/* A nested config that decodes structurally but fails
	 * NativeMatchConfigV1_Validate (lapCount == 0), even though the outer
	 * message decoded and digest-checked fine. */
	CHECK(PatchedCause(bytes, CONFIG_OFFSET + CONFIG_LAP_COUNT_OFFSET, 0x00u, NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_INVALID) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_INVALID;

	/* Every covered failure path reported a distinct, non-zero cause. */
	CHECK(causeCount == sizeof(causes) / sizeof(causes[0]));
	for (size_t i = 0; i < causeCount; i++)
	{
		CHECK(causes[i] != NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE);
		for (size_t j = 0; j < i; j++)
		{
			CHECK(causes[i] != causes[j]);
		}
	}
	return 0;
}

/*
 * Two in-process handshakes with byte-identical proposals and complementary
 * roles reach COMPLETE with byte-identical agreed configs on both sides.
 */
static int TestCompleteHandshake(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloFromA[HANDSHAKE_BYTES];
	uint8_t helloFromB[HANDSHAKE_BYTES];
	uint8_t replyFromA[HANDSHAKE_BYTES];
	uint8_t replyFromB[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	const struct NativeLockstepHandshakeResult *resultA;
	const struct NativeLockstepHandshakeResult *resultB;
	uint8_t agreedBytesA[CONFIG_BYTES];
	uint8_t agreedBytesB[CONFIG_BYTES];
	uint8_t proposedBytes[CONFIG_BYTES];

	FillConfig(&config, UINT32_C(0x01020304));

	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_IDLE);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_IDLE);
	CHECK(NativeLockstepHandshake_Result(&a) == NULL);
	CHECK(NativeLockstepHandshake_Begin(&a, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT);
	CHECK(NativeLockstepHandshake_Result(&a) == NULL);
	CHECK(NativeLockstepHandshake_Result(&b) == NULL);

	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloFromA, sizeof(helloFromA), &sizeA) == 1);
	CHECK(sizeA == HANDSHAKE_BYTES);
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloFromB, sizeof(helloFromB), &sizeB) == 1);
	CHECK(sizeB == HANDSHAKE_BYTES);

	CHECK(NativeLockstepHandshake_AcceptMessage(&b, helloFromA, sizeA) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_AcceptMessage(&a, helloFromB, sizeB) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);

	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);

	resultA = NativeLockstepHandshake_Result(&a);
	resultB = NativeLockstepHandshake_Result(&b);
	CHECK(resultA != NULL);
	CHECK(resultB != NULL);
	CHECK(resultA->rejectReason == 0u);
	CHECK(resultB->rejectReason == 0u);
	CHECK(resultA->peerRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
	CHECK(resultB->peerRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);

	/* Byte-identical, not just struct-equal: compare the encoded bytes to
	 * avoid a false pass from interior padding. */
	CHECK(EncodeConfig(&resultA->agreedConfig, agreedBytesA) == 0);
	CHECK(EncodeConfig(&resultB->agreedConfig, agreedBytesB) == 0);
	CHECK(EncodeConfig(&config, proposedBytes) == 0);
	CHECK(memcmp(agreedBytesA, proposedBytes, sizeof(proposedBytes)) == 0);
	CHECK(memcmp(agreedBytesB, proposedBytes, sizeof(proposedBytes)) == 0);
	CHECK(memcmp(agreedBytesA, agreedBytesB, sizeof(agreedBytesA)) == 0);

	/* Both now compose ACCEPT; the peer's ACCEPT only confirms, it never
	 * moves anything that was not already earned in step 3. */
	CHECK(NativeLockstepHandshake_ComposeMessage(&a, replyFromA, sizeof(replyFromA), &sizeA) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, replyFromB, sizeof(replyFromB), &sizeB) == 1);
	CHECK(NativeLockstepHandshake_AcceptMessage(&b, replyFromA, sizeA) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_AcceptMessage(&a, replyFromB, sizeB) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	CHECK(memcmp(NativeLockstepHandshake_Result(&a), resultA, sizeof(*resultA)) == 0);
	return 0;
}

/* Both sides claiming the same human role reach REJECTED with ROLE_CONFLICT. */
static int TestSameRoleCollision(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloFromA[HANDSHAKE_BYTES];
	uint8_t helloFromB[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	const struct NativeLockstepHandshakeResult *resultA;
	const struct NativeLockstepHandshakeResult *resultB;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloFromA, sizeof(helloFromA), &sizeA) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloFromB, sizeof(helloFromB), &sizeB) == 1);

	CHECK(NativeLockstepHandshake_AcceptMessage(&b, helloFromA, sizeA) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_AcceptMessage(&a, helloFromB, sizeB) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);

	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	resultA = NativeLockstepHandshake_Result(&a);
	resultB = NativeLockstepHandshake_Result(&b);
	CHECK(resultA != NULL);
	CHECK(resultB != NULL);
	CHECK(resultA->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_ROLE_CONFLICT);
	CHECK(resultB->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_ROLE_CONFLICT);
	return 0;
}

/* Configs differing only in trackID reach REJECTED with CONFIG_MISMATCH. */
static int TestConfigMismatch(void)
{
	struct NativeMatchConfigV1 configA;
	struct NativeMatchConfigV1 configB;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloFromA[HANDSHAKE_BYTES];
	uint8_t helloFromB[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	const struct NativeLockstepHandshakeResult *resultA;
	const struct NativeLockstepHandshakeResult *resultB;

	FillConfig(&configA, UINT32_C(0x01020304));
	FillConfig(&configB, UINT32_C(0x0A0B0C0D));
	CHECK(configA.trackID != configB.trackID);

	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &configA, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &configB, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloFromA, sizeof(helloFromA), &sizeA) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloFromB, sizeof(helloFromB), &sizeB) == 1);

	CHECK(NativeLockstepHandshake_AcceptMessage(&b, helloFromA, sizeA) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_AcceptMessage(&a, helloFromB, sizeB) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);

	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	resultA = NativeLockstepHandshake_Result(&a);
	resultB = NativeLockstepHandshake_Result(&b);
	CHECK(resultA != NULL);
	CHECK(resultB != NULL);
	CHECK(resultA->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH);
	CHECK(resultB->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH);
	return 0;
}

/*
 * Once REJECTED, a second, different incoming message (a now-valid HELLO)
 * must not change the already-latched result: a raw memcmp of the result
 * before and after proves nothing was written, not just that it reads the
 * same.
 */
static int TestLatchOnce(void)
{
	struct NativeMatchConfigV1 configA;
	struct NativeMatchConfigV1 configB;
	struct NativeMatchConfigV1 configMatching;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake mismatchedSender;
	struct NativeLockstepHandshake matchingSender;
	uint8_t helloFromB[HANDSHAKE_BYTES];
	uint8_t secondHello[HANDSHAKE_BYTES];
	size_t sizeB = 0;
	size_t secondSize = 0;
	struct NativeLockstepHandshakeResult latched;
	const struct NativeLockstepHandshakeResult *result;

	FillConfig(&configA, UINT32_C(0x01020304));
	FillConfig(&configB, UINT32_C(0x0A0B0C0D));
	FillConfig(&configMatching, UINT32_C(0x01020304));

	NativeLockstepHandshake_Init(&a);
	CHECK(NativeLockstepHandshake_Begin(&a, &configA, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);

	NativeLockstepHandshake_Init(&mismatchedSender);
	CHECK(NativeLockstepHandshake_Begin(&mismatchedSender, &configB, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&mismatchedSender, helloFromB, sizeof(helloFromB), &sizeB) == 1);
	CHECK(NativeLockstepHandshake_AcceptMessage(&a, helloFromB, sizeB) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	result = NativeLockstepHandshake_Result(&a);
	CHECK(result != NULL);
	CHECK(result->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH);
	latched = *result;

	NativeLockstepHandshake_Init(&matchingSender);
	CHECK(NativeLockstepHandshake_Begin(&matchingSender, &configMatching, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&matchingSender, secondHello, sizeof(secondHello), &secondSize) == 1);
	CHECK(NativeLockstepHandshake_AcceptMessage(&a, secondHello, secondSize) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	result = NativeLockstepHandshake_Result(&a);
	CHECK(result != NULL);
	CHECK(memcmp(result, &latched, sizeof(latched)) == 0);
	return 0;
}

/*
 * A REJECT arriving while still HELLO_SENT moves the local side to REJECTED
 * carrying the peer-reported reason, without ever receiving a HELLO itself.
 */
static int TestPeerRejectFirst(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake local;
	struct NativeLockstepHandshakeMessageV1 rejectMsg;
	uint8_t rejectBytes[HANDSHAKE_BYTES];
	const struct NativeLockstepHandshakeResult *result;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&local);
	CHECK(NativeLockstepHandshake_Begin(&local, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Mode(&local) == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT);

	MakeMessage(&rejectMsg, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
	           (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_VERSION_MISMATCH, &config);
	CHECK(EncodeMessage(&rejectMsg, rejectBytes) == 0);

	CHECK(NativeLockstepHandshake_AcceptMessage(&local, rejectBytes, sizeof(rejectBytes)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&local) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	result = NativeLockstepHandshake_Result(&local);
	CHECK(result != NULL);
	CHECK(result->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_VERSION_MISMATCH);
	CHECK(result->peerRole == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);

	/* The outgoing message now acknowledges the reject, carrying the peer's
	 * own reported reason through verbatim. */
	{
		uint8_t ackBytes[HANDSHAKE_BYTES];
		struct NativeCodecReader reader;
		struct NativeLockstepHandshakeMessageV1 ack;
		uint32_t cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;
		size_t ackSize = 0;

		CHECK(NativeLockstepHandshake_ComposeMessage(&local, ackBytes, sizeof(ackBytes), &ackSize) == 1);
		NativeCodecReader_Init(&reader, ackBytes, ackSize);
		CHECK(NativeLockstepHandshakeMessageV1_Decode(&reader, &ack, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE);
		CHECK(ack.messageType == (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT);
		CHECK(ack.rejectReason == (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_VERSION_MISMATCH);
	}
	return 0;
}

/*
 * An ACCEPT arriving while still HELLO_SENT is a no-op: mode stays
 * HELLO_SENT, Result() stays NULL, and the side still requires its own
 * AcceptMessage(HELLO) call to reach COMPLETE.
 */
static int TestPeerAcceptFirstIsNoOp(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake local;
	struct NativeLockstepHandshake peer;
	struct NativeLockstepHandshakeMessageV1 acceptMsg;
	uint8_t acceptBytes[HANDSHAKE_BYTES];
	uint8_t peerHelloBytes[HANDSHAKE_BYTES];
	size_t peerHelloSize = 0;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&local);
	CHECK(NativeLockstepHandshake_Begin(&local, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);

	MakeMessage(&acceptMsg, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
	           (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE, &config);
	CHECK(EncodeMessage(&acceptMsg, acceptBytes) == 0);

	CHECK(NativeLockstepHandshake_AcceptMessage(&local, acceptBytes, sizeof(acceptBytes)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&local) == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT);
	CHECK(NativeLockstepHandshake_Result(&local) == NULL);

	/* Completion is still earned only by this side's own HELLO comparison. */
	NativeLockstepHandshake_Init(&peer);
	CHECK(NativeLockstepHandshake_Begin(&peer, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(&peer, peerHelloBytes, sizeof(peerHelloBytes), &peerHelloSize) == 1);
	CHECK(NativeLockstepHandshake_AcceptMessage(&local, peerHelloBytes, peerHelloSize) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&local) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	CHECK(NativeLockstepHandshake_Result(&local) != NULL);
	return 0;
}

/* A malformed incoming record while HELLO_SENT is REJECTED with MALFORMED,
 * not a propagated decode crash. */
static int TestMalformedMessageRejects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake local;
	uint8_t garbage[HANDSHAKE_BYTES];
	const struct NativeLockstepHandshakeResult *result;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&local);
	CHECK(NativeLockstepHandshake_Begin(&local, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);

	memset(garbage, 0xEE, sizeof(garbage));
	CHECK(NativeLockstepHandshake_AcceptMessage(&local, garbage, sizeof(garbage)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT);
	CHECK(NativeLockstepHandshake_Mode(&local) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	result = NativeLockstepHandshake_Result(&local);
	CHECK(result != NULL);
	CHECK(result->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED);

	/* Idempotent on an already-terminal handshake: no re-latch, still FAULT. */
	CHECK(NativeLockstepHandshake_AcceptMessage(&local, garbage, sizeof(garbage)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT);
	CHECK(memcmp(NativeLockstepHandshake_Result(&local), result, sizeof(*result)) == 0);
	return 0;
}

/* AcceptMessage before Begin (mode IDLE) is caller misuse, not wire data. */
static int TestAcceptBeforeBegin(void)
{
	struct NativeLockstepHandshake hs;
	struct NativeLockstepHandshake before;
	uint8_t bytes[HANDSHAKE_BYTES];

	memset(bytes, 0, sizeof(bytes));
	NativeLockstepHandshake_Init(&hs);
	before = hs;
	CHECK(NativeLockstepHandshake_AcceptMessage(&hs, bytes, sizeof(bytes)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE);
	CHECK(memcmp(&hs, &before, sizeof(hs)) == 0);
	CHECK(NativeLockstepHandshake_Mode(&hs) == NATIVE_LOCKSTEP_HANDSHAKE_IDLE);
	CHECK(NativeLockstepHandshake_Result(&hs) == NULL);

	CHECK(NativeLockstepHandshake_AcceptMessage(NULL, bytes, sizeof(bytes)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE);
	CHECK(NativeLockstepHandshake_AcceptMessage(&hs, NULL, sizeof(bytes)) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE);
	CHECK(memcmp(&hs, &before, sizeof(hs)) == 0);
	return 0;
}

/* Begin's own rejection modes leave hs untouched on every path. */
static int TestBeginRejects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeMatchConfigV1 oneCab;
	struct NativeLockstepHandshake hs;
	struct NativeLockstepHandshake before;
	uint8_t slotIndex = 0;

	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&hs);
	before = hs;

	CHECK(NativeLockstepHandshake_Begin(NULL, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 0);
	CHECK(NativeLockstepHandshake_Begin(&hs, NULL, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 0);
	CHECK(memcmp(&hs, &before, sizeof(hs)) == 0);

	/* Invalid role values. */
	CHECK(NativeLockstepHandshake_Begin(&hs, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_INACTIVE) == 0);
	CHECK(NativeLockstepHandshake_Begin(&hs, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT) == 0);
	CHECK(memcmp(&hs, &before, sizeof(hs)) == 0);

	/* An invalid config. */
	{
		struct NativeMatchConfigV1 invalid = config;

		invalid.lapCount = 0;
		CHECK(!NativeMatchConfigV1_Validate(&invalid));
		CHECK(NativeLockstepHandshake_Begin(&hs, &invalid, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 0);
		CHECK(memcmp(&hs, &before, sizeof(hs)) == 0);
	}

	/* A role this config's slots do not actually assign: the one-cab profile
	 * has no CAB2_HUMAN slot. */
	FillOneCabConfig(&oneCab, UINT32_C(0x0A0B0C0D));
	CHECK(NativeMatchConfigV1_Validate(&oneCab));
	CHECK(!NativeMatchConfigV1_FindRoleSlot(&oneCab, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &slotIndex));
	CHECK(NativeLockstepHandshake_Begin(&hs, &oneCab, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 0);
	CHECK(memcmp(&hs, &before, sizeof(hs)) == 0);

	/* Mode must be IDLE: a second Begin is refused. */
	CHECK(NativeLockstepHandshake_Begin(&hs, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&hs, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 0);
	CHECK(NativeLockstepHandshake_Mode(&hs) == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT);
	return 0;
}

/* ComposeMessage's own rejection modes: IDLE, NULL arguments, short capacity. */
static int TestComposeMessageRejects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake idle;
	struct NativeLockstepHandshake hs;
	uint8_t bytes[HANDSHAKE_BYTES];
	size_t size = SIZE_MAX;

	FillConfig(&config, UINT32_C(0x01020304));

	NativeLockstepHandshake_Init(&idle);
	CHECK(NativeLockstepHandshake_ComposeMessage(&idle, bytes, sizeof(bytes), &size) == 0);
	CHECK(size == SIZE_MAX);

	NativeLockstepHandshake_Init(&hs);
	CHECK(NativeLockstepHandshake_Begin(&hs, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_ComposeMessage(NULL, bytes, sizeof(bytes), &size) == 0);
	CHECK(NativeLockstepHandshake_ComposeMessage(&hs, NULL, sizeof(bytes), &size) == 0);
	CHECK(NativeLockstepHandshake_ComposeMessage(&hs, bytes, sizeof(bytes), NULL) == 0);
	CHECK(size == SIZE_MAX);
	CHECK(NativeLockstepHandshake_ComposeMessage(&hs, bytes, HANDSHAKE_BYTES - 1u, &size) == 0);
	CHECK(size == SIZE_MAX);
	CHECK(NativeLockstepHandshake_ComposeMessage(&hs, bytes, HANDSHAKE_BYTES, &size) == 1);
	CHECK(size == HANDSHAKE_BYTES);
	return 0;
}

int main(void)
{
	CHECK(TestRoundTripAndByteExactness() == 0);
	CHECK(TestFaultCauses() == 0);
	CHECK(TestCompleteHandshake() == 0);
	CHECK(TestSameRoleCollision() == 0);
	CHECK(TestConfigMismatch() == 0);
	CHECK(TestLatchOnce() == 0);
	CHECK(TestPeerRejectFirst() == 0);
	CHECK(TestPeerAcceptFirstIsNoOp() == 0);
	CHECK(TestMalformedMessageRejects() == 0);
	CHECK(TestAcceptBeforeBegin() == 0);
	CHECK(TestBeginRejects() == 0);
	CHECK(TestComposeMessageRejects() == 0);
	return 0;
}
