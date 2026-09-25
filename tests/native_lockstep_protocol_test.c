#include "platform/native_lockstep_protocol.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define BUNDLE_BYTES NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES
#define BODY_BYTES NATIVE_LOCKSTEP_BUNDLE_V1_DIGEST_OFFSET

static const uint8_t g_identity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES] = {
	0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u
};
static const uint32_t g_protocolVersion = UINT32_C(0x01020304);
static const uint32_t g_inputDelay = 2u;

/* The fixture the hand-written expected byte array below describes. */
static void MakeBundle(struct NativeLockstepBundleV1 *bundle)
{
	static const uint8_t buttons0[2] = { 0x34u, 0x12u };
	static const uint8_t analog0[4] = { 0x01u, 0x02u, 0x03u, 0x04u };
	static const uint8_t buttons1[2] = { 0xFFu, 0x00u };
	static const uint8_t analog1[4] = { 0x10u, 0x20u, 0x30u, 0x40u };

	memset(bundle, 0, sizeof(*bundle));
	bundle->protocolVersion = g_protocolVersion;
	memcpy(bundle->matchIdentity, g_identity, sizeof(bundle->matchIdentity));
	bundle->frameIndex = UINT32_C(0x0A0B0C0D);
	bundle->inputDelay = g_inputDelay;
	bundle->senderSlot = 1u;
	bundle->padCount = 2u;
	bundle->verifiedPresent = 1u;
	bundle->reserved0 = 0u;
	bundle->pads[0].slotIndex = 0u;
	bundle->pads[0].pad.status = 0x5Au;
	bundle->pads[0].pad.id = 0x01u;
	memcpy(bundle->pads[0].pad.buttons, buttons0, sizeof(buttons0));
	memcpy(bundle->pads[0].pad.analog, analog0, sizeof(analog0));
	bundle->pads[0].pad.connected = 0x01u;
	bundle->pads[1].slotIndex = 3u;
	bundle->pads[1].pad.status = 0x11u;
	bundle->pads[1].pad.id = 0x02u;
	memcpy(bundle->pads[1].pad.buttons, buttons1, sizeof(buttons1));
	memcpy(bundle->pads[1].pad.analog, analog1, sizeof(analog1));
	bundle->pads[1].pad.connected = 0x00u;
	/* Lag consistent: verifiedFrameIndex + g_inputDelay + 1 == frameIndex. */
	bundle->verifiedFrameIndex = UINT32_C(0x0A0B0C0A);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		bundle->verifiedDomainDigests[i] = UINT64_C(0x0102030405060708) + i;
	}
	bundle->verifiedCombinedDigest = UINT64_C(0xF0DEBC9A78563412);
}

/* One used pad entry, so entry 1 exercises the unused-entry encoding. */
static void MakeSinglePadBundle(struct NativeLockstepBundleV1 *bundle)
{
	memset(bundle, 0, sizeof(*bundle));
	bundle->protocolVersion = g_protocolVersion;
	memcpy(bundle->matchIdentity, g_identity, sizeof(bundle->matchIdentity));
	bundle->frameIndex = 7u;
	bundle->inputDelay = g_inputDelay;
	bundle->senderSlot = 0u;
	bundle->padCount = 1u;
	bundle->verifiedPresent = 0u;
	bundle->pads[0].slotIndex = 0u;
	bundle->pads[0].pad.status = 0x01u;
	bundle->pads[0].pad.connected = 1u;
	bundle->pads[1].slotIndex = NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT;
}

static int SamePad(const struct NativeLockstepBundlePadV1 *left, const struct NativeLockstepBundlePadV1 *right)
{
	return (left->slotIndex == right->slotIndex) && (left->pad.status == right->pad.status) && (left->pad.id == right->pad.id) &&
	       (memcmp(left->pad.buttons, right->pad.buttons, sizeof(left->pad.buttons)) == 0) &&
	       (memcmp(left->pad.analog, right->pad.analog, sizeof(left->pad.analog)) == 0) && (left->pad.connected == right->pad.connected);
}

