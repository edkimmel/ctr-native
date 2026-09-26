#include "platform/native_lobby_state.h"

#include "native_lockstep_peer_link_test_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Candidate-cycling policy tests for platform/native_lobby_state.c
 * (docs/LOBBY_MILESTONE.md section 2.4, Task 6). Every test here uses real
 * loopback UDP sockets through the real, unmodified NativeLockstepPeerLink
 * (Task 5), all within this one test process: a struct NativeLobbyState
 * under test (side A) against either nothing (dead candidates: no socket is
 * ever bound on that port) or a bare struct NativeLockstepPeerLink this file
 * drives directly as the peer (side B), the same "two roles, one process"
 * idiom tests/native_lockstep_peer_link_test.c already uses. Task 5's own
 * two-process test already proves the real cross-process transport; this
 * file proves the candidate-cycling state machine on top of it.
 *
 * Every advance is frame-counted (NativeLobbyState_Poll call count), never
 * wall-clock, so every bound below is a small fixed number of calls, not a
 * timeout: this test is not flaky by construction, and it is run at least
 * twice in a row as part of verification.
 *
 * Fixed loopback test ports, in the 48300-48399 band, distinct from every
 * other test file's own bands (see tests/native_lockstep_peer_link_test.c's
 * own port-band comment for the ones already in use).
 */
#define TEST1_A_PORT 48300u

#define TEST2_A_PORT 48301u
#define TEST2_DEAD1_PORT 48302u
#define TEST2_DEAD2_PORT 48303u

#define TEST3_A_PORT 48304u
#define TEST3_DEAD1_PORT 48305u
#define TEST3_DEAD2_PORT 48306u
#define TEST3_REAL_B_PORT 48307u

#define TEST4_A_PORT 48308u
#define TEST4_B_PORT 48309u

#define TEST5_A_PORT 48310u
#define TEST5_DEAD_PORT 48311u

/* GAP 1 (reviewer-flagged): PEER_LOST/RestartCycle-from-PEER_LOST coverage. */
#define TEST6_A_PORT 48312u
#define TEST6_DEAD1_PORT 48313u
#define TEST6_DEAD2_PORT 48314u
#define TEST6_REAL_B_PORT 48315u

/* GAP 2: Begin rejection-path coverage. */
#define TEST7_A_PORT 48316u
#define TEST7_CANDIDATE_BASE_PORT 48317u /* NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1 == 9 ports: 48317-48325. */

#define TEST8_A_PORT 48326u

#define TEST9_A_PORT 48327u
#define TEST9_CANDIDATE_PORT 48328u

#define TEST10_A_PORT 48329u
#define TEST10_CANDIDATE_PORT 48330u

#define TEST11_A_PORT 48331u
#define TEST11_CANDIDATE_PORT 48332u

/* GAP 3: retransmitIntervalFrames == 0 regression coverage. */
#define TEST12_A_PORT 48333u
#define TEST12_B_PORT 48334u

/* The listen-only lobby (docs/SOLO_CAB_MILESTONE.md SOLO-4, SOLO-S2). */
#define TEST13_A_PORT 48335u
#define TEST13_PEER_PORT 48336u
#define TEST13_STRANGER_PORT 48337u
#define TEST13_DEAD_PORT 48338u
#define TEST14_A_PORT 48339u
#define TEST14_DEAD_PORT 48340u
#define TEST15_A_PORT 48341u
#define TEST15_PEER_PORT 48342u

/* Small, fixed, frame-counted budgets: not wall-clock timeouts. A dead
 * candidate is deterministically abandoned after exactly this many
 * NativeLobbyState_Poll calls; a real loopback handshake completes in a
 * handful of ticks, well inside this budget. */
#define ATTEMPT_FRAMES_PER_CANDIDATE 50u
#define RETRANSMIT_INTERVAL_FRAMES 1u

/* Bounds the outer test-driving loop (waiting for READY/REJECTED); a real
 * loopback round trip is near-instant, so this is generous, not tuned. */
#define DRIVE_BUDGET 5000u

static int TestEmptyCandidateList(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	memset(&state, 0, sizeof(state));

	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST1_A_PORT, NULL, 0u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);
	CHECK(NativeLobbyState_CurrentCandidateIndex(&state) == 0u);

	/* Safe, repeated no-op. */
	NativeLobbyState_Poll(&state);
	NativeLobbyState_Poll(&state);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);

	NativeLobbyState_Close(&state);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	return 0;
}

/*
 * Every candidate points at a port nobody is listening on: after exactly
 * ATTEMPT_FRAMES_PER_CANDIDATE polls each, the state advances past both and
 * lands on WAITING_FOR_PEER once the whole (two-entry) list is exhausted
 * with nobody answering.
 */
