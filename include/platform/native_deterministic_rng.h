#ifndef PLATFORM_NATIVE_DETERMINISTIC_RNG_H
#define PLATFORM_NATIVE_DETERMINISTIC_RNG_H

#include "platform/native_canonical_codec.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Dormant M3 deterministic simulation RNG bank. The bank is portable data:
 * it has no native pointers and does not read or replace any retail RNG.
 */
#define NATIVE_DETERMINISTIC_RNG_BANK_V1_MAGIC UINT32_C(0x3142524e)
#define NATIVE_DETERMINISTIC_RNG_BANK_V1_VERSION UINT32_C(1)
#define NATIVE_DETERMINISTIC_RNG_DERIVATION_VERSION UINT32_C(1)
#define NATIVE_DETERMINISTIC_RNG_ALGORITHM_NAME "xoshiro256**"
#define NATIVE_DETERMINISTIC_RNG_DERIVATION_NAME "SHA-256/CTRNRNG1"
#define NATIVE_DETERMINISTIC_RNG_DIGEST_ALGORITHM_NAME "SHA-256"

#define NATIVE_DETERMINISTIC_RNG_BOT_COUNT 8u
#define NATIVE_DETERMINISTIC_RNG_STREAM_COUNT (3u + NATIVE_DETERMINISTIC_RNG_BOT_COUNT)
#define NATIVE_DETERMINISTIC_RNG_GLOBAL_SLOT UINT8_C(0xff)
#define NATIVE_DETERMINISTIC_RNG_BANK_V1_ENCODED_BYTES 576u
#define NATIVE_DETERMINISTIC_RNG_BANK_V1_HEADER_RESERVED_BYTES 20u
#define NATIVE_DETERMINISTIC_RNG_BANK_V1_STREAM_RESERVED_BYTES 2u

/* Stable stream tags are part of the derivation and serialized contract. */
enum NativeDeterministicRngStreamTag
{
	NATIVE_DETERMINISTIC_RNG_STREAM_MATCH_SETUP = 0x4d415443,
	NATIVE_DETERMINISTIC_RNG_STREAM_ITEMS = 0x4954454d,
	NATIVE_DETERMINISTIC_RNG_STREAM_HAZARDS = 0x48415a44,
	NATIVE_DETERMINISTIC_RNG_STREAM_BOT = 0x424f5420
};

struct NativeDeterministicRngStreamV1
{
	uint32_t tag;
	uint8_t stableSlot;
	uint8_t streamIndex;
	uint64_t state[4];
	uint64_t drawCount;
};

struct NativeDeterministicRngBankV1
{
	uint32_t bankVersion;
	uint32_t derivationVersion;
	uint64_t masterSeed;
	struct NativeDeterministicRngStreamV1 streams[NATIVE_DETERMINISTIC_RNG_STREAM_COUNT];
};

/* Initialization is transactional and accepts only the frozen V1 derivation. */
int NativeDeterministicRngBankV1_Init(struct NativeDeterministicRngBankV1 *bank, uint64_t masterSeed,
	                                  uint32_t derivationVersion);
int NativeDeterministicRngBankV1_Validate(const struct NativeDeterministicRngBankV1 *bank);

/*
 * Global streams require GLOBAL_SLOT for both stableSlot and requesterSlot.
 * A BOT stream requires requesterSlot == stableSlot, rejecting cross-slot use.
 * Failed calls do not modify the bank or output.
 */
int NativeDeterministicRngBankV1_NextU64(struct NativeDeterministicRngBankV1 *bank, uint32_t streamTag,
	                                     uint8_t stableSlot, uint8_t requesterSlot, uint64_t *value);
int NativeDeterministicRngBankV1_NextU32(struct NativeDeterministicRngBankV1 *bank, uint32_t streamTag,
	                                     uint8_t stableSlot, uint8_t requesterSlot, uint32_t *value);
int NativeDeterministicRngBankV1_NextBoundedU32(struct NativeDeterministicRngBankV1 *bank, uint32_t streamTag,
	                                            uint8_t stableSlot, uint8_t requesterSlot,
	                                            uint32_t exclusiveUpperBound, uint32_t *value);

size_t NativeDeterministicRngBankV1_EncodedSize(void);
int NativeDeterministicRngBankV1_Encode(struct NativeCodecWriter *writer, const struct NativeDeterministicRngBankV1 *bank);
int NativeDeterministicRngBankV1_Decode(struct NativeCodecReader *reader, struct NativeDeterministicRngBankV1 *bank);
int NativeDeterministicRngBankV1_Digest(const struct NativeDeterministicRngBankV1 *bank,
	                                    uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

#endif
