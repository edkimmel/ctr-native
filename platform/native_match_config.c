#include "platform/native_match_config.h"

#include <string.h>

static int NativeMatchConfig_IsAllZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;

	for (size_t i = 0; i < size; i++)
	{
		combined |= bytes[i];
	}
	return combined == 0;
}

static int NativeMatchConfig_SlotValid(const struct NativeMatchConfigSlotV1 *slot, uint8_t expectedRole, uint8_t expectedLifecycle)
{
	if ((slot->role != expectedRole) || (slot->initialLifecycle != expectedLifecycle) ||
	    !NativeMatchConfig_IsAllZero(slot->reserved, sizeof(slot->reserved)))
	{
		return 0;
	}
	if ((expectedRole == NATIVE_MATCH_SLOT_ROLE_INACTIVE) && ((slot->characterID != 0) || (slot->difficulty != 0)))
	{
		return 0;
	}
	return 1;
}

void NativeMatchConfigV1_InitArcadeTwoCab(struct NativeMatchConfigV1 *config)
{
	if (config == NULL)
	{
		return;
	}

	memset(config, 0, sizeof(*config));
	config->configurationVersion = NATIVE_MATCH_CONFIG_V1_VERSION;
	config->profile = NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB;
	config->rngDerivationVersion = NATIVE_MATCH_CONFIG_V1_RNG_DERIVATION_VERSION;
	config->canonicalSchemaVersion = NATIVE_MATCH_CONFIG_V1_CANONICAL_SCHEMA_VERSION;
	config->replayFormatVersion = NATIVE_MATCH_CONFIG_V1_REPLAY_FORMAT_VERSION;
	config->protocolVersion = NATIVE_MATCH_CONFIG_V1_PROTOCOL_VERSION;
	config->slots[0].role = NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
	config->slots[1].role = NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	for (uint32_t i = 2; i <= 5; i++)
	{
		config->slots[i].role = NATIVE_MATCH_SLOT_ROLE_BOT;
	}
	for (uint32_t i = 0; i <= 5; i++)
	{
		config->slots[i].initialLifecycle = NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE;
	}
}

int NativeMatchConfigV1_Validate(const struct NativeMatchConfigV1 *config)
{
	static const uint8_t roles[NATIVE_MATCH_CONFIG_V1_SLOT_COUNT] = {
		NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		NATIVE_MATCH_SLOT_ROLE_BOT,
		NATIVE_MATCH_SLOT_ROLE_BOT,
		NATIVE_MATCH_SLOT_ROLE_BOT,
		NATIVE_MATCH_SLOT_ROLE_BOT,
		NATIVE_MATCH_SLOT_ROLE_INACTIVE,
		NATIVE_MATCH_SLOT_ROLE_INACTIVE,
	};

	if ((config == NULL) || (config->configurationVersion != NATIVE_MATCH_CONFIG_V1_VERSION) ||
	    (config->profile != NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_TWO_CAB) || (config->lapCount == 0) ||
	    (config->tickRateNumerator == 0) || (config->tickRateDenominator == 0) ||
	    (config->rngDerivationVersion != NATIVE_MATCH_CONFIG_V1_RNG_DERIVATION_VERSION) ||
	    (config->canonicalSchemaVersion != NATIVE_MATCH_CONFIG_V1_CANONICAL_SCHEMA_VERSION) ||
	    (config->replayFormatVersion != NATIVE_MATCH_CONFIG_V1_REPLAY_FORMAT_VERSION) ||
	    (config->protocolVersion != NATIVE_MATCH_CONFIG_V1_PROTOCOL_VERSION) ||
	    NativeMatchConfig_IsAllZero(config->buildIdentity, sizeof(config->buildIdentity)) ||
	    NativeMatchConfig_IsAllZero(config->contentIdentity, sizeof(config->contentIdentity)) ||
	    NativeMatchConfig_IsAllZero(config->botRulesDigest, sizeof(config->botRulesDigest)) ||
	    !NativeMatchConfig_IsAllZero(config->reserved, sizeof(config->reserved)))
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		const uint8_t lifecycle = roles[i] == NATIVE_MATCH_SLOT_ROLE_INACTIVE ? NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE
		                                                                            : NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE;
		if (!NativeMatchConfig_SlotValid(&config->slots[i], roles[i], lifecycle))
		{
			return 0;
		}
	}
	return 1;
}

