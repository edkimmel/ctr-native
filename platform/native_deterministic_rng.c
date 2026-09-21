#include "platform/native_deterministic_rng.h"

#include <string.h>

static uint64_t NativeDeterministicRng_RotateLeft(uint64_t value, unsigned int bits)
{
	return (value << bits) | (value >> (64u - bits));
}

static uint64_t NativeDeterministicRng_LoadU64LE(const uint8_t bytes[8])
{
	return (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8) | ((uint64_t)bytes[2] << 16) | ((uint64_t)bytes[3] << 24) |
	       ((uint64_t)bytes[4] << 32) | ((uint64_t)bytes[5] << 40) | ((uint64_t)bytes[6] << 48) | ((uint64_t)bytes[7] << 56);
}

static void NativeDeterministicRng_StoreU32LE(uint8_t bytes[4], uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void NativeDeterministicRng_StoreU64LE(uint8_t bytes[8], uint64_t value)
{
	for (unsigned int i = 0; i < 8; i++)
	{
		bytes[i] = (uint8_t)(value >> (i * 8u));
	}
}

static int NativeDeterministicRng_StateIsNonzero(const uint64_t state[4])
{
	return (state[0] | state[1] | state[2] | state[3]) != 0;
}

static int NativeDeterministicRng_ExpectedDescriptor(uint8_t index, uint32_t *tag, uint8_t *stableSlot)
{
	if ((tag == NULL) || (stableSlot == NULL) || (index >= NATIVE_DETERMINISTIC_RNG_STREAM_COUNT))
	{
		return 0;
	}
	if (index == 0)
	{
		*tag = NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP;
		*stableSlot = NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT;
	}
	else if (index == 1)
	{
		*tag = NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS;
		*stableSlot = NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT;
	}
	else if (index == 2)
	{
		*tag = NATIVE_DETERMINISTIC_RNG_STREAM_HAZARDS;
		*stableSlot = NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT;
	}
	else
	{
		*tag = NATIVE_DETERMINISTIC_RNG_STREAM_BOT;
		*stableSlot = (uint8_t)(index - 3u);
	}
	return 1;
}

static int NativeDeterministicRng_StreamIndex(uint32_t tag, uint8_t stableSlot, uint8_t requesterSlot, uint8_t *index)
{
	if (index == NULL)
	{
		return 0;
	}
	if ((tag == NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP) || (tag == NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS) ||
	    (tag == NATIVE_DETERMINISTIC_RNG_STREAM_HAZARDS))
	{
		if ((stableSlot != NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT) ||
		    (requesterSlot != NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT))
		{
			return 0;
		}
		*index = tag == NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP ? 0u
		         : tag == NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS      ? 1u
		                                                               : 2u;
		return 1;
	}
	if ((tag != NATIVE_DETERMINISTIC_RNG_STREAM_BOT) || (stableSlot >= NATIVE_DETERMINISTIC_RNG_BOT_COUNT) ||
	    (requesterSlot != stableSlot))
	{
		return 0;
	}
	*index = (uint8_t)(3u + stableSlot);
	return 1;
}

/*
 * V1 derivation hashes this exact 32-byte message:
 * "CTRNRNG1", masterSeed LE64, derivationVersion LE32, tag LE32,
 * stableSlot as LE32, and a zero LE32 derivation block counter.
 */
static void NativeDeterministicRng_Derive(uint64_t masterSeed, uint32_t derivationVersion, uint32_t tag,
	                                      uint8_t stableSlot, uint64_t state[4])
{
	uint8_t message[32] = {'C', 'T', 'R', 'N', 'R', 'N', 'G', '1'};
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeSha256 sha;

	NativeDeterministicRng_StoreU64LE(&message[8], masterSeed);
	NativeDeterministicRng_StoreU32LE(&message[16], derivationVersion);
	NativeDeterministicRng_StoreU32LE(&message[20], tag);
	NativeDeterministicRng_StoreU32LE(&message[24], stableSlot);
	NativeDeterministicRng_StoreU32LE(&message[28], 0);
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, message, sizeof(message));
	NativeSha256_Final(&sha, digest);
	for (unsigned int i = 0; i < 4; i++)
	{
		state[i] = NativeDeterministicRng_LoadU64LE(&digest[i * 8u]);
	}
	/* xoshiro256** forbids only its all-zero state. Keep repair portable. */
	if (!NativeDeterministicRng_StateIsNonzero(state))
	{
		state[0] = UINT64_C(0x9e3779b97f4a7c15);
	}
}