static int TestAllDeadCandidatesExhausted(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeUdpTransportAddress candidates[2];
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&candidates[0], "127.0.0.1", (uint16_t)TEST2_DEAD1_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[1], "127.0.0.1", (uint16_t)TEST2_DEAD2_PORT));

	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST2_A_PORT, candidates, 2u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_HANDSHAKING);
	CHECK(NativeLobbyState_CurrentCandidateIndex(&state) == 0u);

	for (tick = 0; tick < (2u * ATTEMPT_FRAMES_PER_CANDIDATE); tick++)
	{
		NativeLobbyState_Poll(&state);
		if (NativeLobbyState_Mode(&state) != NATIVE_LOBBY_STATE_HANDSHAKING)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);
	/* Every candidate was tried: the index walked off the end of the list. */
	CHECK(NativeLobbyState_CurrentCandidateIndex(&state) == 2u);

	NativeLobbyState_Close(&state);
	return 0;
}

/*
 * The first two candidates are dead (nobody listening); the third is a real,
 * listening peer -- a bare NativeLockstepPeerLink this file drives directly
 * with a byte-identical config and the complementary role. The state under
 * test advances past both dead entries and reaches READY once it reaches the
 * real candidate and the real handshake completes, with NativeLobbyState_Link
 * returning a non-NULL link whose own mode is RUNNING.
 *
 * Also covers: RestartCycle is refused (returns 0, mode unchanged) once mode
 * is READY, since there is an active session in progress that must not be
 * silently discarded.
 */
static int TestAdvancePastDeadCandidatesToRealPeer(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState stateA;
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress candidates[3];
	struct NativeUdpTransportAddress addrA;
	struct NativeLockstepPeerLink *link;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&candidates[0], "127.0.0.1", (uint16_t)TEST3_DEAD1_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[1], "127.0.0.1", (uint16_t)TEST3_DEAD2_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[2], "127.0.0.1", (uint16_t)TEST3_REAL_B_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST3_A_PORT));

	/* B is opened first and never closed until the end: its own peer address
	 * is A's fixed local port, which stays the same across every candidate A
	 * tries, so B's repeated resends simply wait until A finally binds that
	 * port for real (candidate index 2) and can receive them. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST3_REAL_B_PORT, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	memset(&stateA, 0, sizeof(stateA));
	CHECK(NativeLobbyState_Begin(&stateA, (uint16_t)TEST3_A_PORT, candidates, 3u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);

	for (tick = 0; tick < DRIVE_BUDGET; tick++)
	{
		NativeLobbyState_Poll(&stateA);
		NativeLockstepPeerLink_Retransmit(&linkB);
		NativeLockstepPeerLink_Poll(&linkB);
		if (NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY);
	/* Proof the dead candidates were actually skipped, not that a very
	 * generous single-candidate budget happened to reach the real peer. */
	CHECK(NativeLobbyState_CurrentCandidateIndex(&stateA) == 2u);

	link = NativeLobbyState_Link(&stateA);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* RestartCycle refused from READY: an active session must not be
	 * silently discarded. */
	CHECK(NativeLobbyState_RestartCycle(&stateA) == 0);
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY);
	CHECK(NativeLockstepPeerLink_Mode(NativeLobbyState_Link(&stateA)) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLobbyState_Close(&stateA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * A deliberately mismatched config (same trick as Task 4: a peer proposal
 * differing only in trackID) reaches REJECTED rather than hanging forever,
 * and NativeLobbyState_RestartCycle from REJECTED successfully starts a
 * fresh attempt against candidate 0 again; the second attempt uses a
 * now-matching config on B's side, proving the state machine really
 * recovers, not just that it flags rejection once.
 */
static int TestMismatchedConfigRejectsThenRestartRecovers(void)
{
	struct NativeMatchConfigV1 goodConfig;
	struct NativeMatchConfigV1 mismatchedConfig;
	struct NativeLobbyState stateA;
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeLockstepPeerLink *link;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&goodConfig);
	mismatchedConfig = goodConfig;
	mismatchedConfig.trackID = goodConfig.trackID ^ UINT32_C(0xffffffff);

	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST4_A_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST4_B_PORT));

	/* Round 1: B proposes a mismatched config. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST4_B_PORT, &addrA, &mismatchedConfig,
		(uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	memset(&stateA, 0, sizeof(stateA));
	CHECK(NativeLobbyState_Begin(&stateA, (uint16_t)TEST4_A_PORT, &addrB, 1u, &goodConfig, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);

	/* RestartCycle refused from HANDSHAKING: an active attempt must not be
	 * silently discarded. */
	CHECK(NativeLobbyState_RestartCycle(&stateA) == 0);
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);

	for (tick = 0; tick < DRIVE_BUDGET; tick++)
	{
		NativeLobbyState_Poll(&stateA);
		NativeLockstepPeerLink_Retransmit(&linkB);
		NativeLockstepPeerLink_Poll(&linkB);
		if (NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_REJECTED)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_REJECTED);
	CHECK(NativeLobbyState_Link(&stateA) == NULL);

	NativeLockstepPeerLink_Close(&linkB);

	/* Round 2: B reopens with a now-matching config; RestartCycle from
	 * REJECTED starts a fresh attempt against candidate 0. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST4_B_PORT, &addrA, &goodConfig, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	CHECK(NativeLobbyState_RestartCycle(&stateA) == 1);
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);
	CHECK(NativeLobbyState_CurrentCandidateIndex(&stateA) == 0u);

	for (tick = 0; tick < DRIVE_BUDGET; tick++)
	{
		NativeLobbyState_Poll(&stateA);
		NativeLockstepPeerLink_Retransmit(&linkB);
		NativeLockstepPeerLink_Poll(&linkB);
		if (NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY);
	link = NativeLobbyState_Link(&stateA);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLobbyState_Close(&stateA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * RestartCycle from HANDSHAKING (a dead candidate that has not yet exhausted
 * its attempt budget) is refused: returns 0, mode stays HANDSHAKING, and the
 * currently open link is left untouched (still reachable through
 * NativeLobbyState_Link).
 */