size_t NativeMatchConfigV1_EncodedSize(void)
{
	return NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES;
}

int NativeMatchConfigV1_Encode(struct NativeCodecWriter *writer, const struct NativeMatchConfigV1 *config)
{
	struct NativeCodecWriter encoded;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES > writer->capacity - writer->offset) || !NativeMatchConfigV1_Validate(config))
	{
		return 0;
	}

	encoded = *writer;
	if (!NativeCodecWriter_WriteU32(&encoded, NATIVE_MATCH_CONFIG_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU32(&encoded, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->configurationVersion) || !NativeCodecWriter_WriteU32(&encoded, config->profile) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->trackID) || !NativeCodecWriter_WriteU32(&encoded, config->gameMode1) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->gameMode2) || !NativeCodecWriter_WriteU32(&encoded, config->rules) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->lapCount) || !NativeCodecWriter_WriteU32(&encoded, config->tickRateNumerator) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->tickRateDenominator) || !NativeCodecWriter_WriteU64(&encoded, config->masterSeed) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->rngDerivationVersion) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->canonicalSchemaVersion) ||
	    !NativeCodecWriter_WriteU32(&encoded, config->replayFormatVersion) || !NativeCodecWriter_WriteU32(&encoded, config->protocolVersion) ||
	    !NativeCodecWriter_WriteBytes(&encoded, config->buildIdentity, sizeof(config->buildIdentity)) ||
	    !NativeCodecWriter_WriteBytes(&encoded, config->contentIdentity, sizeof(config->contentIdentity)))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		const struct NativeMatchConfigSlotV1 *slot = &config->slots[i];
		if (!NativeCodecWriter_WriteU8(&encoded, slot->role) || !NativeCodecWriter_WriteU8(&encoded, slot->initialLifecycle) ||
		    !NativeCodecWriter_WriteU8(&encoded, slot->characterID) || !NativeCodecWriter_WriteU8(&encoded, slot->difficulty) ||
		    !NativeCodecWriter_WriteBytes(&encoded, slot->reserved, sizeof(slot->reserved)))
		{
			return 0;
		}
	}
	if (!NativeCodecWriter_WriteBytes(&encoded, config->botRulesDigest, sizeof(config->botRulesDigest)) ||
	    !NativeCodecWriter_WriteBytes(&encoded, config->reserved, sizeof(config->reserved)))
	{
		return 0;
	}
	*writer = encoded;
	return 1;
}

