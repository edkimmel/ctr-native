#include "platform/native_deterministic_rng.h"

#include <limits.h>
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

#define CHECK_U64(actual, expected)                                                                                                        \
	do                                                                                                                                  \
	{                                                                                                                                   \
		const uint64_t actualValue = (actual);                                                                                           \
		const uint64_t expectedValue = (expected);                                                                                       \
		if (actualValue != expectedValue)                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: got 0x%016llx expected 0x%016llx\n", __FILE__, __LINE__,                                     \
			        (unsigned long long)actualValue, (unsigned long long)expectedValue);                                             \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static int Encode(const struct NativeDeterministicRngBankV1 *bank,
	              uint8_t bytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES, NULL);
	return NativeDeterministicRngBankV1_Encode(&writer, bank) &&
	       (NativeCodecWriter_Size(&writer) == NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES);
}

static int BankBytesEqual(const struct NativeDeterministicRngBankV1 *first,
	                      const struct NativeDeterministicRngBankV1 *second)
{
	uint8_t firstBytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES];
	uint8_t secondBytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES];

	return Encode(first, firstBytes) && Encode(second, secondBytes) && (memcmp(firstBytes, secondBytes, sizeof(firstBytes)) == 0);
}

static int TestGoldenDerivationDrawsAndDigest(void)
{
	struct NativeDeterministicRngBankV1 bank;
	uint8_t bytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint64_t value;

	CHECK(strcmp(NATIVE_DETERMINISTIC_RNG_ALGORITHM_NAME, "xoshiro256**") == 0);
	CHECK(strcmp(NATIVE_DETERMINISTIC_RNG_DERIVATION_NAME, "SHA-256/CTRNRNG1") == 0);
	CHECK(strcmp(NATIVE_DETERMINISTIC_RNG_DIGEST_ALGORITHM_NAME, "SHA-256") == 0);
	CHECK(NativeDeterministicRngBankV1_EncodedSize() == sizeof(bytes));
	CHECK(NativeDeterministicRngBankV1_Init(&bank, UINT64_C(0x0123456789abcdef),
	                                       NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION));
	CHECK(NativeDeterministicRngBankV1_Validate(&bank));

	/* Golden V1 SHA-256-derived xoshiro states. */
	CHECK_U64(bank.streams[0].state[0], UINT64_C(0x2db0b9d7af7df5ed));
	CHECK_U64(bank.streams[0].state[1], UINT64_C(0x0a0a6bc14c077b9b));
	CHECK_U64(bank.streams[3].state[0], UINT64_C(0x265872b04f3e6487));
	CHECK_U64(bank.streams[10].state[3], UINT64_C(0xf4b3519ae6cbd46c));

	CHECK(NativeDeterministicRngBankV1_NextU64(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
	                                          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
	                                          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &value));
	CHECK_U64(value, UINT64_C(0xea787d2ea85d2061));
	CHECK(NativeDeterministicRngBankV1_NextU64(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP,
	                                          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
	                                          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &value));
	CHECK_U64(value, UINT64_C(0x60c79386cb71dad1));
	CHECK(NativeDeterministicRngBankV1_NextU64(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 0, 0, &value));
	CHECK_U64(value, UINT64_C(0x1b3d33533b6cae88));
	CHECK(NativeDeterministicRngBankV1_NextU64(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 7, 7, &value));
	CHECK_U64(value, UINT64_C(0x420ac30052d8dd6e));

	CHECK(Encode(&bank, bytes));
	/* Header and first stream descriptor are exact little-endian bytes. */
	CHECK(memcmp(bytes, "NRB1\x40\x02\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00", 16) == 0);
	CHECK(memcmp(&bytes[48], "CTAM\xff\x00\x00\x00", 8) == 0);
	CHECK(NativeDeterministicRngBankV1_Digest(&bank, digest));
	CHECK(memcmp(digest, "\x64\xbe\x5a\xe5\x5e\x4e\xd4\xad\x96\x93\xf6\x1c\x37\xd8\x99\x5b"
	                     "\x7c\xdf\x9a\xda\xee\xcd\xa0\xe0\xd1\xa7\x41\x83\x1d\x4a\xad\xa9",
	             sizeof(digest)) == 0);
	return 0;
}