static int TestRestartRefusedFromHandshaking(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeUdpTransportAddress candidate;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&candidate, "127.0.0.1", (uint16_t)TEST5_DEAD_PORT));

	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST5_A_PORT, &candidate, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_HANDSHAKING);

	/* Stay well inside the attempt budget so the candidate is not exhausted. */
	for (tick = 0; tick < (ATTEMPT_FRAMES_PER_CANDIDATE / 2u); tick++)
	{
		NativeLobbyState_Poll(&state);
	}
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_HANDSHAKING);

	CHECK(NativeLobbyState_RestartCycle(&state) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_HANDSHAKING);
	CHECK(NativeLobbyState_Link(&state) != NULL);

	NativeLobbyState_Close(&state);
	return 0;
}

/*
 * GAP 1 (the important one, reviewer-flagged): NATIVE_LOBBY_STATE_PEER_LOST
 * was entirely untested -- no test drove an underlying peer link to FAULTED
 * or DIVERGED after reaching READY. Drives stateA to READY against a real
 * peer B exactly as TestAdvancePastDeadCandidatesToRealPeer does (two dead
 * candidates, then a real one), then forces stateA's underlying session
 * into FAULTED by feeding it a cleanly composed, correctly-sized bundle
 * with one hand-corrupted body byte and a deliberately stale trailing
 * digest -- the exact "compose cleanly, corrupt one body byte, do not
 * reseal the digest" technique tests/native_lockstep_handshake_fault_test.c's
 * TestMalformedDelivery uses at the handshake layer. At the bundle layer
 * this is simpler to construct deterministically than a real digest
 * mismatch (which needs several real simulated frames on both sides first):
 * native_lockstep_protocol.c's NativeLockstepBundleV1_Decode recomputes an
 * FNV-1a digest over the whole body and rejects any body/digest mismatch
 * with NATIVE_LOCKSTEP_FAULT_BAD_DIGEST before ever inspecting the frame
 * content, so corrupting any body byte (outside the leading magic/version/
 * size header and the trailing digest itself) deterministically reaches
 * that fault regardless of which byte is flipped. The corrupted bundle is
 * sent for real, over B's already-open real socket to A's real socket, so
 * NativeLobbyState_Poll (not a direct in-memory AcceptBundle call) is what
 * actually discovers the fault and flips both the peer link's and the
 * lobby state's mode -- proving the real Poll-driven READY -> PEER_LOST
 * path, not just the session layer underneath it. Also covers
 * NativeLobbyState_RestartCycle from PEER_LOST, documented as a legal
 * source state.
 */
