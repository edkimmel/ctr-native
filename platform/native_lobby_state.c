#include "platform/native_lobby_state.h"

#include <string.h>

int NativeLobbyState_Begin(struct NativeLobbyState *state, uint16_t localPort,
	const struct NativeUdpTransportAddress *candidates, uint32_t candidateCount,
	const struct NativeMatchConfigV1 *proposedConfig, uint8_t localRole, uint32_t inputDelay,
	uint32_t attemptFramesPerCandidate, uint32_t retransmitIntervalFrames)
{
	uint32_t i;

	if ((state == NULL) || (proposedConfig == NULL))
	{
		return 0;
	}
	if (candidateCount > NATIVE_LOBBY_STATE_MAX_CANDIDATES)
	{
		return 0;
	}
	if ((candidateCount > 0u) && (candidates == NULL))
	{
		return 0;
	}
	if ((localRole != (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (localRole != (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
	{
		return 0;
	}
	if (!NativeMatchConfigV1_Validate(proposedConfig))
	{
		return 0;
	}
	if (attemptFramesPerCandidate == 0u)
	{
		return 0;
	}

	if (candidateCount > 0u)
	{
		/* Attempt the first candidate before touching *state at all, so a
		 * failure here (e.g. localPort already in use) leaves *state
		 * untouched, mirroring NativeLockstepPeerLink_Open's own
		 * "changes nothing on failure" convention. */
		struct NativeLockstepPeerLink link = {0};

		if (!NativeLockstepPeerLink_Open(&link, localPort, &candidates[0], proposedConfig, localRole, inputDelay))
		{
			return 0;
		}

		memset(state, 0, sizeof(*state));
		state->link = link;
		state->mode = NATIVE_LOBBY_STATE_HANDSHAKING;
	}
	else
	{
		memset(state, 0, sizeof(*state));
		state->mode = NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
	}

	for (i = 0; i < candidateCount; i++)
	{
		state->candidates[i] = candidates[i];
	}
	state->candidateCount = candidateCount;
	state->currentCandidateIndex = 0u;
	state->candidateFrameCounter = 0u;
	state->attemptFramesPerCandidate = attemptFramesPerCandidate;
	state->retransmitIntervalFrames = retransmitIntervalFrames;
	state->localPort = localPort;
	state->localRole = localRole;
	state->inputDelay = inputDelay;
	state->proposedConfig = *proposedConfig;
	return 1;
}

/*
 * Closes the current candidate's link (if any is open) and tries candidate 0
 * again, using the state's own stored config/role/inputDelay. Shared by
 * NativeLobbyState_RestartCycle and, indirectly, by the candidate-advance
 * step in NativeLobbyState_Poll (which instead advances to the next index,
 * not back to 0 -- see NativeLobbyState_Poll for that logic). Always leaves
 * the state in either HANDSHAKING (open succeeded) or WAITING_FOR_PEER (no
 * candidates, or opening candidate 0 failed).
 */
static void NativeLobbyState_OpenCandidateZeroOrWait(struct NativeLobbyState *state)
{
	NativeLockstepPeerLink_Close(&state->link);
	state->currentCandidateIndex = 0u;
	state->candidateFrameCounter = 0u;

	if (state->candidateCount == 0u)
	{
		state->mode = NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
		return;
	}

	if (NativeLockstepPeerLink_Open(&state->link, state->localPort, &state->candidates[0], &state->proposedConfig,
		    state->localRole, state->inputDelay))
	{
		state->mode = NATIVE_LOBBY_STATE_HANDSHAKING;
	}
	else
	{
		state->mode = NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
	}
}

static void NativeLobbyState_PollHandshaking(struct NativeLobbyState *state)
{
	enum NativeLockstepPeerLinkMode linkMode;

	state->candidateFrameCounter++;
	if ((state->retransmitIntervalFrames != 0u) && ((state->candidateFrameCounter % state->retransmitIntervalFrames) == 0u))
	{
		/* Must run before this tick's Poll call below: see
		 * NativeLockstepPeerLink_Retransmit's doc comment on the
		 * Retransmit-before-Poll ordering contract while HANDSHAKING. */
		NativeLockstepPeerLink_Retransmit(&state->link);
	}
	NativeLockstepPeerLink_Poll(&state->link);

	linkMode = NativeLockstepPeerLink_Mode(&state->link);
	if (linkMode == NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
	{
		state->mode = NATIVE_LOBBY_STATE_READY;
		return;
	}
	if (linkMode == NATIVE_LOCKSTEP_PEER_LINK_REJECTED)
	{
		NativeLockstepPeerLink_Close(&state->link);
		state->mode = NATIVE_LOBBY_STATE_REJECTED;
		return;
	}

	if (state->candidateFrameCounter < state->attemptFramesPerCandidate)
	{
		/* Still within budget for this candidate: keep waiting. */
		return;
	}

	/* Nobody answered this candidate in time: close it, advance to the next
	 * candidate index (never auto-wrapping back to 0), and open the next one
	 * if there is one; otherwise give up for this cycle. */
	NativeLockstepPeerLink_Close(&state->link);
	state->currentCandidateIndex++;
	state->candidateFrameCounter = 0u;

	if (state->currentCandidateIndex < state->candidateCount)
	{
		if (NativeLockstepPeerLink_Open(&state->link, state->localPort, &state->candidates[state->currentCandidateIndex],
			    &state->proposedConfig, state->localRole, state->inputDelay))
		{
			state->mode = NATIVE_LOBBY_STATE_HANDSHAKING;
		}
		else
		{
			/* Defensive: opening the next candidate itself failed (not the
			 * documented "nobody answered" case). Keep advancing rather than
			 * getting stuck. */
			state->mode = NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
		}
	}
	else
	{
		state->mode = NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
	}
}

static void NativeLobbyState_PollReady(struct NativeLobbyState *state)
{
	enum NativeLockstepPeerLinkMode linkMode;

	NativeLockstepPeerLink_Poll(&state->link);
	linkMode = NativeLockstepPeerLink_Mode(&state->link);
	if ((linkMode == NATIVE_LOCKSTEP_PEER_LINK_FAULTED) || (linkMode == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED))
	{
		state->mode = NATIVE_LOBBY_STATE_PEER_LOST;
	}
}

void NativeLobbyState_Poll(struct NativeLobbyState *state)
{
	if (state == NULL)
	{
		return;
	}

	if (state->mode == NATIVE_LOBBY_STATE_HANDSHAKING)
	{
		NativeLobbyState_PollHandshaking(state);
	}
	else if (state->mode == NATIVE_LOBBY_STATE_READY)
	{
		NativeLobbyState_PollReady(state);
	}
	/* WAITING_FOR_PEER, REJECTED, PEER_LOST: no-op. */
}

int NativeLobbyState_RestartCycle(struct NativeLobbyState *state)
{
	if (state == NULL)
	{
		return 0;
	}
	if ((state->mode != NATIVE_LOBBY_STATE_WAITING_FOR_PEER) && (state->mode != NATIVE_LOBBY_STATE_REJECTED) &&
	    (state->mode != NATIVE_LOBBY_STATE_PEER_LOST))
	{
		return 0;
	}

	NativeLobbyState_OpenCandidateZeroOrWait(state);
	return 1;
}

enum NativeLobbyStateMode NativeLobbyState_Mode(const struct NativeLobbyState *state)
{
	return (state != NULL) ? state->mode : NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
}

struct NativeLockstepPeerLink *NativeLobbyState_Link(struct NativeLobbyState *state)
{
	if (state == NULL)
	{
		return NULL;
	}
	if (NativeLockstepPeerLink_Mode(&state->link) == NATIVE_LOCKSTEP_PEER_LINK_IDLE)
	{
		return NULL;
	}
	return &state->link;
}

uint32_t NativeLobbyState_CurrentCandidateIndex(const struct NativeLobbyState *state)
{
	return (state != NULL) ? state->currentCandidateIndex : 0u;
}

void NativeLobbyState_Close(struct NativeLobbyState *state)
{
	if (state == NULL)
	{
		return;
	}
	NativeLockstepPeerLink_Close(&state->link);
	state->mode = NATIVE_LOBBY_STATE_WAITING_FOR_PEER;
}