static int TestStreamIsolationAndOrderInvariance(void)
{
	struct NativeDeterministicRngBankV1 baseline;
	struct NativeDeterministicRngBankV1 perturbed;
	struct NativeDeterministicRngBankV1 ascending;
	struct NativeDeterministicRngBankV1 descending;
	uint64_t expected;
	uint64_t actual;
	uint64_t ascendingValues[NATIVE_DETERMINISTIC_RNG_BOT_COUNT][16];
	uint64_t descendingValues[NATIVE_DETERMINISTIC_RNG_BOT_COUNT][16];

	CHECK(NativeDeterministicRngBankV1_Init(&baseline, UINT64_C(0x4a6f696e4f726465), 1));
	CHECK(NativeDeterministicRngBankV1_Init(&perturbed, UINT64_C(0x4a6f696e4f726465), 1));
	for (unsigned int i = 0; i < 64; i++)
	{
		CHECK(NativeDeterministicRngBankV1_NextU64(&perturbed, NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS,
		                                          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT,
		                                          NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, &actual));
		CHECK(NativeDeterministicRngBankV1_NextU64(&perturbed, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 2, 2, &actual));
	}
	CHECK(NativeDeterministicRngBankV1_NextU64(&baseline, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 5, 5, &expected));
	CHECK(NativeDeterministicRngBankV1_NextU64(&perturbed, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 5, 5, &actual));
	CHECK(actual == expected);
	CHECK(perturbed.streams[8].drawCount == 1);
	CHECK(perturbed.streams[1].drawCount == 64);
	CHECK(perturbed.streams[5].drawCount == 64);

	CHECK(NativeDeterministicRngBankV1_Init(&ascending, UINT64_C(0x4f72646572496e76), 1));
	CHECK(NativeDeterministicRngBankV1_Init(&descending, UINT64_C(0x4f72646572496e76), 1));
	for (unsigned int draw = 0; draw < 16; draw++)
	{
		for (uint8_t slot = 0; slot < NATIVE_DETERMINISTIC_RNG_BOT_COUNT; slot++)
		{
			CHECK(NativeDeterministicRngBankV1_NextU64(&ascending, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, slot, slot,
			                                          &ascendingValues[slot][draw]));
		}
		for (uint8_t reverse = NATIVE_DETERMINISTIC_RNG_BOT_COUNT; reverse != 0; reverse--)
		{
			const uint8_t slot = (uint8_t)(reverse - 1u);
			CHECK(NativeDeterministicRngBankV1_NextU64(&descending, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, slot, slot,
			                                          &descendingValues[slot][draw]));
		}
	}
	CHECK(memcmp(ascendingValues, descendingValues, sizeof(ascendingValues)) == 0);
	CHECK(BankBytesEqual(&ascending, &descending));
	return 0;
}

static int TestSeedSlotAndTagPerturbations(void)
{
	struct NativeDeterministicRngBankV1 first;
	struct NativeDeterministicRngBankV1 second;
	uint64_t match;
	uint64_t items;
	uint64_t hazards;
	uint64_t bot0;
	uint64_t bot1;
	uint64_t changedSeed;

	CHECK(NativeDeterministicRngBankV1_Init(&first, 1, 1));
	CHECK(NativeDeterministicRngBankV1_Init(&second, 2, 1));
	CHECK(NativeDeterministicRngBankV1_NextU64(&first, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP, 0xff, 0xff, &match));
	CHECK(NativeDeterministicRngBankV1_NextU64(&first, NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS, 0xff, 0xff, &items));
	CHECK(NativeDeterministicRngBankV1_NextU64(&first, NATIVE_DETERMINISTIC_RNG_STREAM_HAZARDS, 0xff, 0xff, &hazards));
	CHECK(NativeDeterministicRngBankV1_NextU64(&first, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 0, 0, &bot0));
	CHECK(NativeDeterministicRngBankV1_NextU64(&first, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 1, 1, &bot1));
	CHECK(NativeDeterministicRngBankV1_NextU64(&second, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP, 0xff, 0xff,
	                                          &changedSeed));
	CHECK(match != items && match != hazards && items != hazards);
	CHECK(bot0 != bot1);
	CHECK(match != changedSeed);
	CHECK(first.streams[0].state[0] != first.streams[1].state[0]);
	CHECK(first.streams[3].state[0] != first.streams[4].state[0]);
	return 0;
}

