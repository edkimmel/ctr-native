#ifndef PLATFORM_NATIVE_MATCH_CONFIG_H
#define PLATFORM_NATIVE_MATCH_CONFIG_H

#include "platform/native_canonical_codec.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>

#define NATIVE_MATCH_CONFIG_V1_MAGIC UINT32_C(0x31434d4e)
#define NATIVE_MATCH_CONFIG_V1_VERSION UINT32_C(1)
#define NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB UINT32_C(1)
#define NATIVE_MATCH_CONFIG_V1_RNG_DERIVATION_VERSION UINT32_C(1)
#define NATIVE_MATCH_CONFIG_V1_CANONICAL_SCHEMA_VERSION UINT32_C(5)
#define NATIVE_MATCH_CONFIG_V1_REPLAY_FORMAT_VERSION UINT32_C(4)
#define NATIVE_MATCH_CONFIG_V1_PROTOCOL_VERSION UINT32_C(1)
#define NATIVE_MATCH_CONFIG_V1_DIGEST_ALGORITHM_NAME "SHA-256"
#define NATIVE_MATCH_CONFIG_V1_SLOT_COUNT 8u
#define NATIVE_MATCH_CONFIG_V1_SLOT_RESERVED_BYTES 4u
#define NATIVE_MATCH_CONFIG_V1_RESERVED_BYTES 28u
#define NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES 256u

enum NativeMatchSlotRole
{
	NATIVE_MATCH_SLOT_ROLE_INACTIVE = 0,
	NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN = 1,
	NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN = 2,
	NATIVE_MATCH_SLOT_ROLE_BOT = 3
};

enum NativeMatchSlotLifecycle
{
	NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE = 0,
	NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE = 1,
	NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED = 2,
	NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED = 3
};

struct NativeMatchConfigSlotV1
{
	uint8_t role;
	uint8_t initialLifecycle;
	uint8_t characterID;
	uint8_t difficulty;
	uint8_t reserved[NATIVE_MATCH_CONFIG_V1_SLOT_RESERVED_BYTES];
};

/*
 * Portable match identity. No native pointer, local cabinet identity, join
 * order, UI, renderer, audio, or list-order state belongs in this value.
 */
struct NativeMatchConfigV1
{
	uint32_t configurationVersion;
	uint32_t profile;
	uint32_t trackID;
	uint32_t gameMode1;
	uint32_t gameMode2;
	uint32_t rules;
	uint32_t lapCount;
	uint32_t tickRateNumerator;
	uint32_t tickRateDenominator;
	uint64_t masterSeed;
	uint32_t rngDerivationVersion;
	uint32_t canonicalSchemaVersion;
	uint32_t replayFormatVersion;
	uint32_t protocolVersion;
	uint8_t buildIdentity[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t contentIdentity[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeMatchConfigSlotV1 slots[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT];
	uint8_t botRulesDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t reserved[NATIVE_MATCH_CONFIG_V1_RESERVED_BYTES];
};

/* Initializes the immutable two-cab role/lifecycle map and all V1 tags. */
void NativeMatchConfigV1_InitArcadeTwoCab(struct NativeMatchConfigV1 *config);
int NativeMatchConfigV1_Validate(const struct NativeMatchConfigV1 *config);
size_t NativeMatchConfigV1_EncodedSize(void);

/* Encode/decode are transactional. Decode accepts exactly one complete V1 record. */
int NativeMatchConfigV1_Encode(struct NativeCodecWriter *writer, const struct NativeMatchConfigV1 *config);
int NativeMatchConfigV1_Decode(struct NativeCodecReader *reader, struct NativeMatchConfigV1 *config);
int NativeMatchConfigV1_Digest(const struct NativeMatchConfigV1 *config, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

/* Resolves immutable ownership for input routing without changing the config. */
int NativeMatchConfigV1_FindRoleSlot(const struct NativeMatchConfigV1 *config, uint8_t role, uint8_t *slotIndex);

/* Roles never transition. This validates and applies only mutable lifecycle state. */
int NativeMatchSlotLifecycle_CanTransition(uint8_t role, uint8_t currentLifecycle, uint8_t nextLifecycle);
int NativeMatchSlotLifecycle_Transition(uint8_t role, uint8_t *lifecycle, uint8_t nextLifecycle);

#endif