/* Field-for-field, because the struct has interior padding memcmp would read. */
static int SameBundle(const struct NativeLockstepBundleV1 *left, const struct NativeLockstepBundleV1 *right)
{
	if ((left->protocolVersion != right->protocolVersion) || (memcmp(left->matchIdentity, right->matchIdentity, 8u) != 0) ||
	    (left->frameIndex != right->frameIndex) || (left->inputDelay != right->inputDelay) || (left->senderSlot != right->senderSlot) ||
	    (left->padCount != right->padCount) || (left->verifiedPresent != right->verifiedPresent) || (left->reserved0 != right->reserved0) ||
	    (left->verifiedFrameIndex != right->verifiedFrameIndex) || (left->verifiedCombinedDigest != right->verifiedCombinedDigest) ||
	    (memcmp(left->reserved1, right->reserved1, sizeof(left->reserved1)) != 0))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY; i++)
	{
		if (!SamePad(&left->pads[i], &right->pads[i]))
		{
			return 0;
		}
	}
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		if (left->verifiedDomainDigests[i] != right->verifiedDomainDigests[i])
		{
			return 0;
		}
	}
	return 1;
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

static int Encode(const struct NativeLockstepBundleV1 *bundle, uint8_t *bytes)
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, BUNDLE_BYTES, NULL);
	CHECK(NativeLockstepBundleV1_Encode(&writer, bundle));
	CHECK(NativeCodecWriter_Size(&writer) == BUNDLE_BYTES);
	return 0;
}

/*
 * Decode must fail with exactly the expected cause and must leave both the
 * caller's reader and the output bundle untouched.
 */
static int DecodeFails(const uint8_t *bytes, size_t size, uint32_t expectedProtocolVersion, uint32_t expectedInputDelay,
                       uint32_t expectedCause)
{
	struct NativeCodecReader reader;
	struct NativeCodecReader readerBefore;
	struct NativeLockstepBundleV1 out;
	struct NativeLockstepBundleV1 outBefore;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_NONE;

	MakeSinglePadBundle(&out);
	outBefore = out;
	NativeCodecReader_Init(&reader, bytes, size);
	readerBefore = reader;
	CHECK(!NativeLockstepBundleV1_Decode(&reader, g_identity, expectedProtocolVersion, expectedInputDelay, &out, &cause));
	CHECK(cause == expectedCause);
	CHECK(memcmp(&reader, &readerBefore, sizeof(reader)) == 0);
	CHECK(SameBundle(&out, &outBefore));
	/* A NULL fault sink is accepted and must behave identically. */
	NativeCodecReader_Init(&reader, bytes, size);
	CHECK(!NativeLockstepBundleV1_Decode(&reader, g_identity, expectedProtocolVersion, expectedInputDelay, &out, NULL));
	CHECK(SameBundle(&out, &outBefore));
	return 0;
}

static int DecodeOk(const uint8_t *bytes, const struct NativeLockstepBundleV1 *expected)
{
	struct NativeCodecReader reader;
	struct NativeLockstepBundleV1 out;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_BAD_MAGIC;

	memset(&out, 0xCD, sizeof(out));
	NativeCodecReader_Init(&reader, bytes, BUNDLE_BYTES);
	CHECK(NativeLockstepBundleV1_Decode(&reader, g_identity, g_protocolVersion, g_inputDelay, &out, &cause));
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_NONE);
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	CHECK(SameBundle(&out, expected));
	return 0;
}

/* One patched byte, resealed, must report exactly one cause. */
static int PatchedCause(const uint8_t *valid, size_t offset, uint8_t value, uint32_t expectedCause)
{
	uint8_t bytes[BUNDLE_BYTES];

	memcpy(bytes, valid, BUNDLE_BYTES);
	bytes[offset] = value;
	Reseal(bytes);
	return DecodeFails(bytes, sizeof(bytes), g_protocolVersion, g_inputDelay, expectedCause);
}