static int TestBoundedDraws(void)
{
	static const uint32_t bounds[] = {1, 2, 3, 7, 65535, UINT32_MAX};
	struct NativeDeterministicRngBankV1 bank;

	CHECK(NativeDeterministicRngBankV1_Init(&bank, UINT64_MAX, 1));
	for (size_t boundIndex = 0; boundIndex < sizeof(bounds) / sizeof(bounds[0]); boundIndex++)
	{
		for (unsigned int draw = 0; draw < 1000; draw++)
		{
			uint32_t value = UINT32_MAX;
			CHECK(NativeDeterministicRngBankV1_NextBoundedU32(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 4, 4,
			                                                 bounds[boundIndex], &value));
			CHECK(value < bounds[boundIndex]);
		}
	}
	CHECK(bank.streams[7].drawCount >= 6000);
	return 0;
}

static int TestOwnershipAndFailureAtomicity(void)
{
	struct NativeDeterministicRngBankV1 bank;
	struct NativeDeterministicRngBankV1 before;
	uint64_t value64;
	uint32_t value32;

	CHECK(NativeDeterministicRngBankV1_Init(&bank, 9, 1));
#define REJECT_U64(tag, slot, requester)                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		before = bank;                                                                                                                   \
		value64 = UINT64_C(0x5a5a5a5a5a5a5a5a);                                                                                        \
		CHECK(!NativeDeterministicRngBankV1_NextU64(&bank, (tag), (slot), (requester), &value64));                                     \
		CHECK(value64 == UINT64_C(0x5a5a5a5a5a5a5a5a));                                                                                \
		CHECK(BankBytesEqual(&bank, &before));                                                                                          \
	} while (0)
	REJECT_U64(NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 3, 2);
	REJECT_U64(NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 3, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT);
	REJECT_U64(NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 8, 8);
	REJECT_U64(NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS, 0, 0);
	REJECT_U64(NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, 0);
	REJECT_U64(UINT32_C(0x12345678), NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT, NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT);
#undef REJECT_U64

	before = bank;
	CHECK(!NativeDeterministicRngBankV1_NextU64(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 0, 0, NULL));
	CHECK(BankBytesEqual(&bank, &before));
	value32 = UINT32_C(0x5a5a5a5a);
	CHECK(!NativeDeterministicRngBankV1_NextBoundedU32(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 0, 0, 0, &value32));
	CHECK(value32 == UINT32_C(0x5a5a5a5a) && BankBytesEqual(&bank, &before));
	CHECK(!NativeDeterministicRngBankV1_NextBoundedU32(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, 0, 0, 2, NULL));
	CHECK(BankBytesEqual(&bank, &before));

	before = bank;
	bank.streams[0].state[0] = bank.streams[0].state[1] = bank.streams[0].state[2] = bank.streams[0].state[3] = 0;
	value64 = UINT64_C(0x5a5a5a5a5a5a5a5a);
	CHECK(!NativeDeterministicRngBankV1_NextU64(&bank, NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP, 0xff, 0xff, &value64));
	CHECK(value64 == UINT64_C(0x5a5a5a5a5a5a5a5a));
	bank = before;

	memset(&before, 0xa5, sizeof(before));
	bank = before;
	CHECK(!NativeDeterministicRngBankV1_Init(&bank, 1, 2));
	CHECK(memcmp(&bank, &before, sizeof(bank)) == 0);
	CHECK(!NativeDeterministicRngBankV1_Init(NULL, 1, 1));
	CHECK(!NativeDeterministicRngBankV1_Validate(NULL));
	return 0;
}

