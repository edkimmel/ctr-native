#ifndef TESTS_NATIVE_ARCADE_LINK_LOOPBACK_TEST_FIXTURE_H
#define TESTS_NATIVE_ARCADE_LINK_LOOPBACK_TEST_FIXTURE_H

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_link_options.h"
#include "platform/native_arcade_netplay.h"

#include <stdint.h>

/*
 * Tiny shared, header-only, test-only loopback harness for the arcade-link
 * host glue, included by tests/native_arcade_link_host_test.c and
 * tests/main_arcade_link_view_layout_test.c so both drive the host
 * singleton (as cabinet 1) against a test-owned NativeArcadeNetplay adapter
 * (as cabinet 2) the same way. Every advance is tick-counted, never
 * wall-clock. Each including test picks its own fixed loopback port band.
 */

#define NATIVE_ARCADE_LINK_LOOPBACK_IPV4 UINT32_C(0x7F000001)

/* A fixed identity that builds the fixed fixture. */
static inline void NativeArcadeLinkLoopback_Identity(struct NativeIdentityV1 *identity)
{
	uint32_t i;

	for (i = 0u; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		identity->build[i] = (uint8_t)(i + 1u);
		identity->content[i] = (uint8_t)(0x80u + i);
	}
}

/* Host options with the link enabled as role, on localPort, with one
 * loopback candidate on peerPort. */
static inline void NativeArcadeLinkLoopback_LinkOptions(struct NativeArcadeLinkOptions *options, uint8_t role,
	uint32_t localPort, uint32_t peerPort)
{
	NativeArcadeLinkOptions_SetDefaults(options);
	options->enabled = 1u;
	options->localRole = role;
	options->localPort = (uint16_t)localPort;
	options->peers[0].ipv4 = NATIVE_ARCADE_LINK_LOOPBACK_IPV4;
	options->peers[0].port = (uint16_t)peerPort;
	options->peerCount = 1u;
}

/* Initializes the test-owned peer adapter as cabinet 2 on peerPort against
 * the host on hostPort, with the same fixture and the default timings.
 * Returns 1 on success. */
static inline int NativeArcadeLinkLoopback_PeerInit(struct NativeArcadeNetplay *peer,
	const struct NativeIdentityV1 *identity, uint32_t hostPort, uint32_t peerPort, uint64_t selectEntropy)
{
	struct NativeArcadeNetplayConfig config;

	NativeArcadeNetplay_DefaultConfig(&config);
	if (!NativeArcadeLinkFixture_Build(identity, &config.fixture))
	{
		return 0;
	}
	config.candidates[0].ipv4 = NATIVE_ARCADE_LINK_LOOPBACK_IPV4;
	config.candidates[0].port = (uint16_t)hostPort;
	config.candidateCount = 1u;
	config.localPort = (uint16_t)peerPort;
	config.localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	config.selectEntropy = selectEntropy;
	return NativeArcadeNetplay_Init(peer, &config);
}

/* One tick of the host (cabinet 1) and the peer adapter (cabinet 2). */
static inline void NativeArcadeLinkLoopback_TickPair(struct NativeArcadeNetplay *peer, uint32_t heldHost,
	uint32_t heldPeer, uint32_t *hostAction, uint32_t *peerAction)
{
	*hostAction = NativeArcadeLinkHost_Tick(heldHost, 0u);
	*peerAction = (uint32_t)NativeArcadeNetplay_Tick(peer, heldPeer, 0u);
}

/* A press on either side: a held tick, then a released tick. Returns 1 when
 * every action of both ticks was NONE, else 0. */
static inline int NativeArcadeLinkLoopback_PressPair(struct NativeArcadeNetplay *peer, uint32_t heldHost,
	uint32_t heldPeer)
{
	uint32_t hostAction;
	uint32_t peerAction;
	int quiet;

	NativeArcadeLinkLoopback_TickPair(peer, heldHost, heldPeer, &hostAction, &peerAction);
	quiet = (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE) &&
	        (peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE);
	NativeArcadeLinkLoopback_TickPair(peer, 0u, 0u, &hostAction, &peerAction);
	quiet = quiet && (hostAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE) &&
	        (peerAction == (uint32_t)NATIVE_ARCADE_FLOW_ACTION_NONE);
	return quiet;
}

#endif
