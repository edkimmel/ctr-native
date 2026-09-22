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

int main(void)
{
	CHECK(TestEmptyCandidateList() == 0);
	CHECK(TestAllDeadCandidatesExhausted() == 0);
	CHECK(TestAdvancePastDeadCandidatesToRealPeer() == 0);
	CHECK(TestMismatchedConfigRejectsThenRestartRecovers() == 0);
	CHECK(TestRestartRefusedFromHandshaking() == 0);
	puts("native_lobby_state_test: passed");
	return 0;
}
