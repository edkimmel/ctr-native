#include "platform/native_canonical_state.h"

#include <string.h>

#define NATIVE_CANONICAL_CONTROL_BYTES 48u
#define NATIVE_CANONICAL_RNG_BYTES     20u
#define NATIVE_CANONICAL_INPUT_BYTES   40u
#define NATIVE_CANONICAL_HEADER_BYTES  84u
#define NATIVE_CANONICAL_DOMAIN_BYTES  16u
#define NATIVE_CANONICAL_COMBINED_BYTES 8u
#define NATIVE_CANONICAL_STATE_BYTES                                                                                                      \
	(NATIVE_CANONICAL_HEADER_BYTES + (NATIVE_CANONICAL_CONTROL_BYTES + NATIVE_CANONICAL_DOMAIN_BYTES) +                                \
	 (NATIVE_CANONICAL_RNG_BYTES + NATIVE_CANONICAL_DOMAIN_BYTES) +                                                                    \
	 (NATIVE_CANONICAL_INPUT_BYTES + NATIVE_CANONICAL_DOMAIN_BYTES) +                                                                  \
	 (3u * NATIVE_CANONICAL_DOMAIN_BYTES) + NATIVE_CANONICAL_COMBINED_BYTES)

static size_t NativeCanonicalStateV1_DomainPayloadSize(uint32_t domainID)
{
	switch (domainID)
	{
		case NATIVE_CANONICAL_DOMAIN_CONTROL:
			return NATIVE_CANONICAL_CONTROL_BYTES;
		case NATIVE_CANONICAL_DOMAIN_RNG:
			return NATIVE_CANONICAL_RNG_BYTES;
		case NATIVE_CANONICAL_DOMAIN_INPUT:
			return NATIVE_CANONICAL_INPUT_BYTES;
		case NATIVE_CANONICAL_DOMAIN_DRIVERS:
		case NATIVE_CANONICAL_DOMAIN_WORLD:
		case NATIVE_CANONICAL_DOMAIN_TOPOLOGY:
			return 0;
		default:
			return SIZE_MAX;
	}
}

static int NativeCanonicalStateV1_EncodeControl(struct NativeCodecWriter *writer, const struct NativeCanonicalControlV1 *control)
{
	return NativeCodecWriter_WriteS32(writer, control->frameTimer) && NativeCodecWriter_WriteS32(writer, control->frameCounter) &&
	       NativeCodecWriter_WriteS32(writer, control->timer) && NativeCodecWriter_WriteS32(writer, control->framesInThisLEV) &&
	       NativeCodecWriter_WriteS32(writer, control->elapsedTimeMS) && NativeCodecWriter_WriteS32(writer, control->msInThisLEV) &&
	       NativeCodecWriter_WriteS32(writer, control->elapsedEventTime) && NativeCodecWriter_WriteS32(writer, control->mainGameState) &&
	       NativeCodecWriter_WriteS32(writer, control->loadingStage) && NativeCodecWriter_WriteS32(writer, control->levelID) &&
	       NativeCodecWriter_WriteS32(writer, control->gameMode1) && NativeCodecWriter_WriteS32(writer, control->gameMode2);
}

static int NativeCanonicalStateV1_DecodeControl(struct NativeCodecReader *reader, struct NativeCanonicalControlV1 *control)
{
	return NativeCodecReader_ReadS32(reader, &control->frameTimer) && NativeCodecReader_ReadS32(reader, &control->frameCounter) &&
	       NativeCodecReader_ReadS32(reader, &control->timer) && NativeCodecReader_ReadS32(reader, &control->framesInThisLEV) &&
	       NativeCodecReader_ReadS32(reader, &control->elapsedTimeMS) && NativeCodecReader_ReadS32(reader, &control->msInThisLEV) &&
	       NativeCodecReader_ReadS32(reader, &control->elapsedEventTime) && NativeCodecReader_ReadS32(reader, &control->mainGameState) &&
	       NativeCodecReader_ReadS32(reader, &control->loadingStage) && NativeCodecReader_ReadS32(reader, &control->levelID) &&
	       NativeCodecReader_ReadS32(reader, &control->gameMode1) && NativeCodecReader_ReadS32(reader, &control->gameMode2);
}

