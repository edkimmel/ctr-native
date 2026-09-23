#include "platform/native_arcade_netplay.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_menu_input.h"
#include "platform/native_lobby_state.h"
#include "platform/native_lockstep_match_outcome.h"
#include "platform/native_lockstep_match_roster.h"
#include "platform/native_lockstep_rematch.h"
#include "platform/native_match_config.h"
#include "platform/native_match_select_message.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_match_select_session.h"
#include "platform/native_sha256.h"

/* A composed select record travels as exactly one aux datagram. */
_Static_assert(NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES,
	"the select record must fill exactly one peer-link aux datagram");

/* The ASCII domain tag of the select nonce hash, without its NUL. */
static const char k_selectNonceTag[] = "CTRN match select nonce v1";

void NativeArcadeNetplay_DefaultConfig(struct NativeArcadeNetplayConfig *config)
{
	if (config == NULL)
	{
		return;
	}
	memset(config, 0, sizeof(*config));
	config->localRole = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
	config->inputDelay = NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY;
	config->attemptTicksPerCandidate = NATIVE_ARCADE_NETPLAY_DEFAULT_ATTEMPT_TICKS_PER_CANDIDATE;
	config->retransmitIntervalTicks = NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS;
	config->stallTimeoutTicks = NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS;
	NativeArcadeFlow_DefaultTimings(&config->timings);
	NativeMatchSelectSession_DefaultTimings(&config->selectTimings);
	config->selectEntropy = 0u;
}