static int TestPeerLostThenRestartRecovers(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState stateA;
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeLockstepSession *sessionB;
	const struct NativeLockstepFaultReport *fault;
	struct NativeUdpTransportAddress candidates[3];
	struct NativeUdpTransportAddress addrA;
	struct NativeLockstepPeerLink *link;
	uint8_t bundleBytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t bundleSize = 0;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&candidates[0], "127.0.0.1", (uint16_t)TEST6_DEAD1_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[1], "127.0.0.1", (uint16_t)TEST6_DEAD2_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[2], "127.0.0.1", (uint16_t)TEST6_REAL_B_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST6_A_PORT));

	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST6_REAL_B_PORT, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	memset(&stateA, 0, sizeof(stateA));
	CHECK(NativeLobbyState_Begin(&stateA, (uint16_t)TEST6_A_PORT, candidates, 3u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);

	for (tick = 0; tick < DRIVE_BUDGET; tick++)
	{
		NativeLobbyState_Poll(&stateA);
		NativeLockstepPeerLink_Retransmit(&linkB);
		NativeLockstepPeerLink_Poll(&linkB);
		if (NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY);
	CHECK(NativeLobbyState_CurrentCandidateIndex(&stateA) == 2u);
	link = NativeLobbyState_Link(&stateA);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* B composes one real, cleanly encoded bundle for consumption frame 0.
	 * frame 0 is within B's own first inputDelay + 1 frames, so
	 * verifiedPresent is legitimately 0 and no RecordLocalDigests call is
	 * needed first for this to compose successfully. One body byte (well
	 * inside the pad payload region, past the header fields and short of
	 * the trailing digest at NATIVE_LOCKSTEP_BUNDLE_V1_DIGEST_OFFSET) is
	 * then hand-corrupted without recomputing the digest, and the whole
	 * still-correctly-sized record is sent to A over the real socket B
	 * already has open to it -- not a direct in-memory AcceptBundle call. */
	sessionB = NativeLockstepPeerLink_Session(&linkB);
	CHECK(sessionB != NULL);
	CHECK(NativeLockstepSession_ComposeBundle(sessionB, 0u, bundleBytes, sizeof(bundleBytes), &bundleSize) == 1);
	CHECK(bundleSize == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	bundleBytes[40] ^= 0x01u; /* A pad-region body byte; leaves the trailing digest stale. */
	CHECK(NativeUdpTransport_Send(&linkB.transport, &addrA, bundleBytes, bundleSize));

	/* A's own Poll drains and rejects the corrupted bundle: the underlying
	 * session latches FAULTED, NativeLockstepPeerLink_Poll mirrors that onto
	 * the peer link's own mode, and NativeLobbyState_Poll's PollReady branch
	 * observes it and flips the lobby state to PEER_LOST. */
	for (tick = 0; tick < DRIVE_BUDGET; tick++)
	{
		NativeLobbyState_Poll(&stateA);
		if (NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_PEER_LOST)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_PEER_LOST);
	link = NativeLobbyState_Link(&stateA);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	CHECK(NativeLockstepSession_Mode(NativeLockstepPeerLink_Session(link)) == NATIVE_LOCKSTEP_FAULTED);
	fault = NativeLockstepSession_FirstFault(NativeLockstepPeerLink_Session(link));
	CHECK(fault != NULL);
	CHECK(fault->cause == (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST);

	/* RestartCycle is accepted from PEER_LOST (documented as a legal source
	 * state) and correctly re-opens against candidate 0 (not the candidate
	 * that was active when PEER_LOST was reached) with a fresh underlying
	 * peer link. */
	CHECK(NativeLobbyState_RestartCycle(&stateA) == 1);
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);
	CHECK(NativeLobbyState_CurrentCandidateIndex(&stateA) == 0u);
	link = NativeLobbyState_Link(&stateA);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	NativeLobbyState_Close(&stateA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * GAP 2: NativeLobbyState_Begin's documented rejection paths had no
 * dedicated tests. candidateCount exceeding NATIVE_LOBBY_STATE_MAX_CANDIDATES
 * is rejected (returns 0) and leaves mode at its zero-initialized default.
 */
static int TestBeginRejectsExcessiveCandidateCount(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeUdpTransportAddress candidates[NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u];
	uint32_t i;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	for (i = 0; i < (NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u); i++)
	{
		CHECK(NativeUdpTransport_MakeAddress(&candidates[i], "127.0.0.1", (uint16_t)(TEST7_CANDIDATE_BASE_PORT + i)));
	}

	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST7_A_PORT, candidates, (uint32_t)(NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u), &config,
		(uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE,
		RETRANSMIT_INTERVAL_FRAMES) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);
	return 0;
}

/* GAP 2: a NULL candidates pointer with a nonzero candidateCount is
 * rejected. */
static int TestBeginRejectsNullCandidatesWithNonzeroCount(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST8_A_PORT, NULL, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);
	return 0;
}

/* GAP 2: a localRole that is not CAB1_HUMAN or CAB2_HUMAN (here, BOT) is
 * rejected. */
static int TestBeginRejectsInvalidLocalRole(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeUdpTransportAddress candidate;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&candidate, "127.0.0.1", (uint16_t)TEST9_CANDIDATE_PORT));

	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST9_A_PORT, &candidate, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_BOT,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);

	/* INACTIVE is rejected too, not just BOT. */
	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST9_A_PORT, &candidate, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_INACTIVE,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	return 0;
}