static uint64_t NativeDeterministicRng_Next(struct NativeDeterministicRngStreamV1 *stream)
{
	const uint64_t result = NativeDeterministicRng_RotateLeft(stream->state[1] * UINT64_C(5), 7) * UINT64_C(9);
	const uint64_t temporary = stream->state[1] << 17;

	stream->state[2] ^= stream->state[0];
	stream->state[3] ^= stream->state[1];
	stream->state[1] ^= stream->state[2];
	stream->state[0] ^= stream->state[3];
	stream->state[2] ^= temporary;
	stream->state[3] = NativeDeterministicRng_RotateLeft(stream->state[3], 45);
	stream->drawCount++;
	return result;
}

int NativeDeterministicRngBankV1_Init(struct NativeDeterministicRngBankV1 *bank, uint64_t masterSeed,
	                                  uint32_t derivationVersion)
{
	struct NativeDeterministicRngBankV1 candidate;

	if ((bank == NULL) || (derivationVersion != NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION))
	{
		return 0;
	}
	memset(&candidate, 0, sizeof(candidate));
	candidate.bankVersion = NATIVE_DETERMINISTIC_RNG_BANK_V1_VERSION;
	candidate.derivationVersion = derivationVersion;
	candidate.masterSeed = masterSeed;
	for (uint8_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		struct NativeDeterministicRngStreamV1 *stream = &candidate.streams[i];
		if (!NativeDeterministicRng_ExpectedDescriptor(i, &stream->tag, &stream->stableSlot))
		{
			return 0;
		}
		stream->streamIndex = i;
		NativeDeterministicRng_Derive(masterSeed, derivationVersion, stream->tag, stream->stableSlot, stream->state);
	}
	*bank = candidate;
	return 1;
}

int NativeDeterministicRngBankV1_InitInPlace(struct NativeDeterministicRngBankV1 *bank, uint64_t masterSeed,
	                                         uint32_t derivationVersion)
{
	if ((bank == NULL) || (derivationVersion != NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION))
	{
		return 0;
	}
	memset(bank, 0, sizeof(*bank));
	bank->bankVersion = NATIVE_DETERMINISTIC_RNG_BANK_V1_VERSION;
	bank->derivationVersion = derivationVersion;
	bank->masterSeed = masterSeed;
	for (uint8_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		struct NativeDeterministicRngStreamV1 *stream = &bank->streams[i];
		if (!NativeDeterministicRng_ExpectedDescriptor(i, &stream->tag, &stream->stableSlot))
		{
			return 0;
		}
		stream->streamIndex = i;
		NativeDeterministicRng_Derive(masterSeed, derivationVersion, stream->tag, stream->stableSlot, stream->state);
	}
	return 1;
}

int NativeDeterministicRngBankV1_Validate(const struct NativeDeterministicRngBankV1 *bank)
{
	if ((bank == NULL) || (bank->bankVersion != NATIVE_DETERMINISTIC_RNG_BANK_V1_VERSION) ||
	    (bank->derivationVersion != NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION))
	{
		return 0;
	}
	for (uint8_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		uint32_t tag;
		uint8_t stableSlot;
		const struct NativeDeterministicRngStreamV1 *stream = &bank->streams[i];
		if (!NativeDeterministicRng_ExpectedDescriptor(i, &tag, &stableSlot) || (stream->tag != tag) ||
		    (stream->stableSlot != stableSlot) || (stream->streamIndex != i) ||
		    !NativeDeterministicRng_StateIsNonzero(stream->state))
		{
			return 0;
		}
	}
	return 1;
}