int NativeMatchConfigV1_Decode(struct NativeCodecReader *reader, struct NativeMatchConfigV1 *config)
{
	struct NativeCodecReader encoded;
	struct NativeMatchConfigV1 decoded;
	uint32_t magic;
	uint32_t encodedSize;

	if ((reader == NULL) || (config == NULL) || !NativeCodecReader_Ok(reader) ||
	    (NativeCodecReader_Remaining(reader) != NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES))
	{
		return 0;
	}

	memset(&decoded, 0, sizeof(decoded));
	encoded = *reader;
	if (!NativeCodecReader_ReadU32(&encoded, &magic) || !NativeCodecReader_ReadU32(&encoded, &encodedSize) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.configurationVersion) || !NativeCodecReader_ReadU32(&encoded, &decoded.profile) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.trackID) || !NativeCodecReader_ReadU32(&encoded, &decoded.gameMode1) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.gameMode2) || !NativeCodecReader_ReadU32(&encoded, &decoded.rules) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.lapCount) || !NativeCodecReader_ReadU32(&encoded, &decoded.tickRateNumerator) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.tickRateDenominator) || !NativeCodecReader_ReadU64(&encoded, &decoded.masterSeed) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.rngDerivationVersion) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.canonicalSchemaVersion) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.replayFormatVersion) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.protocolVersion) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.buildIdentity, sizeof(decoded.buildIdentity)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.contentIdentity, sizeof(decoded.contentIdentity)))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		struct NativeMatchConfigSlotV1 *slot = &decoded.slots[i];
		if (!NativeCodecReader_ReadU8(&encoded, &slot->role) || !NativeCodecReader_ReadU8(&encoded, &slot->initialLifecycle) ||
		    !NativeCodecReader_ReadU8(&encoded, &slot->characterID) || !NativeCodecReader_ReadU8(&encoded, &slot->difficulty) ||
		    !NativeCodecReader_ReadBytes(&encoded, slot->reserved, sizeof(slot->reserved)))
		{
			return 0;
		}
	}
	if (!NativeCodecReader_ReadBytes(&encoded, decoded.botRulesDigest, sizeof(decoded.botRulesDigest)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.reserved, sizeof(decoded.reserved)) ||
	    (magic != NATIVE_MATCH_CONFIG_V1_MAGIC) || (encodedSize != NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES) ||
	    (NativeCodecReader_Remaining(&encoded) != 0) || !NativeMatchConfigV1_Validate(&decoded))
	{
		return 0;
	}
	*reader = encoded;
	*config = decoded;
	return 1;
}

int NativeMatchConfigV1_Digest(const struct NativeMatchConfigV1 *config, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t candidate[NATIVE_SHA256_DIGEST_BYTES];
	struct NativeCodecWriter writer;
	struct NativeSha256 sha;

	if (digest == NULL)
	{
		return 0;
	}
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeMatchConfigV1_Encode(&writer, config) || (NativeCodecWriter_Size(&writer) != sizeof(bytes)))
	{
		return 0;
	}
	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, bytes, sizeof(bytes));
	NativeSha256_Final(&sha, candidate);
	memcpy(digest, candidate, sizeof(candidate));
	return 1;
}

int NativeMatchConfigV1_FindRoleSlot(const struct NativeMatchConfigV1 *config, uint8_t role, uint8_t *slotIndex)
{
	uint8_t found = UINT8_MAX;

	if ((slotIndex == NULL) || !NativeMatchConfigV1_Validate(config) ||
	    ((role != NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (role != NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN)))
	{
		return 0;
	}
	for (uint8_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		if (config->slots[i].role == role)
		{
			if (found != UINT8_MAX)
			{
				return 0;
			}
			found = i;
		}
	}
	if (found == UINT8_MAX)
	{
		return 0;
	}
	*slotIndex = found;
	return 1;
}

int NativeMatchSlotLifecycle_CanTransition(uint8_t role, uint8_t currentLifecycle, uint8_t nextLifecycle)
{
	if (role == NATIVE_MATCH_SLOT_ROLE_INACTIVE)
	{
		return (currentLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE) &&
		       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_INACTIVE);
	}
	if ((role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) || (role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
	{
		if (currentLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE)
		{
			return (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE) ||
			       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED) ||
			       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED);
		}
		if (currentLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED)
		{
			return (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_DISCONNECTED) ||
			       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED);
		}
		return (currentLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED) &&
		       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED);
	}
	if (role == NATIVE_MATCH_SLOT_ROLE_BOT)
	{
		if (currentLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE)
		{
			return (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_ACTIVE) ||
			       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED);
		}
		return (currentLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED) &&
		       (nextLifecycle == NATIVE_MATCH_SLOT_LIFECYCLE_FINISHED);
	}
	return 0;
}

int NativeMatchSlotLifecycle_Transition(uint8_t role, uint8_t *lifecycle, uint8_t nextLifecycle)
{
	if ((lifecycle == NULL) || !NativeMatchSlotLifecycle_CanTransition(role, *lifecycle, nextLifecycle))
	{
		return 0;
	}
	*lifecycle = nextLifecycle;
	return 1;
}