/*
 * GAP 2: a proposedConfig failing NativeMatchConfigV1_Validate is rejected.
 * This is also the one rejection path (of the reviewer's choosing budget)
 * that asserts the whole state struct is left byte-identical to how it was
 * before the failed Begin call, matching the header's "on any invalid
 * input... returns 0 and changes nothing" contract: the struct starts
 * zero-initialized (the same "never a valid begun state" condition every
 * other test in this file starts from), and a full-struct memcmp after the
 * failed call proves Begin wrote nothing at all, not just that mode
 * happens to read back as the zero default.
 */
static int TestBeginRejectsInvalidConfigAndTouchesNothing(void)
{
	struct NativeMatchConfigV1 badConfig;
	struct NativeLobbyState state;
	struct NativeLobbyState zeroed;
	struct NativeUdpTransportAddress candidate;

	NativeLockstepPeerLinkFixture_BuildConfig(&badConfig);
	badConfig.lapCount = 0u; /* NativeMatchConfigV1_Validate requires a nonzero lapCount. */
	CHECK(!NativeMatchConfigV1_Validate(&badConfig));
	CHECK(NativeUdpTransport_MakeAddress(&candidate, "127.0.0.1", (uint16_t)TEST10_CANDIDATE_PORT));

	memset(&state, 0, sizeof(state));
	memset(&zeroed, 0, sizeof(zeroed));

	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST10_A_PORT, &candidate, 1u, &badConfig, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES) == 0);
	CHECK(memcmp(&state, &zeroed, sizeof(state)) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);
	return 0;
}

/* GAP 2: attemptFramesPerCandidate == 0 is rejected. */
static int TestBeginRejectsZeroAttemptFramesPerCandidate(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeUdpTransportAddress candidate;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&candidate, "127.0.0.1", (uint16_t)TEST11_CANDIDATE_PORT));

	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST11_A_PORT, &candidate, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, 0u, RETRANSMIT_INTERVAL_FRAMES) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_Link(&state) == NULL);
	return 0;
}

/*
 * GAP 3: the retransmitIntervalFrames == 0 defensive branch had no
 * regression test. Reading platform/native_lobby_state.c's
 * NativeLobbyState_PollHandshaking: with retransmitIntervalFrames == 0 the
 * "(state->retransmitIntervalFrames != 0u) && (...)" short-circuit means
 * the modulo is never evaluated and NativeLockstepPeerLink_Retransmit is
 * never called from this module's own Poll -- confirmed "no periodic
 * resend", not "every tick" and not a division by zero. The only HELLO
 * stateA ever sends is therefore the single one NativeLockstepPeerLink_Open
 * already sends immediately when NativeLobbyState_Begin opens candidate 0.
 *
 * This is not enough on its own to prove interval 0 is safe: it must also
 * be shown not to silently break connectivity. Over a real, lossless
 * loopback UDP socket a single sent HELLO is not lost, so stateA still
 * reaches READY here, with the OTHER side (B, driven directly by this test
 * exactly as every other real-peer test in this file drives B) retransmitting
 * on its own normal schedule -- proving interval 0 means "this module's own
 * periodic resend is off, the caller/peer is relied on for the rest" and
 * does not stall a handshake against an otherwise-cooperating peer, while
 * also, across dozens of NativeLobbyState_Poll calls, producing no crash or
 * incorrect state transition.
 */