int NativeDeterministicRngBankV1_NextU64(struct NativeDeterministicRngBankV1 *bank, uint32_t streamTag,
	                                     uint8_t stableSlot, uint8_t requesterSlot, uint64_t *value)
{
	uint8_t index;
	uint64_t candidate;

	if ((value == NULL) || !NativeDeterministicRngBankV1_Validate(bank) ||
	    !NativeDeterministicRng_StreamIndex(streamTag, stableSlot, requesterSlot, &index))
	{
		return 0;
	}
	candidate = NativeDeterministicRng_Next(&bank->streams[index]);
	*value = candidate;
	return 1;
}

int NativeDeterministicRngBankV1_NextU32(struct NativeDeterministicRngBankV1 *bank, uint32_t streamTag,
	                                     uint8_t stableSlot, uint8_t requesterSlot, uint32_t *value)
{
	uint64_t wide;

	if ((value == NULL) || !NativeDeterministicRngBankV1_NextU64(bank, streamTag, stableSlot, requesterSlot, &wide))
	{
		return 0;
	}
	*value = (uint32_t)(wide >> 32);
	return 1;
}

int NativeDeterministicRngBankV1_NextBoundedU32(struct NativeDeterministicRngBankV1 *bank, uint32_t streamTag,
	                                            uint8_t stableSlot, uint8_t requesterSlot,
	                                            uint32_t exclusiveUpperBound, uint32_t *value)
{
	struct NativeDeterministicRngBankV1 candidate;
	uint32_t randomValue;
	const uint32_t threshold = exclusiveUpperBound == 0 ? 0 : (uint32_t)(0u - exclusiveUpperBound) % exclusiveUpperBound;

	if ((bank == NULL) || (value == NULL) || (exclusiveUpperBound == 0) || !NativeDeterministicRngBankV1_Validate(bank))
	{
		return 0;
	}
	candidate = *bank;
	do
	{
		if (!NativeDeterministicRngBankV1_NextU32(&candidate, streamTag, stableSlot, requesterSlot, &randomValue))
		{
			return 0;
		}
	} while (randomValue < threshold);
	*bank = candidate;
	*value = randomValue % exclusiveUpperBound;
	return 1;
}

size_t NativeDeterministicRngBankV1_EncodedSize(void)
{
	return NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES;
}

int NativeDeterministicRngBankV1_Encode(struct NativeCodecWriter *writer, const struct NativeDeterministicRngBankV1 *bank)
{
	static const uint8_t zeros[NATIVE_DETERMINISTIC_RNG_BANK_V1_HEADER_RESERVED_BYTES] = {0};
	struct NativeCodecWriter encoded;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES > writer->capacity - writer->offset) ||
	    !NativeDeterministicRngBankV1_Validate(bank))
	{
		return 0;
	}
	encoded = *writer;
	if (!NativeCodecWriter_WriteU32(&encoded, NATIVE_DETERMINISTIC_RNG_BANK_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteU32(&encoded, bank->bankVersion) ||
	    !NativeCodecWriter_WriteU32(&encoded, bank->derivationVersion) ||
	    !NativeCodecWriter_WriteU64(&encoded, bank->masterSeed) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_DETERMINISTIC_RNG_STREAM_COUNT) ||
	    !NativeCodecWriter_WriteBytes(&encoded, zeros, sizeof(zeros)))
	{
		return 0;
	}
	for (uint8_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		const struct NativeDeterministicRngStreamV1 *stream = &bank->streams[i];
		if (!NativeCodecWriter_WriteU32(&encoded, stream->tag) || !NativeCodecWriter_WriteU8(&encoded, stream->stableSlot) ||
		    !NativeCodecWriter_WriteU8(&encoded, stream->streamIndex) ||
		    !NativeCodecWriter_WriteBytes(&encoded, zeros, NATIVE_DETERMINISTIC_RNG_BANK_V1_STREAM_RESERVED_BYTES))
		{
			return 0;
		}
		for (unsigned int word = 0; word < 4; word++)
		{
			if (!NativeCodecWriter_WriteU64(&encoded, stream->state[word]))
			{
				return 0;
			}
		}
		if (!NativeCodecWriter_WriteU64(&encoded, stream->drawCount))
		{
			return 0;
		}
	}
	if (NativeCodecWriter_Size(&encoded) - writer->offset != NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES)
	{
		return 0;
	}
	*writer = encoded;
	return 1;
}