int main(void)
{
	static const uint8_t expectedBody[BODY_BYTES] = {
		/* 0: magic 0x31424c4e */ 0x4Eu, 0x4Cu, 0x42u, 0x31u,
		/* 4: bundleVersion 1 */ 0x01u, 0x00u, 0x00u, 0x00u,
		/* 8: encodedSize 128 */ 0x80u, 0x00u, 0x00u, 0x00u,
		/* 12: protocolVersion 0x01020304 */ 0x04u, 0x03u, 0x02u, 0x01u,
		/* 16: matchIdentity */ 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u,
		/* 24: frameIndex 0x0A0B0C0D */ 0x0Du, 0x0Cu, 0x0Bu, 0x0Au,
		/* 28: inputDelay 2 */ 0x02u, 0x00u, 0x00u, 0x00u,
		/* 32: senderSlot, padCount, verifiedPresent, reserved0 */ 0x01u, 0x02u, 0x01u, 0x00u,
		/* 36: pad entry 0 */ 0x00u, 0x5Au, 0x01u, 0x34u, 0x12u, 0x01u, 0x02u, 0x03u, 0x04u, 0x01u,
		/* 46: pad entry 1 */ 0x03u, 0x11u, 0x02u, 0xFFu, 0x00u, 0x10u, 0x20u, 0x30u, 0x40u, 0x00u,
		/* 56: verifiedFrameIndex 0x0A0B0C0A */ 0x0Au, 0x0Cu, 0x0Bu, 0x0Au,
		/* 60: verifiedDomainDigests[0..5] */
		0x08u, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
		0x09u, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
		0x0Au, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
		0x0Bu, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
		0x0Cu, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
		0x0Du, 0x07u, 0x06u, 0x05u, 0x04u, 0x03u, 0x02u, 0x01u,
		/* 108: verifiedCombinedDigest 0xF0DEBC9A78563412 */ 0x12u, 0x34u, 0x56u, 0x78u, 0x9Au, 0xBCu, 0xDEu, 0xF0u,
		/* 116: reserved1 */ 0x00u, 0x00u, 0x00u, 0x00u
	};
	struct NativeLockstepBundleV1 bundle;
	struct NativeLockstepBundleV1 single;
	struct NativeLockstepBundleV1 invalid;
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	uint8_t bytes[BUNDLE_BYTES];
	uint8_t singleBytes[BUNDLE_BYTES];
	uint8_t patched[BUNDLE_BYTES];
	uint8_t offsetBuffer[8u + BUNDLE_BYTES];
	uint8_t narrow[BUNDLE_BYTES];
	uint32_t causes[12];
	size_t causeCount = 0u;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_NONE;

	CHECK(NativeLockstepBundleV1_EncodedSize() == BUNDLE_BYTES);
	CHECK(BUNDLE_BYTES == 128u);
	CHECK(BODY_BYTES == 120u);

	/* Round trip. */
	MakeBundle(&bundle);
	CHECK(NativeLockstepBundleV1_Validate(&bundle));
	CHECK(!NativeLockstepBundleV1_Validate(NULL));
	CHECK(Encode(&bundle, bytes) == 0);
	CHECK(DecodeOk(bytes, &bundle) == 0);
	MakeSinglePadBundle(&single);
	CHECK(NativeLockstepBundleV1_Validate(&single));
	CHECK(Encode(&single, singleBytes) == 0);
	CHECK(DecodeOk(singleBytes, &single) == 0);

	/* Byte-exact little-endian layout at the offsets of the wire table. */
	CHECK(memcmp(bytes, expectedBody, sizeof(expectedBody)) == 0);
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, expectedBody, sizeof(expectedBody));
	for (uint32_t i = 0; i < 8u; i++)
	{
		CHECK(bytes[BODY_BYTES + i] == (uint8_t)(digest.value >> (8u * i)));
	}
	/* The digest never digests itself: it is a function of the body alone. */
	CHECK(digest.value != 0u);

	/* Encoding is position independent inside a wider writer. */
	memset(offsetBuffer, 0xCDu, sizeof(offsetBuffer));
	NativeCodecWriter_Init(&writer, offsetBuffer, sizeof(offsetBuffer), NULL);
	CHECK(NativeCodecWriter_WriteBytes(&writer, expectedBody, 8u));
	CHECK(NativeLockstepBundleV1_Encode(&writer, &bundle));
	CHECK(NativeCodecWriter_Size(&writer) == 8u + BUNDLE_BYTES);
	CHECK(memcmp(&offsetBuffer[8], bytes, BUNDLE_BYTES) == 0);

	/* Oversize rejection: 127 bytes of capacity writes nothing at all. */
	memset(narrow, 0xCDu, sizeof(narrow));
	NativeCodecWriter_Init(&writer, narrow, BUNDLE_BYTES - 1u, NULL);
	CHECK(!NativeLockstepBundleV1_Encode(&writer, &bundle));
	CHECK(NativeCodecWriter_Size(&writer) == 0u);
	CHECK(NativeCodecWriter_Ok(&writer));
	for (size_t i = 0; i < sizeof(narrow); i++)
	{
		CHECK(narrow[i] == 0xCDu);
	}
	/* The same rejection when the remaining capacity, not the total, is short. */
	NativeCodecWriter_Init(&writer, offsetBuffer, sizeof(offsetBuffer), NULL);
	CHECK(NativeCodecWriter_WriteBytes(&writer, expectedBody, 9u));
	CHECK(!NativeLockstepBundleV1_Encode(&writer, &bundle));
	CHECK(NativeCodecWriter_Size(&writer) == 9u);

	/* Encode refuses an invalid record and an invalid writer. */
	invalid = bundle;
	invalid.padCount = NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY + 1u;
	CHECK(!NativeLockstepBundleV1_Validate(&invalid));
	memset(narrow, 0xCDu, sizeof(narrow));
	NativeCodecWriter_Init(&writer, narrow, sizeof(narrow), NULL);
	CHECK(!NativeLockstepBundleV1_Encode(&writer, &invalid));
	CHECK(NativeCodecWriter_Size(&writer) == 0u);
	for (size_t i = 0; i < sizeof(narrow); i++)
	{
		CHECK(narrow[i] == 0xCDu);
	}
	CHECK(!NativeLockstepBundleV1_Encode(NULL, &bundle));

	/* Truncation: every short length is rejected as a size fault. */
	for (size_t size = 0; size < BUNDLE_BYTES; size++)
	{
		CHECK(DecodeFails(bytes, size, g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_BAD_SIZE) == 0);
	}
	/* An overlong reader is rejected too: the record is exactly one width. */
	CHECK(DecodeFails(offsetBuffer, BUNDLE_BYTES + 1u, g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_BAD_SIZE) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_SIZE;

	/* Bad magic, bundle version, and encoded size. */
	CHECK(PatchedCause(bytes, 3u, 0x32u, NATIVE_LOCKSTEP_FAULT_BAD_MAGIC) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_MAGIC;
	CHECK(PatchedCause(bytes, 4u, 0x02u, NATIVE_LOCKSTEP_FAULT_BAD_VERSION) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_VERSION;
	CHECK(PatchedCause(bytes, 8u, 0x81u, NATIVE_LOCKSTEP_FAULT_BAD_SIZE) == 0);

	/* Any of the eight match-identity bytes differing is rejected. */
	for (size_t i = 0; i < sizeof(g_identity); i++)
	{
		CHECK(PatchedCause(bytes, 16u + i, (uint8_t)(g_identity[i] ^ 0xFFu), NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY) == 0);
	}
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY;

	/* Peer expectation mismatches need no patch: the record is well formed. */
	CHECK(DecodeFails(bytes, BUNDLE_BYTES, g_protocolVersion + 1u, g_inputDelay, NATIVE_LOCKSTEP_FAULT_PROTOCOL_VERSION) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_PROTOCOL_VERSION;
	CHECK(DecodeFails(bytes, BUNDLE_BYTES, g_protocolVersion, g_inputDelay + 1u, NATIVE_LOCKSTEP_FAULT_INPUT_DELAY) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_INPUT_DELAY;

	/* A corrupted digest, and a corrupted body byte with a stale digest. */
	memcpy(patched, bytes, sizeof(patched));
	patched[BODY_BYTES] ^= 0x01u;
	CHECK(DecodeFails(patched, sizeof(patched), g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	memcpy(patched, bytes, sizeof(patched));
	patched[BUNDLE_BYTES - 1u] ^= 0x80u;
	CHECK(DecodeFails(patched, sizeof(patched), g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	memcpy(patched, bytes, sizeof(patched));
	patched[24] ^= 0x01u; /* frameIndex, digest left stale. */
	CHECK(DecodeFails(patched, sizeof(patched), g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	memcpy(patched, bytes, sizeof(patched));
	patched[40] ^= 0x20u; /* pad entry 0 body, digest left stale. */
	CHECK(DecodeFails(patched, sizeof(patched), g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_DIGEST;

	/* Nonzero reserved bytes. */
	CHECK(PatchedCause(bytes, 35u, 0x01u, NATIVE_LOCKSTEP_FAULT_BAD_RESERVED) == 0);
	for (size_t i = 0; i < NATIVE_LOCKSTEP_BUNDLE_V1_RESERVED1_BYTES; i++)
	{
		CHECK(PatchedCause(bytes, 116u + i, 0x01u, NATIVE_LOCKSTEP_FAULT_BAD_RESERVED) == 0);
	}
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_RESERVED;

	/* Sender slot and pad count bounds. */
	CHECK(PatchedCause(bytes, 32u, (uint8_t)NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	CHECK(PatchedCause(bytes, 32u, 0xFFu, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	CHECK(PatchedCause(bytes, 33u, (uint8_t)(NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY + 1u), NATIVE_LOCKSTEP_FAULT_BAD_PAD_COUNT) == 0);
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_PAD_COUNT;

	/* A used entry must name a real slot; two entries cannot share one. */
	CHECK(PatchedCause(bytes, 36u, 0xFFu, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	CHECK(PatchedCause(bytes, 46u, (uint8_t)NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	CHECK(PatchedCause(bytes, 46u, 0x00u, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	/* An unused entry is 0xFF plus nine zero bytes and nothing else. */
	CHECK(PatchedCause(singleBytes, 46u, 0x05u, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	CHECK(PatchedCause(singleBytes, 46u, 0x00u, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	for (size_t i = 1u; i < 10u; i++)
	{
		CHECK(PatchedCause(singleBytes, 46u + i, 0x01u, NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	}
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_BAD_SLOT;

	/*
	 * Verified-block shape: verifiedPresent is 0 or 1, and 0 requires zero
	 * verification fields.  This is a malformed record, not a lag violation.
	 */
	CHECK(PatchedCause(bytes, 34u, 0x00u, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0);
	CHECK(PatchedCause(bytes, 34u, 0x02u, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0);
	CHECK(PatchedCause(bytes, 34u, 0xFFu, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0);
	CHECK(PatchedCause(singleBytes, 56u, 0x01u, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0);  /* verifiedFrameIndex */
	CHECK(PatchedCause(singleBytes, 60u, 0x01u, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0);  /* domain digest 0 */
	CHECK(PatchedCause(singleBytes, 103u, 0x80u, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0); /* domain digest 5 */
	CHECK(PatchedCause(singleBytes, 108u, 0x01u, NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE) == 0); /* combined digest */
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE;

	/*
	 * The verified-digest lag invariant: with verifiedPresent == 1 the digest
	 * lags the carried input frame by exactly D + 1 frames.  The fixture is at
	 * exact lag, so one frame either way is the only patch needed.
	 */
	CHECK(PatchedCause(bytes, 56u, 0x0Bu, NATIVE_LOCKSTEP_FAULT_VERIFY_LAG) == 0); /* one frame too new */
	CHECK(PatchedCause(bytes, 56u, 0x09u, NATIVE_LOCKSTEP_FAULT_VERIFY_LAG) == 0); /* one frame too old */
	causes[causeCount++] = NATIVE_LOCKSTEP_FAULT_VERIFY_LAG;

	/* Exact lag at another nonzero delay, and the cases the lag rule excludes. */
	{
		struct NativeLockstepBundleV1 lag;
		struct NativeLockstepBundleV1 out;
		struct NativeCodecReader reader;
		uint8_t lagBytes[BUNDLE_BYTES];

		MakeBundle(&lag);
		lag.inputDelay = 5u;
		lag.frameIndex = 100u;
		lag.verifiedFrameIndex = 94u; /* 94 + 5 + 1 == 100 */
		CHECK(Encode(&lag, lagBytes) == 0);
		memset(&out, 0xCDu, sizeof(out));
		cause = NATIVE_LOCKSTEP_FAULT_BAD_MAGIC;
		NativeCodecReader_Init(&reader, lagBytes, BUNDLE_BYTES);
		CHECK(NativeLockstepBundleV1_Decode(&reader, g_identity, g_protocolVersion, 5u, &out, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_FAULT_NONE);
		CHECK(NativeCodecReader_Remaining(&reader) == 0);
		CHECK(SameBundle(&out, &lag));

		/* 64-bit arithmetic: a verifiedFrameIndex that wraps uint32 is no pass. */
		lag.inputDelay = g_inputDelay;
		lag.frameIndex = 2u;
		lag.verifiedFrameIndex = UINT32_MAX;
		CHECK(Encode(&lag, lagBytes) == 0);
		CHECK(DecodeFails(lagBytes, BUNDLE_BYTES, g_protocolVersion, g_inputDelay, NATIVE_LOCKSTEP_FAULT_VERIFY_LAG) == 0);

		/* verifiedPresent == 0 is never lag-checked, whatever the frame index. */
		MakeSinglePadBundle(&lag);
		lag.frameIndex = 0u;
		CHECK(Encode(&lag, lagBytes) == 0);
		memset(&out, 0xCDu, sizeof(out));
		cause = NATIVE_LOCKSTEP_FAULT_BAD_MAGIC;
		NativeCodecReader_Init(&reader, lagBytes, BUNDLE_BYTES);
		CHECK(NativeLockstepBundleV1_Decode(&reader, g_identity, g_protocolVersion, g_inputDelay, &out, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_FAULT_NONE);
		CHECK(SameBundle(&out, &lag));
		lag.frameIndex = UINT32_MAX;
		CHECK(Encode(&lag, lagBytes) == 0);
		cause = NATIVE_LOCKSTEP_FAULT_BAD_MAGIC;
		NativeCodecReader_Init(&reader, lagBytes, BUNDLE_BYTES);
		CHECK(NativeLockstepBundleV1_Decode(&reader, g_identity, g_protocolVersion, g_inputDelay, &out, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_FAULT_NONE);
		CHECK(SameBundle(&out, &lag));
	}

	/*
	 * Every covered failure path reported a distinct, non-zero cause.  causes[]
	 * lists the decoder's causes only.  WINDOW_OVERRUN and CONFLICTING_INPUT
	 * come from the input window, and VERIFY_AHEAD (LR-S5) is a lockstep
	 * session cause that the decoder never returns and the wire never carries,
	 * so it is deliberately absent here.  The list must never claim it.
	 */
	CHECK(causeCount == sizeof(causes) / sizeof(causes[0]));
	for (size_t i = 0; i < causeCount; i++)
	{
		CHECK(causes[i] != NATIVE_LOCKSTEP_FAULT_NONE);
		CHECK(causes[i] != NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD);
		for (size_t j = 0; j < i; j++)
		{
			CHECK(causes[i] != causes[j]);
		}
	}
	/* Appended after VERIFY_SHAPE, never renumbered. */
	CHECK(NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE == 14);
	CHECK(NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD == 15);

	/* NULL arguments are size faults and touch nothing. */
	CHECK(!NativeLockstepBundleV1_Decode(NULL, g_identity, g_protocolVersion, g_inputDelay, &bundle, &cause));
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_BAD_SIZE);
	{
		struct NativeCodecReader reader;
		NativeCodecReader_Init(&reader, bytes, BUNDLE_BYTES);
		cause = NATIVE_LOCKSTEP_FAULT_NONE;
		CHECK(!NativeLockstepBundleV1_Decode(&reader, NULL, g_protocolVersion, g_inputDelay, &bundle, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_FAULT_BAD_SIZE);
		cause = NATIVE_LOCKSTEP_FAULT_NONE;
		CHECK(!NativeLockstepBundleV1_Decode(&reader, g_identity, g_protocolVersion, g_inputDelay, NULL, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_FAULT_BAD_SIZE);
		CHECK(NativeCodecReader_Remaining(&reader) == BUNDLE_BYTES);
	}
	return 0;
}