static int TestZeroRetransmitIntervalDisablesOwnResendButStillConnects(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState stateA;
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeLockstepPeerLink *link;
	uint32_t tick;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST12_A_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST12_B_PORT));

	/* B is opened first, exactly like TestAdvancePastDeadCandidatesToRealPeer,
	 * so its socket is already bound and listening before stateA's Begin call
	 * sends its own one-shot HELLO. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST12_B_PORT, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	memset(&stateA, 0, sizeof(stateA));
	CHECK(NativeLobbyState_Begin(&stateA, (uint16_t)TEST12_A_PORT, &addrB, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, 0u));
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_HANDSHAKING);

	for (tick = 0; tick < DRIVE_BUDGET; tick++)
	{
		/* stateA's own Poll never retransmits (interval 0): only B does,
		 * every tick, on its own normal schedule. */
		NativeLobbyState_Poll(&stateA);
		NativeLockstepPeerLink_Retransmit(&linkB);
		NativeLockstepPeerLink_Poll(&linkB);
		if (NativeLobbyState_Mode(&stateA) != NATIVE_LOBBY_STATE_HANDSHAKING)
		{
			break;
		}
	}
	CHECK(NativeLobbyState_Mode(&stateA) == NATIVE_LOBBY_STATE_READY);
	link = NativeLobbyState_Link(&stateA);
	CHECK(link != NULL);
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLobbyState_Close(&stateA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/* ---- The listen-only lobby (SOLO-4) ---- */

/* Polls the listening state `count` times; returns 0 if the probe transport
 * received anything in between (the listen-only link must never send). */
static int PollListeningQuiet(struct NativeLobbyState *state, struct NativeUdpTransport *probe, uint32_t count)
{
	uint8_t bytes[512];
	size_t size = 0;
	struct NativeUdpTransportAddress sender;
	uint32_t i;

	for (i = 0; i < count; i++)
	{
		NativeLobbyState_Poll(state);
		CHECK(NativeLobbyState_Mode(state) == NATIVE_LOBBY_STATE_LISTENING);
		CHECK(NativeUdpTransport_Receive(probe, bytes, sizeof(bytes), &size, &sender) == NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY);
	}
	return 0;
}

/* One well-formed handshake HELLO (284 bytes) from role CAB2 on config. */
static int ComposeHello(const struct NativeMatchConfigV1 *config, uint8_t *bytes, size_t capacity, size_t *size)
{
	struct NativeLockstepHandshake handshake;

	NativeLockstepHandshake_Init(&handshake);
	CHECK(NativeLockstepHandshake_Begin(&handshake, config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN));
	CHECK(NativeLockstepHandshake_ComposeMessage(&handshake, bytes, capacity, size));
	CHECK(*size == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES);
	return 0;
}

/* One well-formed handshake reply (284 bytes) from role CAB2 on config:
 * an ACCEPT (rejectReason NONE) or a REJECT (with its reason). */
static int ComposeReply(const struct NativeMatchConfigV1 *config, uint8_t messageType, uint8_t rejectReason, uint8_t *bytes,
	size_t capacity, size_t *size)
{
	struct NativeLockstepHandshakeMessageV1 message;
	struct NativeCodecWriter writer;

	memset(&message, 0, sizeof(message));
	message.messageType = messageType;
	message.senderRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	message.rejectReason = rejectReason;
	message.config = *config;
	NativeCodecWriter_Init(&writer, bytes, capacity, NULL);
	CHECK(NativeLockstepHandshakeMessageV1_Encode(&writer, &message));
	*size = NativeCodecWriter_Size(&writer);
	CHECK(*size == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES);
	return 0;
}

/*
 * The listen-only lobby never sends, and it filters what it hears: only a
 * well-formed handshake HELLO from a candidate address latches peerHeard.
 * A malformed handshake-width datagram, a datagram of another width, a
 * well-formed ACCEPT or REJECT from a candidate, and a well-formed HELLO from
 * an address that is not a candidate do not.
 */
static int TestListenNeverSendsAndFilters(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeUdpTransportAddress candidates[2];
	struct NativeUdpTransportAddress listenAddress;
	struct NativeUdpTransport peer;
	struct NativeUdpTransport stranger;
	uint8_t hello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t junk[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t aux[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t accept[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t reject[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	size_t helloSize = 0;
	size_t acceptSize = 0;
	size_t rejectSize = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(ComposeHello(&config, hello, sizeof(hello), &helloSize) == 0);
	CHECK(ComposeReply(&config, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE,
		      accept, sizeof(accept), &acceptSize) == 0);
	CHECK(ComposeReply(&config, (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT,
		      (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH, reject, sizeof(reject), &rejectSize) == 0);
	memset(junk, 0x5A, sizeof(junk));
	memset(aux, 0x3C, sizeof(aux));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[0], "127.0.0.1", (uint16_t)TEST13_DEAD_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&candidates[1], "127.0.0.1", (uint16_t)TEST13_PEER_PORT));
	CHECK(NativeUdpTransport_MakeAddress(&listenAddress, "127.0.0.1", (uint16_t)TEST13_A_PORT));

	CHECK(NativeUdpTransport_GlobalInit());
	memset(&peer, 0, sizeof(peer));
	memset(&stranger, 0, sizeof(stranger));
	CHECK(NativeUdpTransport_Open(&peer, (uint16_t)TEST13_PEER_PORT));
	CHECK(NativeUdpTransport_Open(&stranger, (uint16_t)TEST13_STRANGER_PORT));

	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_BeginListen(&state, (uint16_t)TEST13_A_PORT, candidates, 2u) == 1);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_LISTENING);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);
	/* No handshake link to drive, and no cycle to restart. */
	CHECK(NativeLobbyState_Link(&state) == NULL);
	CHECK(NativeLobbyState_RestartCycle(&state) == 0);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_LISTENING);
	CHECK(NativeLockstepPeerLink_Mode(&state.link) == NATIVE_LOCKSTEP_PEER_LINK_LISTENING);

	/* Nothing is ever sent, to a candidate or anyone. */
	CHECK(PollListeningQuiet(&state, &peer, 30u) == 0);
	CHECK(PollListeningQuiet(&state, &stranger, 5u) == 0);
	/* The peer link's other entry points do nothing on a listening link. */
	NativeLockstepPeerLink_Retransmit(&state.link);
	CHECK(NativeLockstepPeerLink_SendAux(&state.link, aux, sizeof(aux)) == 0);
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&state.link, 0u) == 0);
	CHECK(PollListeningQuiet(&state, &peer, 1u) == 0);

	/* A malformed handshake-width datagram from the candidate: not heard. */
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, junk, sizeof(junk)));
	CHECK(PollListeningQuiet(&state, &peer, 3u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);

	/* An aux-width datagram from the candidate: not heard. */
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, aux, sizeof(aux)));
	CHECK(PollListeningQuiet(&state, &peer, 3u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);

	/* A truncated HELLO from the candidate: not heard. */
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, hello, helloSize - 1u));
	CHECK(PollListeningQuiet(&state, &peer, 3u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);

	/* A well-formed ACCEPT, then a well-formed REJECT, from the candidate:
	 * not heard, and not answered. Only a HELLO shows a peer in its lobby. */
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, accept, acceptSize));
	CHECK(PollListeningQuiet(&state, &peer, 3u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, reject, rejectSize));
	CHECK(PollListeningQuiet(&state, &peer, 3u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, accept, acceptSize));
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, reject, rejectSize));
	CHECK(NativeLockstepPeerLink_PollListen(&state.link, candidates, 2u) == 0u);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);

	/* A well-formed HELLO from an address that is not a candidate: not
	 * heard, and not answered. */
	CHECK(NativeUdpTransport_Send(&stranger, &listenAddress, hello, helloSize));
	CHECK(PollListeningQuiet(&state, &stranger, 3u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);

	/* The ordinary Poll is a no-op on a listening link: a HELLO it would
	 * otherwise read stays queued for the listen poll. */
	CHECK(NativeUdpTransport_Send(&peer, &listenAddress, hello, helloSize));
	NativeLockstepPeerLink_Poll(&state.link);
	CHECK(NativeLockstepPeerLink_Mode(&state.link) == NATIVE_LOCKSTEP_PEER_LINK_LISTENING);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);

	/* The well-formed HELLO from the candidate: heard, latched, and not
	 * answered. */
	CHECK(PollListeningQuiet(&state, &peer, 1u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 1);
	CHECK(PollListeningQuiet(&state, &peer, 20u) == 0);
	CHECK(NativeLobbyState_PeerHeard(&state) == 1);
	CHECK(NativeLockstepPeerLink_Mode(&state.link) == NATIVE_LOCKSTEP_PEER_LINK_LISTENING);

	/* Close clears the latch. */
	NativeLobbyState_Close(&state);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&state.link) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);

	NativeUdpTransport_Close(&stranger);
	NativeUdpTransport_Close(&peer);
	NativeUdpTransport_GlobalShutdown();
	return 0;
}