int NativeArcadeNetplay_Init(struct NativeArcadeNetplay *netplay, const struct NativeArcadeNetplayConfig *config)
{
	struct NativeArcadeFlow flow;
	uint8_t localSlot = 0u;

	if ((netplay == NULL) || (config == NULL))
	{
		return 0;
	}
	if (!NativeMatchConfigV1_Validate(&config->fixture))
	{
		return 0;
	}
	if ((config->localRole != (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) &&
		(config->localRole != (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
	{
		return 0;
	}
	if (!NativeMatchConfigV1_FindRoleSlot(&config->fixture, config->localRole, &localSlot))
	{
		return 0;
	}
	if (config->localPort == 0u)
	{
		return 0;
	}
	if ((config->candidateCount == 0u) || (config->candidateCount > NATIVE_LOBBY_STATE_MAX_CANDIDATES))
	{
		return 0;
	}
	if (config->attemptTicksPerCandidate == 0u)
	{
		return 0;
	}
	/* Only 1 honours the peer-link Retransmit-before-Poll contract (UX-5);
	 * any other cadence can hang a handshake. */
	if (config->retransmitIntervalTicks != NATIVE_ARCADE_NETPLAY_DEFAULT_RETRANSMIT_INTERVAL_TICKS)
	{
		return 0;
	}
	if ((config->inputDelay < (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY) ||
		(config->inputDelay > (uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY))
	{
		return 0;
	}
	if ((config->stallTimeoutTicks < NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES) ||
		(config->stallTimeoutTicks > NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES))
	{
		return 0;
	}
	if (!NativeArcadeFlow_Init(&flow, &config->timings))
	{
		return 0;
	}
	if ((config->selectTimings.characterTicks == 0u) || (config->selectTimings.trackTicks == 0u) ||
		(config->selectTimings.lapTicks == 0u) || (config->selectTimings.peerSilenceTicks == 0u))
	{
		return 0;
	}

	memset(netplay, 0, sizeof(*netplay));
	netplay->config = *config;
	netplay->currentConfig = config->fixture;
	netplay->flow = flow;
	NativeArcadeMenuInput_Reset(&netplay->menuInput);
	netplay->lastScreenSerial = NativeArcadeFlow_ScreenSerial(&netplay->flow);
	netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
	netplay->localSlot = localSlot;
	netplay->initialized = 1u;
	return 1;
}

/* Opens a lobby on the current proposal. A failed open leaves lobbyBegun 0,
 * which reads as WAITING, so the flow's retry pause tries again. */
static void NativeArcadeNetplay_BeginLobby(struct NativeArcadeNetplay *netplay)
{
	netplay->lobbyBegun = (uint8_t)(NativeLobbyState_Begin(&netplay->lobby, netplay->config.localPort,
										netplay->config.candidates, netplay->config.candidateCount,
										&netplay->currentConfig, netplay->config.localRole,
										netplay->config.inputDelay, netplay->config.attemptTicksPerCandidate,
										netplay->config.retransmitIntervalTicks) != 0);
}

static void NativeArcadeNetplay_CloseLobby(struct NativeArcadeNetplay *netplay)
{
	NativeLobbyState_Close(&netplay->lobby);
	netplay->lobbyBegun = 0u;
	netplay->raceArmed = 0u;
}

/* Restarts the candidate cycle, or closes and begins again when no lobby is
 * open or the restart is refused. While a rematch is blocked nothing is
 * begun, so REMATCH_WAIT keeps reading WAITING and times out to OPPONENT
 * LEFT; while a relink is blocked likewise, so SELECT_RESULT times out to
 * LINK ERROR. */
static void NativeArcadeNetplay_RestartLobby(struct NativeArcadeNetplay *netplay)
{
	if ((netplay->rematchBlocked != 0u) || (netplay->relinkBlocked != 0u))
	{
		NativeArcadeNetplay_CloseLobby(netplay);
		return;
	}
	if ((netplay->lobbyBegun != 0u) && NativeLobbyState_RestartCycle(&netplay->lobby))
	{
		return;
	}
	NativeArcadeNetplay_CloseLobby(netplay);
	NativeArcadeNetplay_BeginLobby(netplay);
}

static uint32_t NativeArcadeNetplay_LobbyStatus(const struct NativeArcadeNetplay *netplay)
{
	if (netplay->lobbyBegun == 0u)
	{
		return NATIVE_ARCADE_FLOW_LOBBY_WAITING;
	}
	switch (NativeLobbyState_Mode(&netplay->lobby))
	{
	case NATIVE_LOBBY_STATE_HANDSHAKING:
		return NATIVE_ARCADE_FLOW_LOBBY_CONNECTING;
	case NATIVE_LOBBY_STATE_READY:
		return NATIVE_ARCADE_FLOW_LOBBY_READY;
	case NATIVE_LOBBY_STATE_REJECTED:
		return NATIVE_ARCADE_FLOW_LOBBY_REJECTED;
	case NATIVE_LOBBY_STATE_PEER_LOST:
		return NATIVE_ARCADE_FLOW_LOBBY_LOST;
	case NATIVE_LOBBY_STATE_WAITING_FOR_PEER:
	default:
		return NATIVE_ARCADE_FLOW_LOBBY_WAITING;
	}
}

/* Ends any select: the session is no longer driven, and the relink block is
 * lifted. The session contents and the last outcome stay for inspection. */
static void NativeArcadeNetplay_ClearSelect(struct NativeArcadeNetplay *netplay)
{
	netplay->selectActive = 0u;
	netplay->relinked = 0u;
	netplay->relinkBlocked = 0u;
}

enum NativeArcadeFlowAction NativeArcadeNetplay_Enter(struct NativeArcadeNetplay *netplay)
{
	enum NativeArcadeFlowAction action;

	if ((netplay == NULL) || (netplay->initialized == 0u))
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}
	action = NativeArcadeFlow_Enter(&netplay->flow);
	if (action == NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY)
	{
		netplay->currentConfig = netplay->config.fixture;
		netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
		netplay->rematchBlocked = 0u;
		NativeArcadeNetplay_ClearSelect(netplay);
		NativeArcadeNetplay_BeginLobby(netplay);
	}
	return action;
}

/*
 * BEGIN_REMATCH (UX-7): both cabinets derive the same seed from the same
 * agreed config, so both propose the same rematch config on a brand-new
 * lobby, link, and session. If the seed or the config cannot be built (a
 * defensive path only: the current config was validated when it was
 * proposed), no lobby is begun and the rematch is blocked: RestartLobby
 * begins nothing while rematchBlocked is set, so the flow reads WAITING
 * until the rematch wait times out to OPPONENT LEFT. The old config, and so
 * the old seed, is never proposed again, and nothing races on a guess.
 */
static void NativeArcadeNetplay_BeginRematch(struct NativeArcadeNetplay *netplay)
{
	struct NativeMatchConfigV1 next;
	uint64_t seed = 0u;
	int built = 0;

	if (NativeArcadeNetplay_DeriveRematchSeed(&netplay->currentConfig, &seed))
	{
		built = NativeLockstepRematch_BuildConfig(&netplay->currentConfig, seed, &next);
	}
	NativeArcadeNetplay_CloseLobby(netplay);
	netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
	NativeArcadeNetplay_ClearSelect(netplay);
	if (!built)
	{
		netplay->rematchBlocked = 1u;
		return;
	}
	netplay->currentConfig = next;
	NativeArcadeNetplay_BeginLobby(netplay);
}

/* Discards every datagram waiting in the open link's aux inbox. */
static void NativeArcadeNetplay_DiscardAux(struct NativeArcadeNetplay *netplay)
{
	struct NativeLockstepPeerLink *link;
	uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0u;

	if (netplay->lobbyBegun == 0u)
	{
		return;
	}
	link = NativeLobbyState_Link(&netplay->lobby);
	while (NativeLockstepPeerLink_TakeAux(link, bytes, sizeof(bytes), &size))
	{
		/* Discarded unread. */
	}
}

/* A table value, or the table's first entry when value is not one. */
static uint8_t NativeArcadeNetplay_CharacterOrFirst(uint32_t value)
{
	uint32_t index = 0u;

	if ((value <= 0xffu) && NativeMatchSelect_CharacterIndex((uint8_t)value, &index))
	{
		return (uint8_t)value;
	}
	return NativeMatchSelect_CharacterAt(0u);
}

static uint8_t NativeArcadeNetplay_TrackOrFirst(uint32_t value)
{
	uint32_t index = 0u;

	if ((value <= 0xffu) && NativeMatchSelect_TrackIndex((uint8_t)value, &index))
	{
		return (uint8_t)value;
	}
	return NativeMatchSelect_TrackAt(0u);
}

static uint8_t NativeArcadeNetplay_LapsOrFirst(uint32_t value)
{
	uint32_t index = 0u;

	if ((value <= 0xffu) && NativeMatchSelect_LapOptionIndex((uint8_t)value, &index))
	{
		return (uint8_t)value;
	}
	return NativeMatchSelect_LapOptionAt(0u);
}

/*
 * BEGIN_SELECT (docs/MATCH_SELECT_MILESTONE.md section 2.6): a fresh select
 * on the agreed lobby config. Stale aux datagrams are discarded first (the
 * peer resends its full state every tick). humanCount is the base's number
 * of human-role slots, which is exactly the count BuildConfig accepts for
 * this base; the session's own Init rejects a count or local human it
 * cannot serve, and then selectActive stays 0 and the flow reads FAILED.
 */
static void NativeArcadeNetplay_BeginSelect(struct NativeArcadeNetplay *netplay)
{
	const struct NativeMatchConfigV1 *base = &netplay->currentConfig;
	uint64_t nonce = 0u;
	uint32_t humanCount = 0u;
	uint32_t slot;
	uint8_t localSlot = 0u;
	uint8_t character = NativeMatchSelect_CharacterAt(0u);

	NativeArcadeNetplay_DiscardAux(netplay);
	NativeArcadeNetplay_ClearSelect(netplay);
	netplay->outcomeValid = 0u;
	netplay->selectSerial += 1u;

	for (slot = 0u; slot < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; slot++)
	{
		if ((base->slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) ||
			(base->slots[slot].role == (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
		{
			humanCount += 1u;
		}
	}
	if (NativeMatchConfigV1_FindRoleSlot(base, netplay->config.localRole, &localSlot))
	{
		character = NativeArcadeNetplay_CharacterOrFirst(base->slots[localSlot].characterID);
	}
	(void)NativeArcadeNetplay_DeriveSelectNonce(netplay->config.selectEntropy, netplay->config.localRole,
		netplay->selectSerial, &nonce);

	netplay->selectActive = (uint8_t)(NativeMatchSelectSession_Init(&netplay->select, base, humanCount,
											(uint32_t)netplay->config.localRole - 1u, nonce, character,
											NativeArcadeNetplay_TrackOrFirst(base->trackID),
											NativeArcadeNetplay_LapsOrFirst(base->lapCount),
											&netplay->config.selectTimings) != 0);
}

/*
 * RELINK: the old link is closed in every case, so a READY from it can never
 * start a race. On success the resolved config becomes the current config
 * and a new lobby is begun on it; the relink handshake re-checks it byte for
 * byte. On failure nothing is begun and the relink is blocked, so the flow
 * reads WAITING until its launch timeout shows LINK ERROR: it never races on
 * the base config.
 */
static void NativeArcadeNetplay_Relink(struct NativeArcadeNetplay *netplay)
{
	const struct NativeMatchSelectOutcome *outcome = NULL;
	struct NativeMatchConfigV1 resolved;
	int built = 0;

	if (netplay->selectActive != 0u)
	{
		outcome = NativeMatchSelectSession_Outcome(&netplay->select);
	}
	if (outcome != NULL)
	{
		built = NativeMatchSelect_BuildConfig(&netplay->currentConfig, outcome, &resolved);
	}
	NativeArcadeNetplay_CloseLobby(netplay);
	netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
	netplay->relinked = 1u;
	if (!built)
	{
		netplay->relinkBlocked = 1u;
		return;
	}
	netplay->lastOutcome = *outcome;
	netplay->outcomeValid = 1u;
	netplay->currentConfig = resolved;
	NativeArcadeNetplay_BeginLobby(netplay);
}

/* 1 while the select session is driven: SELECT, or SELECT_RESULT before
 * RELINK has run. */
static int NativeArcadeNetplay_Selecting(const struct NativeArcadeNetplay *netplay)
{
	uint32_t screen = NativeArcadeFlow_Screen(&netplay->flow);

	return ((screen == NATIVE_ARCADE_FLOW_SCREEN_SELECT) || (screen == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT)) &&
		(netplay->relinked == 0u);
}

/* Before the flow runs: take every aux datagram into the session, route the
 * menu event on SELECT (BACK is ignored by the session, SEL-7), tick the
 * session, and return its status in the flow's enum. A session that never
 * started reads FAILED. */
static uint8_t NativeArcadeNetplay_DriveSelect(struct NativeArcadeNetplay *netplay, enum NativeArcadeMenuEvent event)
{
	struct NativeLockstepPeerLink *link;
	uint8_t bytes[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0u;
	enum NativeMatchSelectInput input = NATIVE_MATCH_SELECT_INPUT_NONE;
	uint32_t status;

	if (netplay->selectActive == 0u)
	{
		return (uint8_t)NATIVE_ARCADE_FLOW_SELECT_FAILED;
	}

	if (netplay->lobbyBegun != 0u)
	{
		link = NativeLobbyState_Link(&netplay->lobby);
		while (NativeLockstepPeerLink_TakeAux(link, bytes, sizeof(bytes), &size))
		{
			(void)NativeMatchSelectSession_Accept(&netplay->select, bytes, size);
		}
	}

	if (NativeArcadeFlow_Screen(&netplay->flow) == NATIVE_ARCADE_FLOW_SCREEN_SELECT)
	{
		switch (event)
		{
		case NATIVE_ARCADE_MENU_EVENT_PREV:
			input = NATIVE_MATCH_SELECT_INPUT_PREV;
			break;
		case NATIVE_ARCADE_MENU_EVENT_NEXT:
			input = NATIVE_MATCH_SELECT_INPUT_NEXT;
			break;
		case NATIVE_ARCADE_MENU_EVENT_CONFIRM:
			input = NATIVE_MATCH_SELECT_INPUT_CONFIRM;
			break;
		case NATIVE_ARCADE_MENU_EVENT_BACK:
			input = NATIVE_MATCH_SELECT_INPUT_BACK;
			break;
		case NATIVE_ARCADE_MENU_EVENT_NONE:
		default:
			input = NATIVE_MATCH_SELECT_INPUT_NONE;
			break;
		}
		(void)NativeMatchSelectSession_ApplyInput(&netplay->select, input);
	}

	NativeMatchSelectSession_Tick(&netplay->select);

	status = NativeMatchSelectSession_Status(&netplay->select);
	if (status == NATIVE_MATCH_SELECT_STATUS_CONFIRMED)
	{
		return (uint8_t)NATIVE_ARCADE_FLOW_SELECT_CONFIRMED;
	}
	if (status == NATIVE_MATCH_SELECT_STATUS_FAILED)
	{
		return (uint8_t)NATIVE_ARCADE_FLOW_SELECT_FAILED;
	}
	return (uint8_t)NATIVE_ARCADE_FLOW_SELECT_PENDING;
}

/* After the flow's action: one select record per tick on the aux route
 * while still selecting (this keeps the linger going through the result
 * hold). A failed compose or send is simply lossy. */
static void NativeArcadeNetplay_SendSelect(struct NativeArcadeNetplay *netplay)
{
	uint8_t bytes[NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES];
	size_t size = 0u;

	if ((netplay->selectActive == 0u) || (netplay->lobbyBegun == 0u))
	{
		return;
	}
	if (NativeMatchSelectSession_Compose(&netplay->select, bytes, sizeof(bytes), &size))
	{
		(void)NativeLockstepPeerLink_SendAux(NativeLobbyState_Link(&netplay->lobby), bytes, size);
	}
}

/* Applies a latched outcome, if any: drops the remote human in the roster
 * and records the flow's link-failure reason for its next observation.
 * Shared by OnTakeResult and Tick's own PEER_LOST read. */
static void NativeArcadeNetplay_ApplyLatchedOutcome(struct NativeArcadeNetplay *netplay)
{
	const struct NativeLockstepMatchOutcomeReport *report;

	report = NativeLockstepMatchOutcome_FirstOutcome(&netplay->outcome);
	if (report != NULL)
	{
		(void)NativeLockstepMatchRoster_ApplyOutcome(&netplay->roster, netplay->localSlot, report);
		netplay->pendingLinkFailure = NativeArcadeNetplay_EndReasonForCause(report->cause);
	}
}

/* START_RACE: a fresh outcome tracker and roster for this match. */
static void NativeArcadeNetplay_ArmRace(struct NativeArcadeNetplay *netplay)
{
	(void)NativeLockstepMatchOutcome_Init(&netplay->outcome, netplay->config.stallTimeoutTicks);
	(void)NativeLockstepMatchRoster_Init(&netplay->roster, &netplay->currentConfig);
	netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
	netplay->raceArmed = 1u;
	netplay->matchCount += 1u;
}

enum NativeArcadeFlowAction NativeArcadeNetplay_Tick(struct NativeArcadeNetplay *netplay, uint32_t heldMenuButtons,
	uint8_t raceFinished)
{
	struct NativeArcadeFlowObservation observation;
	enum NativeArcadeMenuEvent event;
	enum NativeArcadeFlowAction action;
	uint32_t serial;

	/* 1. Dormant: nothing is polled or changed. */
	if ((netplay == NULL) || (netplay->initialized == 0u) ||
		(NativeArcadeFlow_Screen(&netplay->flow) == NATIVE_ARCADE_FLOW_SCREEN_OFF))
	{
		return NATIVE_ARCADE_FLOW_ACTION_NONE;
	}

	/* 2. Service the lobby. */
	if (netplay->lobbyBegun != 0u)
	{
		NativeLobbyState_Poll(&netplay->lobby);
	}

	/* 2b. That poll drains bundles into the session. If it found the link
	 * FAULTED or DIVERGED while racing (lobby PEER_LOST), read the cause from
	 * the session now; the outcome tracker checks DIVERGED and FAULTED before
	 * any stall logic, so the flow shows DESYNC or LINK ERROR from the real
	 * cause. */
	if ((netplay->lobbyBegun != 0u) && (netplay->raceArmed != 0u) &&
		(netplay->pendingLinkFailure == NATIVE_ARCADE_FLOW_END_NONE) &&
		(NativeArcadeFlow_Screen(&netplay->flow) == NATIVE_ARCADE_FLOW_SCREEN_RACING) &&
		(NativeLobbyState_Mode(&netplay->lobby) == NATIVE_LOBBY_STATE_PEER_LOST))
	{
		(void)NativeLockstepMatchOutcome_Poll(&netplay->outcome,
			NativeLockstepPeerLink_Session(NativeLobbyState_Link(&netplay->lobby)), NATIVE_LOCKSTEP_SESSION_OK, 0u);
		NativeArcadeNetplay_ApplyLatchedOutcome(netplay);
	}

	/* 3. Release-to-arm on every screen entry (UX-3). */
	serial = NativeArcadeFlow_ScreenSerial(&netplay->flow);
	if (serial != netplay->lastScreenSerial)
	{
		NativeArcadeMenuInput_Reset(&netplay->menuInput);
		netplay->lastScreenSerial = serial;
	}

	/* 4. At most one menu event. */
	event = NativeArcadeMenuInput_Update(&netplay->menuInput, heldMenuButtons);

	/* 5. Observation. */
	memset(&observation, 0, sizeof(observation));
	observation.lobbyStatus = NativeArcadeNetplay_LobbyStatus(netplay);
	observation.linkFailure = netplay->pendingLinkFailure;
	observation.raceFinished = (uint8_t)((raceFinished != 0u) ? 1u : 0u);
	observation.selectStatus = (uint8_t)NATIVE_ARCADE_FLOW_SELECT_PENDING;

	/* 5b. Select phase: aux inbox into the session, the menu event on
	 * SELECT, the session tick, and its status for the flow. */
	if (NativeArcadeNetplay_Selecting(netplay))
	{
		observation.selectStatus = NativeArcadeNetplay_DriveSelect(netplay, event);
	}

	/* 6. Run the flow. */
	action = NativeArcadeFlow_Tick(&netplay->flow, &observation, event);

	/* 7. Execute the host-side part of the action. */
	switch (action)
	{
	case NATIVE_ARCADE_FLOW_ACTION_BEGIN_SELECT:
		NativeArcadeNetplay_BeginSelect(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_RELINK:
		NativeArcadeNetplay_Relink(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_RESTART_LOBBY:
		NativeArcadeNetplay_RestartLobby(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK:
		NativeArcadeNetplay_CloseLobby(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH:
		NativeArcadeNetplay_BeginRematch(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_START_RACE:
		NativeArcadeNetplay_ArmRace(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE:
		NativeArcadeNetplay_CloseLobby(netplay);
		netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
		netplay->rematchBlocked = 0u;
		NativeArcadeNetplay_ClearSelect(netplay);
		break;
	case NATIVE_ARCADE_FLOW_ACTION_NONE:
	case NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY:
	default:
		break;
	}

	/* 8. Still selecting after the action (BEGIN_SELECT included, RELINK
	 * excluded): send this tick's select record. */
	if (NativeArcadeNetplay_Selecting(netplay))
	{
		NativeArcadeNetplay_SendSelect(netplay);
	}

	/* 9. START_RACE and RETURN_TO_TITLE are the caller's cue. */
	return action;
}

void NativeArcadeNetplay_OnTakeResult(struct NativeArcadeNetplay *netplay, enum NativeLockstepSessionResult result,
	uint32_t frameIndex)
{
	struct NativeLockstepSession *session;

	if ((netplay == NULL) || (netplay->initialized == 0u) || (netplay->raceArmed == 0u) ||
		(NativeArcadeFlow_Screen(&netplay->flow) != NATIVE_ARCADE_FLOW_SCREEN_RACING) ||
		(netplay->pendingLinkFailure != NATIVE_ARCADE_FLOW_END_NONE))
	{
		return;
	}

	/* May be NULL when no link is open; the outcome tracker treats a NULL
	 * session as a no-op. */
	session = NativeLockstepPeerLink_Session(NativeLobbyState_Link(&netplay->lobby));
	(void)NativeLockstepMatchOutcome_Poll(&netplay->outcome, session, result, frameIndex);
	/* The flow picks this up on the next Tick. */
	NativeArcadeNetplay_ApplyLatchedOutcome(netplay);
}

int NativeArcadeNetplay_GetView(const struct NativeArcadeNetplay *netplay, struct NativeArcadeNetplayView *view)
{
	uint32_t screen;

	if ((netplay == NULL) || (view == NULL))
	{
		return 0;
	}
	memset(view, 0, sizeof(*view));
	screen = NativeArcadeFlow_Screen(&netplay->flow);
	view->screen = screen;
	view->lobbyStatus = (screen == NATIVE_ARCADE_FLOW_SCREEN_OFF) ? (uint32_t)NATIVE_ARCADE_FLOW_LOBBY_WAITING
																  : NativeArcadeFlow_LobbyStatus(&netplay->flow);
	view->endReason = NativeArcadeFlow_EndReason(&netplay->flow);
	view->selectedRow = NativeArcadeFlow_SelectedRow(&netplay->flow);
	view->ticksInScreen = NativeArcadeFlow_TicksInScreen(&netplay->flow);
	view->matchCount = netplay->matchCount;
	view->localRole = netplay->config.localRole;
	view->menuArmed = (uint8_t)((NativeArcadeMenuInput_IsArmed(&netplay->menuInput) != 0) ? 1u : 0u);
	return 1;
}

/*
 * The current proposal is the agreed config: the handshake only reaches
 * COMPLETE when both proposals are byte-identical (docs/LOBBY_MILESTONE.md
 * section 2.2; validate-and-reject, not negotiation), and the flow only
 * reaches RACING on READY of the relink, whose proposal is the resolved
 * config. MATCH_FOUND and the select screens hold only the select base.
 */
const struct NativeMatchConfigV1 *NativeArcadeNetplay_AgreedConfig(const struct NativeArcadeNetplay *netplay)
{
	uint32_t screen;

	if ((netplay == NULL) || (netplay->initialized == 0u))
	{
		return NULL;
	}
	screen = NativeArcadeFlow_Screen(&netplay->flow);
	if ((screen == NATIVE_ARCADE_FLOW_SCREEN_RACING) || (screen == NATIVE_ARCADE_FLOW_SCREEN_RESULTS))
	{
		return &netplay->currentConfig;
	}
	return NULL;
}

const struct NativeMatchSelectSession *NativeArcadeNetplay_Select(const struct NativeArcadeNetplay *netplay)
{
	uint32_t screen;

	if ((netplay == NULL) || (netplay->initialized == 0u) || (netplay->selectActive == 0u))
	{
		return NULL;
	}
	screen = NativeArcadeFlow_Screen(&netplay->flow);
	if ((screen == NATIVE_ARCADE_FLOW_SCREEN_SELECT) || (screen == NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT))
	{
		return &netplay->select;
	}
	return NULL;
}

struct NativeLockstepPeerLink *NativeArcadeNetplay_Link(struct NativeArcadeNetplay *netplay)
{
	if ((netplay == NULL) || (netplay->initialized == 0u) || (netplay->lobbyBegun == 0u))
	{
		return NULL;
	}
	return NativeLobbyState_Link(&netplay->lobby);
}

uint32_t NativeArcadeNetplay_EndReasonForCause(uint32_t outcomeCause)
{
	switch (outcomeCause)
	{
	case NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT:
		return NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT;
	case NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED:
		return NATIVE_ARCADE_FLOW_END_DESYNC;
	case NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED:
		return NATIVE_ARCADE_FLOW_END_LINK_ERROR;
	default:
		return NATIVE_ARCADE_FLOW_END_NONE;
	}
}

int NativeArcadeNetplay_DeriveRematchSeed(const struct NativeMatchConfigV1 *previous, uint64_t *seedOut)
{
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint32_t word;
	uint32_t byteIndex;
	uint64_t candidate;

	if ((previous == NULL) || (seedOut == NULL))
	{
		return 0;
	}
	if (!NativeMatchConfigV1_Digest(previous, digest))
	{
		return 0;
	}
	for (word = 0u; word < (NATIVE_SHA256_DIGEST_BYTES / 8u); word++)
	{
		candidate = 0u;
		for (byteIndex = 0u; byteIndex < 8u; byteIndex++)
		{
			candidate |= (uint64_t)digest[(word * 8u) + byteIndex] << (8u * byteIndex);
		}
		if ((candidate != 0u) && (candidate != previous->masterSeed))
		{
			*seedOut = candidate;
			return 1;
		}
	}
	return 0;
}

int NativeArcadeNetplay_DeriveSelectNonce(uint64_t entropy, uint8_t localRole, uint32_t selectSerial, uint64_t *nonceOut)
{
	struct NativeSha256 sha;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t input[8u + 1u + 4u];
	uint32_t byteIndex;
	uint64_t nonce = 0u;

	if (nonceOut == NULL)
	{
		return 0;
	}
	for (byteIndex = 0u; byteIndex < 8u; byteIndex++)
	{
		input[byteIndex] = (uint8_t)(entropy >> (8u * byteIndex));
	}
	input[8] = localRole;
	for (byteIndex = 0u; byteIndex < 4u; byteIndex++)
	{
		input[9u + byteIndex] = (uint8_t)(selectSerial >> (8u * byteIndex));
	}

	NativeSha256_Init(&sha);
	NativeSha256_Update(&sha, k_selectNonceTag, sizeof(k_selectNonceTag) - 1u);
	NativeSha256_Update(&sha, input, sizeof(input));
	NativeSha256_Final(&sha, digest);

	for (byteIndex = 0u; byteIndex < 8u; byteIndex++)
	{
		nonce |= (uint64_t)digest[byteIndex] << (8u * byteIndex);
	}
	*nonceOut = nonce;
	return 1;
}

void NativeArcadeNetplay_Shutdown(struct NativeArcadeNetplay *netplay)
{
	if (netplay == NULL)
	{
		return;
	}
	/* A zeroed (never initialized) struct has no lobby to close. */
	if (netplay->initialized != 0u)
	{
		NativeArcadeNetplay_CloseLobby(netplay);
		(void)NativeArcadeFlow_Init(&netplay->flow, &netplay->config.timings);
		NativeArcadeMenuInput_Reset(&netplay->menuInput);
		netplay->lastScreenSerial = NativeArcadeFlow_ScreenSerial(&netplay->flow);
	}
	netplay->lobbyBegun = 0u;
	netplay->raceArmed = 0u;
	netplay->rematchBlocked = 0u;
	NativeArcadeNetplay_ClearSelect(netplay);
	netplay->pendingLinkFailure = NATIVE_ARCADE_FLOW_END_NONE;
}
