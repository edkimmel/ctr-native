#include "platform/native_canonical_projector.h"

#include <string.h>

static int MainCanonicalStateV4_IdentityMatchesConfig(const struct NativeIdentityV1 *identity,
	const struct NativeMatchConfigV1 *config)
{
	return (memcmp(identity->build, config->buildIdentity, NATIVE_IDENTITY_DIGEST_BYTES) == 0) &&
	       (memcmp(identity->content, config->contentIdentity, NATIVE_IDENTITY_DIGEST_BYTES) == 0);
}

static int MainCanonicalStateV4_ContextValid(const struct MainCanonicalStateV4Context *context)
{
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	return (context != NULL) && NativeMatchConfigV1_Validate(&context->config) &&
	       (context->config.canonicalSchemaVersion == NATIVE_CANONICAL_STATE_V4_SCHEMA_VERSION) &&
	       (context->config.replayFormatVersion == NATIVE_CANONICAL_REPLAY_V4_FORMAT_VERSION) &&
	       NativeMatchConfigV1_Digest(&context->config, digest) &&
	       (memcmp(digest, context->configDigest, sizeof(digest)) == 0);
}

int MainCanonicalStateV4Context_Init(struct MainCanonicalStateV4Context *context,
	const struct NativeMatchConfigV1 *config)
{
	struct MainCanonicalStateV4Context candidate;

	if ((context == NULL) || (config == NULL) || !NativeMatchConfigV1_Validate(config) ||
	    (config->canonicalSchemaVersion != NATIVE_CANONICAL_STATE_V4_SCHEMA_VERSION) ||
	    (config->replayFormatVersion != NATIVE_CANONICAL_REPLAY_V4_FORMAT_VERSION))
	{
		return 0;
	}

	candidate.config = *config;
	if (!NativeMatchConfigV1_Digest(&candidate.config, candidate.configDigest)) return 0;
	*context = candidate;
	return 1;
}

int MainCanonicalState_ProjectV4(struct NativeCanonicalStateV4 *state,
	const struct MainCanonicalStateV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeDeterministicRngBankV1 *deterministicRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversV1 *drivers,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology)
{
	struct NativeCanonicalStateV4 candidate;

	if ((state == NULL) || !MainCanonicalStateV4_ContextValid(context) || (identity == NULL) ||
	    (control == NULL) || (retailRng == NULL) || (deterministicRng == NULL) || (input == NULL) ||
	    (drivers == NULL) || (worldCounters == NULL) || (mineRegistry == NULL) || (topology == NULL) ||
	    !MainCanonicalStateV4_IdentityMatchesConfig(identity, &context->config) ||
	    (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT) ||
	    !NativeDeterministicRngBankV1_Validate(deterministicRng) ||
	    (deterministicRng->masterSeed != context->config.masterSeed) ||
	    (deterministicRng->derivationVersion != context->config.rngDerivationVersion) ||
	    !NativeCanonicalDriversV1_Validate(drivers) ||
	    !NativeCanonicalWorldCountersV1_Validate(worldCounters) ||
	    !NativeCanonicalWorldMineRegistryV1_Validate(mineRegistry) ||
	    !NativeCanonicalTopologyV1_Validate(topology))
	{
		return 0;
	}

	NativeCanonicalStateV4_Init(&candidate);
	candidate.frameNumber = replayFrameNumber;
	candidate.identity = *identity;
	memcpy(candidate.configDigest, context->configDigest, sizeof(candidate.configDigest));
	candidate.control = *control;
	candidate.retailRng = *retailRng;
	candidate.deterministicRng = *deterministicRng;
	candidate.input = *input;
	candidate.drivers = *drivers;
	candidate.worldCounters = *worldCounters;
	candidate.mineRegistry = *mineRegistry;
	candidate.topology = *topology;
	if (!NativeCanonicalStateV4_ComputeDigests(&candidate)) return 0;
	*state = candidate;
	return 1;
}

int MainCanonicalState_ProjectV4InPlaceWithScratch(struct NativeCanonicalStateV4 *state,
	const struct MainCanonicalStateV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeDeterministicRngBankV1 *deterministicRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversV1 *drivers,
	const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry,
	const struct NativeCanonicalTopologyV1 *topology,
	uint8_t *scratch, size_t scratchSize)
{
	if ((state == NULL) || !MainCanonicalStateV4_ContextValid(context) || (identity == NULL) ||
	    (control == NULL) || (retailRng == NULL) || (deterministicRng == NULL) || (input == NULL) ||
	    (drivers == NULL) || (worldCounters == NULL) || (mineRegistry == NULL) || (topology == NULL) ||
	    !MainCanonicalStateV4_IdentityMatchesConfig(identity, &context->config) ||
	    (input->padCount != NATIVE_CANONICAL_INPUT_PAD_COUNT) ||
	    !NativeDeterministicRngBankV1_Validate(deterministicRng) ||
	    (deterministicRng->masterSeed != context->config.masterSeed) ||
	    (deterministicRng->derivationVersion != context->config.rngDerivationVersion) ||
	    !NativeCanonicalDriversV1_Validate(drivers) ||
	    !NativeCanonicalWorldCountersV1_Validate(worldCounters) ||
	    !NativeCanonicalWorldMineRegistryV1_Validate(mineRegistry) ||
	    !NativeCanonicalTopologyV1_Validate(topology))
	{
		return 0;
	}

	memset(state, 0, sizeof(*state));
	state->schemaVersion = NATIVE_CANONICAL_STATE_V4_SCHEMA_VERSION;
	state->replayFormatVersion = NATIVE_CANONICAL_REPLAY_V4_FORMAT_VERSION;
	state->domainCount = NATIVE_CANONICAL_DOMAIN_COUNT;
	state->frameNumber = replayFrameNumber;
	state->identity = *identity;
	memcpy(state->configDigest, context->configDigest, sizeof(state->configDigest));
	state->control = *control;
	state->retailRng = *retailRng;
	state->deterministicRng = *deterministicRng;
	state->input = *input;
	state->drivers = *drivers;
	state->worldCounters = *worldCounters;
	state->mineRegistry = *mineRegistry;
	state->topology = *topology;
	return NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch(state, scratch, scratchSize);
}