/*
 * The listen-only lobby closes and reopens on the same local port, as a
 * cabinet does between LOBBY and solo: listen, close, listen again, close,
 * then an ordinary handshaking lobby on that same port. A second listener on
 * a port in use fails and changes nothing.
 */
static int TestListenCloseAndReopenSamePort(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLobbyState state;
	struct NativeLobbyState other;
	struct NativeLobbyState sentinel;
	struct NativeUdpTransportAddress dead;
	uint32_t round;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&dead, "127.0.0.1", (uint16_t)TEST14_DEAD_PORT));
	memset(&state, 0, sizeof(state));

	for (round = 0; round < 3u; round++)
	{
		CHECK(NativeLobbyState_BeginListen(&state, (uint16_t)TEST14_A_PORT, &dead, 1u) == 1);
		CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_LISTENING);

		/* The port is taken while listening. */
		memset(&other, 0xA5, sizeof(other));
		memcpy(&sentinel, &other, sizeof(other));
		CHECK(NativeLobbyState_BeginListen(&other, (uint16_t)TEST14_A_PORT, &dead, 1u) == 0);
		CHECK(memcmp(&other, &sentinel, sizeof(other)) == 0);

		NativeLobbyState_Poll(&state);
		NativeLobbyState_Close(&state);
		CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_WAITING_FOR_PEER);
		NativeLobbyState_Close(&state);
	}

	/* The same port serves an ordinary lobby next (LOBBY after solo). */
	CHECK(NativeLobbyState_Begin(&state, (uint16_t)TEST14_A_PORT, &dead, 1u, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, ATTEMPT_FRAMES_PER_CANDIDATE, RETRANSMIT_INTERVAL_FRAMES));
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_HANDSHAKING);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);
	NativeLobbyState_Close(&state);

	/* ...and a listener after it. */
	CHECK(NativeLobbyState_BeginListen(&state, (uint16_t)TEST14_A_PORT, NULL, 0u) == 1);
	CHECK(NativeLobbyState_Mode(&state) == NATIVE_LOBBY_STATE_LISTENING);
	NativeLobbyState_Poll(&state);
	CHECK(NativeLobbyState_PeerHeard(&state) == 0);
	NativeLobbyState_Close(&state);
	return 0;
}

