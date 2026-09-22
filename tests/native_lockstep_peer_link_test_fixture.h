#ifndef TESTS_NATIVE_LOCKSTEP_PEER_LINK_TEST_FIXTURE_H
#define TESTS_NATIVE_LOCKSTEP_PEER_LINK_TEST_FIXTURE_H

#include "platform/native_canonical_state_v4.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

/*
 * Tiny shared, header-only, test-only helper included by both
 * tests/native_lockstep_peer_link_process_test.c (the parent process) and
 * tests/native_lockstep_peer_link_helper.c (the child process), so both
 * build a byte-identical NativeMatchConfigV1 proposal and byte-identical
 * synthetic per-frame canonical states without duplicating logic that could
 * silently drift apart between the two files. The real CTR simulation is
 * explicitly out of scope for this milestone; this synthetic canonical
 * state is deliberate and documented, not a shortcut to hide.
 */

/*
 * Calls NativeMatchConfigV1_InitArcadeTwoCab, then explicitly sets every
 * field NativeMatchConfigV1_Validate requires to be non-default (see
 * platform/native_match_config.c NativeMatchConfigV1_Validate): a nonzero
 * lapCount, a nonzero tickRateNumerator/tickRateDenominator, a fixed nonzero
 * masterSeed, and fixed non-all-zero 32-byte buildIdentity/contentIdentity/
 * botRulesDigest patterns (Validate rejects an all-zero identity field).
 * trackID/gameMode1/gameMode2/rules and the two human slots' characterID/
 * difficulty are unconstrained by Validate but are still set to fixed
 * values so the built config is fully deterministic. The result always
 * passes NativeMatchConfigV1_Validate.
 */
static inline void NativeLockstepPeerLinkFixture_BuildConfig(struct NativeMatchConfigV1 *config)
{
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = UINT32_C(0x50454552); /* Fixed, arbitrary. */
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = 0x11u;
		config->contentIdentity[i] = 0x22u;
		config->botRulesDigest[i] = 0x33u;
	}
	for (uint8_t i = 0; i <= 1; i++)
	{
		config->slots[i].characterID = i;
		config->slots[i].difficulty = 2;
	}
}

/*
 * Mirrors MakeState in tests/native_lockstep_session_test.c lines 63-72
 * exactly: NativeCanonicalStateV4_Init, frameNumber and
 * control.frameCounter set from frame, then NativeCanonicalStateV4_ComputeDigests.
 * The synthetic digest is a pure function of frame index alone, with no
 * dependency on the real CTR simulation, so both processes derive the same
 * digest for a given frame purely from that frame index. Returns 1 on
 * success, 0 on failure (ComputeDigests rejected the state).
 */
static inline int NativeLockstepPeerLinkFixture_MakeState(struct NativeCanonicalStateV4 *state, uint32_t frame)
{
	NativeCanonicalStateV4_Init(state);
	state->frameNumber = frame;
	state->control.frameCounter = (int32_t)frame;
	return NativeCanonicalStateV4_ComputeDigests(state) == 1;
}

#endif