static int TestCheckpointRoundTrip(void)
{
	struct NativeDeterministicRngBankV1 original;
	struct NativeDeterministicRngBankV1 restored;
	uint8_t bytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES];
	uint8_t originalDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t restoredDigest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecReader reader;

	CHECK(NativeDeterministicRngBankV1_Init(&original, UINT64_C(0x434845434b504f49), 1));
	for (uint8_t slot = 0; slot < NATIVE_DETERMINISTIC_RNG_BOT_COUNT; slot++)
	{
		for (unsigned int draw = 0; draw <= slot; draw++)
		{
			uint64_t ignored;
			CHECK(NativeDeterministicRngBankV1_NextU64(&original, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, slot, slot,
			                                          &ignored));
		}
	}
	CHECK(Encode(&original, bytes));
	memset(&restored, 0xcc, sizeof(restored));
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeDeterministicRngBankV1_Decode(&reader, &restored));
	CHECK(reader.offset == sizeof(bytes));
	CHECK(BankBytesEqual(&original, &restored));
	CHECK(NativeDeterministicRngBankV1_Digest(&original, originalDigest));
	CHECK(NativeDeterministicRngBankV1_Digest(&restored, restoredDigest));
	CHECK(memcmp(originalDigest, restoredDigest, sizeof(originalDigest)) == 0);
	for (uint8_t slot = 0; slot < NATIVE_DETERMINISTIC_RNG_BOT_COUNT; slot++)
	{
		for (unsigned int draw = 0; draw < 32; draw++)
		{
			uint64_t first;
			uint64_t second;
			CHECK(NativeDeterministicRngBankV1_NextU64(&original, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, slot, slot, &first));
			CHECK(NativeDeterministicRngBankV1_NextU64(&restored, NATIVE_DETERMINISTIC_RNG_STREAM_BOT, slot, slot, &second));
			CHECK(first == second);
		}
	}
	return 0;
}

static int TestDecodeFailuresAreTransactional(void)
{
	struct NativeDeterministicRngBankV1 source;
	struct NativeDeterministicRngBankV1 output;
	struct NativeDeterministicRngBankV1 before;
	uint8_t bytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES + 1];
	uint8_t valid[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES];
	struct NativeCodecReader reader;

	CHECK(NativeDeterministicRngBankV1_Init(&source, UINT64_C(0xabcdef0123456789), 1));
	CHECK(Encode(&source, valid));
	memset(&output, 0x5a, sizeof(output));
	before = output;
	for (size_t size = 0; size < NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES; size++)
	{
		NativeCodecReader_Init(&reader, valid, size);
		CHECK(!NativeDeterministicRngBankV1_Decode(&reader, &output));
		CHECK(reader.offset == 0);
		CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	}
	memcpy(bytes, valid, sizeof(valid));
	bytes[sizeof(valid)] = 0;
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(!NativeDeterministicRngBankV1_Decode(&reader, &output));
	CHECK(reader.offset == 0 && memcmp(&output, &before, sizeof(output)) == 0);

#define REJECT_BYTE(index, value)                                                                                                          \
	do                                                                                                                                  \
	{                                                                                                                                   \
		memcpy(bytes, valid, sizeof(valid));                                                                                             \
		bytes[(index)] = (value);                                                                                                       \
		NativeCodecReader_Init(&reader, bytes, sizeof(valid));                                                                          \
		CHECK(!NativeDeterministicRngBankV1_Decode(&reader, &output));                                                                  \
		CHECK(reader.offset == 0 && memcmp(&output, &before, sizeof(output)) == 0);                                                     \
	} while (0)
	REJECT_BYTE(0, 0);
	REJECT_BYTE(4, 0);
	REJECT_BYTE(8, 2);
	REJECT_BYTE(12, 2);
	REJECT_BYTE(24, 10);
	for (size_t i = 28; i < 48; i++) REJECT_BYTE(i, 1);
	for (size_t stream = 0; stream < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; stream++)
	{
		const size_t offset = 48u + stream * 48u;
		REJECT_BYTE(offset, (uint8_t)(valid[offset] ^ 1u));
		REJECT_BYTE(offset + 4u, (uint8_t)(valid[offset + 4u] ^ 1u));
		REJECT_BYTE(offset + 5u, (uint8_t)(valid[offset + 5u] ^ 1u));
		REJECT_BYTE(offset + 6u, 1);
		REJECT_BYTE(offset + 7u, 1);
	}