/* BeginListen validates like Begin and changes nothing on a rejection; the
 * NULL-state entry points are safe. */
static int TestListenRejections(void)
{
	struct NativeLobbyState state;
	struct NativeLobbyState sentinel;
	struct NativeUdpTransportAddress candidates[NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u];
	uint32_t i;

	for (i = 0; i < (NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u); i++)
	{
		CHECK(NativeUdpTransport_MakeAddress(&candidates[i], "127.0.0.1", (uint16_t)(TEST15_PEER_PORT)));
	}
	memset(&state, 0xA5, sizeof(state));
	memcpy(&sentinel, &state, sizeof(state));
	CHECK(NativeLobbyState_BeginListen(NULL, (uint16_t)TEST15_A_PORT, candidates, 1u) == 0);
	CHECK(NativeLobbyState_BeginListen(&state, (uint16_t)TEST15_A_PORT, candidates, NATIVE_LOBBY_STATE_MAX_CANDIDATES + 1u) == 0);
	CHECK(memcmp(&state, &sentinel, sizeof(state)) == 0);
	CHECK(NativeLobbyState_BeginListen(&state, (uint16_t)TEST15_A_PORT, NULL, 1u) == 0);
	CHECK(memcmp(&state, &sentinel, sizeof(state)) == 0);
	CHECK(NativeLobbyState_PeerHeard(NULL) == 0);

	/* A full candidate list is accepted. */
	memset(&state, 0, sizeof(state));
	CHECK(NativeLobbyState_BeginListen(&state, (uint16_t)TEST15_A_PORT, candidates, NATIVE_LOBBY_STATE_MAX_CANDIDATES) == 1);
	CHECK(state.candidateCount == NATIVE_LOBBY_STATE_MAX_CANDIDATES);
	NativeLobbyState_Close(&state);

	/* The bare peer-link entry points refuse NULL and other modes. */
	CHECK(NativeLockstepPeerLink_OpenListen(NULL, (uint16_t)TEST15_A_PORT) == 0);
	CHECK(NativeLockstepPeerLink_PollListen(NULL, candidates, 1u) == 0u);
	CHECK(NativeLockstepPeerLink_PollListen(&state.link, candidates, 1u) == 0u);
	return 0;
}

int main(void)
{
	CHECK(TestEmptyCandidateList() == 0);
	CHECK(TestAllDeadCandidatesExhausted() == 0);
	CHECK(TestAdvancePastDeadCandidatesToRealPeer() == 0);
	CHECK(TestMismatchedConfigRejectsThenRestartRecovers() == 0);
	CHECK(TestRestartRefusedFromHandshaking() == 0);
	CHECK(TestPeerLostThenRestartRecovers() == 0);
	CHECK(TestBeginRejectsExcessiveCandidateCount() == 0);
	CHECK(TestBeginRejectsNullCandidatesWithNonzeroCount() == 0);
	CHECK(TestBeginRejectsInvalidLocalRole() == 0);
	CHECK(TestBeginRejectsInvalidConfigAndTouchesNothing() == 0);
	CHECK(TestBeginRejectsZeroAttemptFramesPerCandidate() == 0);
	CHECK(TestZeroRetransmitIntervalDisablesOwnResendButStillConnects() == 0);
	CHECK(TestListenNeverSendsAndFilters() == 0);
	CHECK(TestListenCloseAndReopenSamePort() == 0);
	CHECK(TestListenRejections() == 0);
	puts("native_lobby_state_test: passed");
	return 0;
}