static int NativeCanonicalStateV1_EncodeRng(struct NativeCodecWriter *writer, const struct NativeCanonicalRngV1 *rng)
{
	return NativeCodecWriter_WriteU32(writer, rng->mixRandomNumber) && NativeCodecWriter_WriteU32(writer, rng->deadcoed0) &&
	       NativeCodecWriter_WriteU32(writer, rng->deadcoed1) && NativeCodecWriter_WriteU32(writer, rng->advRng0) &&
	       NativeCodecWriter_WriteU32(writer, rng->advRng1);
}

static int NativeCanonicalStateV1_DecodeRng(struct NativeCodecReader *reader, struct NativeCanonicalRngV1 *rng)
{
	return NativeCodecReader_ReadU32(reader, &rng->mixRandomNumber) && NativeCodecReader_ReadU32(reader, &rng->deadcoed0) &&
	       NativeCodecReader_ReadU32(reader, &rng->deadcoed1) && NativeCodecReader_ReadU32(reader, &rng->advRng0) &&
	       NativeCodecReader_ReadU32(reader, &rng->advRng1);
}

static int NativeCanonicalStateV1_EncodeInput(struct NativeCodecWriter *writer, const struct NativeCanonicalInputV1 *input)
{
	if (!NativeCodecWriter_WriteU32(writer, input->padCount))
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_CANONICAL_INPUT_PAD_COUNT; i++)
	{
		const struct NativeCanonicalInputPadV1 *pad = &input->pads[i];

		if (!NativeCodecWriter_WriteU8(writer, pad->status) || !NativeCodecWriter_WriteU8(writer, pad->id) ||
		    !NativeCodecWriter_WriteBytes(writer, pad->buttons, sizeof(pad->buttons)) ||
		    !NativeCodecWriter_WriteBytes(writer, pad->analog, sizeof(pad->analog)) || !NativeCodecWriter_WriteU8(writer, pad->connected))
		{
			return 0;
		}
	}

	return 1;
}

static int NativeCanonicalStateV1_DecodeInput(struct NativeCodecReader *reader, struct NativeCanonicalInputV1 *input)
{
	if (!NativeCodecReader_ReadU32(reader, &input->padCount) || (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT))
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_CANONICAL_INPUT_PAD_COUNT; i++)
	{
		struct NativeCanonicalInputPadV1 *pad = &input->pads[i];

		if (!NativeCodecReader_ReadU8(reader, &pad->status) || !NativeCodecReader_ReadU8(reader, &pad->id) ||
		    !NativeCodecReader_ReadBytes(reader, pad->buttons, sizeof(pad->buttons)) ||
		    !NativeCodecReader_ReadBytes(reader, pad->analog, sizeof(pad->analog)) || !NativeCodecReader_ReadU8(reader, &pad->connected))
		{
			return 0;
		}
	}

	return 1;
}

static int NativeCanonicalStateV1_BuildPayload(const struct NativeCanonicalStateV1 *state, uint32_t domainID, uint8_t *bytes, size_t size)
{
	struct NativeCodecWriter writer;

	if ((state == NULL) || (size != NativeCanonicalStateV1_DomainPayloadSize(domainID)))
	{
		return 0;
	}

	NativeCodecWriter_Init(&writer, bytes, size, NULL);
	switch (domainID)
	{
		case NATIVE_CANONICAL_DOMAIN_CONTROL:
			if (!NativeCanonicalStateV1_EncodeControl(&writer, &state->control))
			{
				return 0;
			}
			break;
		case NATIVE_CANONICAL_DOMAIN_RNG:
			if (!NativeCanonicalStateV1_EncodeRng(&writer, &state->rng))
			{
				return 0;
			}
			break;
		case NATIVE_CANONICAL_DOMAIN_INPUT:
			if (!NativeCanonicalStateV1_EncodeInput(&writer, &state->input))
			{
				return 0;
			}
			break;
		case NATIVE_CANONICAL_DOMAIN_DRIVERS:
		case NATIVE_CANONICAL_DOMAIN_WORLD:
		case NATIVE_CANONICAL_DOMAIN_TOPOLOGY:
			break;
		default:
			return 0;
	}

	return NativeCodecWriter_Ok(&writer) && (NativeCodecWriter_Size(&writer) == size);
}

static uint64_t NativeCanonicalStateV1_DigestPayload(const uint8_t *bytes, size_t size)
{
	struct NativeCodecDigest64 digest;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, size);
	return digest.value;
}