#undef REJECT_BYTE

	/* All-zero state is invalid even when descriptor/header bytes are valid. */
	memcpy(bytes, valid, sizeof(valid));
	memset(&bytes[56], 0, 32);
	NativeCodecReader_Init(&reader, bytes, sizeof(valid));
	CHECK(!NativeDeterministicRngBankV1_Decode(&reader, &output));
	CHECK(reader.offset == 0 && memcmp(&output, &before, sizeof(output)) == 0);
	CHECK(!NativeDeterministicRngBankV1_Decode(NULL, &output));
	NativeCodecReader_Init(&reader, valid, sizeof(valid));
	CHECK(!NativeDeterministicRngBankV1_Decode(&reader, NULL) && reader.offset == 0);
	return 0;
}

static int TestNonzeroOffsetAndEncodeFailures(void)
{
	struct NativeDeterministicRngBankV1 bank;
	struct NativeDeterministicRngBankV1 decoded;
	uint8_t framed[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES + 3] = {0xaa, 0xbb, 0xcc};
	uint8_t before[sizeof(framed)];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t digestBefore[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;
	struct NativeCodecDigest64 codecDigest;
	uint64_t codecDigestBefore;

	CHECK(NativeDeterministicRngBankV1_Init(&bank, 0, 1));
	NativeCodecWriter_Init(&writer, framed, sizeof(framed), NULL);
	CHECK(NativeCodecWriter_WriteBytes(&writer, "\xaa\xbb\xcc", 3));
	CHECK(NativeDeterministicRngBankV1_Encode(&writer, &bank));
	CHECK(writer.offset == sizeof(framed));
	NativeCodecReader_Init(&reader, framed, sizeof(framed));
	reader.offset = 3;
	CHECK(NativeDeterministicRngBankV1_Decode(&reader, &decoded));
	CHECK(reader.offset == sizeof(framed) && BankBytesEqual(&bank, &decoded));

	memset(framed, 0xcc, sizeof(framed));
	memcpy(before, framed, sizeof(before));
	NativeCodecDigest64_Init(&codecDigest);
	NativeCodecWriter_Init(&writer, framed, NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES - 1, &codecDigest);
	codecDigestBefore = codecDigest.value;
	CHECK(!NativeDeterministicRngBankV1_Encode(&writer, &bank));
	CHECK(writer.offset == 0 && writer.failed == 0 && codecDigest.value == codecDigestBefore);
	CHECK(memcmp(framed, before, sizeof(framed)) == 0);

	NativeCodecWriter_Init(&writer, framed, sizeof(framed), &codecDigest);
	CHECK(NativeCodecWriter_WriteU8(&writer, 0xaa));
	memcpy(before, framed, sizeof(before));
	codecDigestBefore = codecDigest.value;
	bank.bankVersion++;
	CHECK(!NativeDeterministicRngBankV1_Encode(&writer, &bank));
	CHECK(writer.offset == 1 && writer.failed == 0 && codecDigest.value == codecDigestBefore);
	CHECK(memcmp(framed, before, sizeof(framed)) == 0);
	CHECK(!NativeDeterministicRngBankV1_Encode(NULL, &bank));
	CHECK(!NativeDeterministicRngBankV1_Encode(&writer, NULL));

	memset(digest, 0xcc, sizeof(digest));
	memcpy(digestBefore, digest, sizeof(digest));
	CHECK(!NativeDeterministicRngBankV1_Digest(&bank, digest));
	CHECK(memcmp(digest, digestBefore, sizeof(digest)) == 0);
	CHECK(!NativeDeterministicRngBankV1_Digest(&bank, NULL));
	CHECK(!NativeDeterministicRngBankV1_Digest(NULL, digest));
	return 0;
}

int main(void)
{
	if (TestGoldenDerivationDrawsAndDigest() != 0 || TestStreamIsolationAndOrderInvariance() != 0 ||
	    TestSeedSlotAndTagPerturbations() != 0 || TestBoundedDraws() != 0 || TestOwnershipAndFailureAtomicity() != 0 ||
	    TestCheckpointRoundTrip() != 0 || TestDecodeFailuresAreTransactional() != 0 ||
	    TestNonzeroOffsetAndEncodeFailures() != 0)
	{
		return 1;
	}
	puts("native deterministic RNG bank tests passed");
	return 0;
}