int NativeDeterministicRngBankV1_Decode(struct NativeCodecReader *reader, struct NativeDeterministicRngBankV1 *bank)
{
	struct NativeCodecReader encoded;
	struct NativeDeterministicRngBankV1 candidate;
	uint8_t reserved[NATIVE_DETERMINISTIC_RNG_BANK_V1_HEADER_RESERVED_BYTES];
	uint32_t magic;
	uint32_t encodedSize;
	uint32_t streamCount;

	if ((reader == NULL) || (bank == NULL) || !NativeCodecReader_Ok(reader) ||
	    (NativeCodecReader_Remaining(reader) != NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES))
	{
		return 0;
	}
	memset(&candidate, 0, sizeof(candidate));
	encoded = *reader;
	if (!NativeCodecReader_ReadU32(&encoded, &magic) || !NativeCodecReader_ReadU32(&encoded, &encodedSize) ||
	    !NativeCodecReader_ReadU32(&encoded, &candidate.bankVersion) ||
	    !NativeCodecReader_ReadU32(&encoded, &candidate.derivationVersion) ||
	    !NativeCodecReader_ReadU64(&encoded, &candidate.masterSeed) || !NativeCodecReader_ReadU32(&encoded, &streamCount) ||
	    !NativeCodecReader_ReadBytes(&encoded, reserved, sizeof(reserved)))
	{
		return 0;
	}
	for (size_t i = 0; i < sizeof(reserved); i++)
	{
		if (reserved[i] != 0)
		{
			return 0;
		}
	}
	for (uint8_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; i++)
	{
		struct NativeDeterministicRngStreamV1 *stream = &candidate.streams[i];
		if (!NativeCodecReader_ReadU32(&encoded, &stream->tag) || !NativeCodecReader_ReadU8(&encoded, &stream->stableSlot) ||
		    !NativeCodecReader_ReadU8(&encoded, &stream->streamIndex) ||
		    !NativeCodecReader_ReadBytes(&encoded, reserved, NATIVE_DETERMINISTIC_RNG_BANK_V1_STREAM_RESERVED_BYTES))
		{
			return 0;
		}
		for (size_t byte = 0; byte < NATIVE_DETERMINISTIC_RNG_BANK_V1_STREAM_RESERVED_BYTES; byte++)
		{
			if (reserved[byte] != 0)
			{
				return 0;
			}
		}
		for (unsigned int word = 0; word < 4; word++)
		{
			if (!NativeCodecReader_ReadU64(&encoded, &stream->state[word]))
			{
				return 0;
			}
		}
		if (!NativeCodecReader_ReadU64(&encoded, &stream->drawCount))
		{
			return 0;
		}
	}
	if ((magic != NATIVE_DETERMINISTIC_RNG_BANK_V1_MAGIC) ||
	    (encodedSize != NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES) ||
	    (streamCount != NATIVE_DETERMINISTIC_RNG_STREAM_COUNT) || (NativeCodecReader_Remaining(&encoded) != 0) ||
	    !NativeDeterministicRngBankV1_Validate(&candidate))
	{
		return 0;
	}
	*reader = encoded;
	*bank = candidate;
	return 1;
}

int NativeDeterministicRngBankV1_Digest(const struct NativeDeterministicRngBankV1 *bank,
	                                    uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t bytes[NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES];
	uint8_t candidate[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	struct NativeSha256 sha;

	if (digest == NULL)
	{
		return 0;
	}
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeDeterministicRngBankV1_Encode(&writer, bank) || (NativeCodecWriter_Size(&writer) != sizeof(bytes)))
	{
		return 0;
	}
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, bytes, sizeof(bytes));
	NativeSha256_Final(&sha, candidate);
	memcpy(digest, candidate, sizeof(candidate));
	return 1;
}