static uint64_t NativeCanonicalStateV1_CombinedDigest(const uint64_t *domainDigests)
{
	uint8_t bytes[NATIVE_CANONICAL_DOMAIN_COUNT * 12u];
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		(void)NativeCodecWriter_WriteU32(&writer, NativeCanonicalDomainOrder[i]);
		(void)NativeCodecWriter_WriteU64(&writer, domainDigests[i]);
	}

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, sizeof(bytes));
	return digest.value;
}

static int NativeCanonicalStateV1_DigestsMatch(const struct NativeCanonicalStateV1 *state)
{
	struct NativeCanonicalStateV1 expected;

	if (state == NULL)
	{
		return 0;
	}

	expected = *state;
	if (!NativeCanonicalStateV1_ComputeDigests(&expected))
	{
		return 0;
	}

	return (memcmp(state->domainDigests, expected.domainDigests, sizeof(state->domainDigests)) == 0) &&
	       (state->combinedDigest == expected.combinedDigest);
}

static int NativeCanonicalIdentityV1_Equals(const struct NativeIdentityV1 *left, const struct NativeIdentityV1 *right)
{
	return (memcmp(left->build, right->build, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(left->content, right->content, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
}

void NativeCanonicalStateV1_Init(struct NativeCanonicalStateV1 *state)
{
	if (state == NULL)
	{
		return;
	}

	memset(state, 0, sizeof(*state));
	state->schemaVersion = NATIVE_CANONICAL_STATE_SCHEMA_VERSION_V2;
	state->replayFormatVersion = NATIVE_CANONICAL_REPLAY_FORMAT_VERSION_V2;
	state->domainCount = NATIVE_CANONICAL_DOMAIN_COUNT;
	state->input.padCount = NATIVE_CANONICAL_INPUT_PAD_COUNT;
	(void)NativeCanonicalStateV1_ComputeDigests(state);
}

int NativeCanonicalStateV1_Validate(const struct NativeCanonicalStateV1 *state)
{
	return (state != NULL) && (state->schemaVersion == NATIVE_CANONICAL_STATE_SCHEMA_VERSION_V2) &&
	       (state->replayFormatVersion == NATIVE_CANONICAL_REPLAY_FORMAT_VERSION_V2) && (state->domainCount == NATIVE_CANONICAL_DOMAIN_COUNT) &&
	       (state->input.padCount == NATIVE_CANONICAL_INPUT_PAD_COUNT);
}

int NativeCanonicalStateV1_ComputeDigests(struct NativeCanonicalStateV1 *state)
{
	struct NativeCanonicalStateV1 computed;
	uint8_t bytes[NATIVE_CANONICAL_CONTROL_BYTES];

	if (!NativeCanonicalStateV1_Validate(state))
	{
		return 0;
	}

	computed = *state;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		const size_t size = NativeCanonicalStateV1_DomainPayloadSize(NativeCanonicalDomainOrder[i]);

		if ((size == SIZE_MAX) || !NativeCanonicalStateV1_BuildPayload(state, NativeCanonicalDomainOrder[i], bytes, size))
		{
			return 0;
		}
		computed.domainDigests[i] = NativeCanonicalStateV1_DigestPayload(bytes, size);
	}
	computed.combinedDigest = NativeCanonicalStateV1_CombinedDigest(computed.domainDigests);
	*state = computed;
	return 1;
}

size_t NativeCanonicalStateV1_EncodedSize(void)
{
	return NATIVE_CANONICAL_STATE_BYTES;
}

int NativeCanonicalStateV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalStateV1 *state)
{
	struct NativeCodecWriter encoded;
	uint8_t bytes[NATIVE_CANONICAL_CONTROL_BYTES];

	if ((writer == NULL) || !NativeCanonicalStateV1_Validate(state) || !NativeCanonicalStateV1_DigestsMatch(state) || (writer->failed != 0) ||
	    (writer->offset > writer->capacity) || ((writer->data == NULL) && (writer->capacity != 0)) ||
	    (NativeCanonicalStateV1_EncodedSize() > writer->capacity - writer->offset))
	{
		return 0;
	}

	encoded = *writer;
	if (!NativeCodecWriter_WriteU32(&encoded, NATIVE_CANONICAL_STATE_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU32(&encoded, state->schemaVersion) || !NativeCodecWriter_WriteU32(&encoded, state->replayFormatVersion) ||
	    !NativeCodecWriter_WriteU32(&encoded, state->domainCount) || !NativeCodecWriter_WriteU32(&encoded, state->frameNumber) ||
	    !NativeCodecWriter_WriteBytes(&encoded, state->identity.build, sizeof(state->identity.build)) ||
	    !NativeCodecWriter_WriteBytes(&encoded, state->identity.content, sizeof(state->identity.content)))
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		const uint32_t domainID = NativeCanonicalDomainOrder[i];
		const size_t size = NativeCanonicalStateV1_DomainPayloadSize(domainID);

		if (!NativeCanonicalStateV1_BuildPayload(state, domainID, bytes, size) || !NativeCodecWriter_WriteU32(&encoded, domainID) ||
		    !NativeCodecWriter_WriteU32(&encoded, (uint32_t)size) || !NativeCodecWriter_WriteBytes(&encoded, bytes, size) ||
		    !NativeCodecWriter_WriteU64(&encoded, state->domainDigests[i]))
		{
			return 0;
		}
	}

	if (!NativeCodecWriter_WriteU64(&encoded, state->combinedDigest))
	{
		return 0;
	}

	*writer = encoded;
	return 1;
}

int NativeCanonicalStateV1_Decode(struct NativeCodecReader *reader, const struct NativeIdentityV1 *expectedIdentity,
                                  struct NativeCanonicalStateV1 *state)
{
	struct NativeCanonicalStateV1 decoded;
	struct NativeCanonicalStateV1 calculated;
	struct NativeCodecReader encoded;
	uint32_t magic;
	uint8_t bytes[NATIVE_CANONICAL_CONTROL_BYTES];

	if ((reader == NULL) || (expectedIdentity == NULL) || (state == NULL))
	{
		return 0;
	}

	encoded = *reader;
	NativeCanonicalStateV1_Init(&decoded);
	if (!NativeCodecReader_ReadU32(&encoded, &magic) || (magic != NATIVE_CANONICAL_STATE_V1_MAGIC) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.schemaVersion) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.replayFormatVersion) || !NativeCodecReader_ReadU32(&encoded, &decoded.domainCount) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.frameNumber) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.identity.build, sizeof(decoded.identity.build)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.identity.content, sizeof(decoded.identity.content)) ||
	    !NativeCanonicalIdentityV1_Equals(&decoded.identity, expectedIdentity) || !NativeCanonicalStateV1_Validate(&decoded))
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		const uint32_t expectedDomainID = NativeCanonicalDomainOrder[i];
		const size_t expectedSize = NativeCanonicalStateV1_DomainPayloadSize(expectedDomainID);
		uint32_t domainID;
		uint32_t payloadSize;

		if (!NativeCodecReader_ReadU32(&encoded, &domainID) || !NativeCodecReader_ReadU32(&encoded, &payloadSize) ||
		    (domainID != expectedDomainID) || (payloadSize != expectedSize))
		{
			return 0;
		}

		switch (domainID)
		{
			case NATIVE_CANONICAL_DOMAIN_CONTROL:
				if (!NativeCanonicalStateV1_DecodeControl(&encoded, &decoded.control))
				{
					return 0;
				}
				break;
			case NATIVE_CANONICAL_DOMAIN_RNG:
				if (!NativeCanonicalStateV1_DecodeRng(&encoded, &decoded.rng))
				{
					return 0;
				}
				break;
			case NATIVE_CANONICAL_DOMAIN_INPUT:
				if (!NativeCanonicalStateV1_DecodeInput(&encoded, &decoded.input))
				{
					return 0;
				}
				break;
			default:
				break;
		}

		if (!NativeCanonicalStateV1_BuildPayload(&decoded, domainID, bytes, expectedSize) ||
		    !NativeCodecReader_ReadU64(&encoded, &decoded.domainDigests[i]) ||
		    (decoded.domainDigests[i] != NativeCanonicalStateV1_DigestPayload(bytes, expectedSize)))
		{
			return 0;
		}
	}

	if (!NativeCodecReader_ReadU64(&encoded, &decoded.combinedDigest))
	{
		return 0;
	}

	calculated = decoded;
	if (!NativeCanonicalStateV1_ComputeDigests(&calculated) ||
	    (memcmp(decoded.domainDigests, calculated.domainDigests, sizeof(decoded.domainDigests)) != 0) ||
	    (decoded.combinedDigest != calculated.combinedDigest))
	{
		return 0;
	}

	*reader = encoded;
	*state = decoded;
	return 1;
}
