#include "platform/native_match_select_session.h"

#include "platform/native_match_config.h"
#include "platform/native_match_select_message.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES <= NATIVE_SHA256_DIGEST_BYTES, "The wire baseDigest is a prefix of the base config digest.");
_Static_assert(NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES <= NATIVE_SHA256_DIGEST_BYTES, "The resolvedDigest is a prefix of the outcome digest.");

#define NATIVE_MATCH_SELECT_SESSION_ALL_LOCKED (NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK | NATIVE_MATCH_SELECT_LOCK_LAPS)

static int NativeMatchSelectSession_IsTerminal(const struct NativeMatchSelectSession *session)
{
	return (session->status == NATIVE_MATCH_SELECT_STATUS_CONFIRMED) || (session->status == NATIVE_MATCH_SELECT_STATUS_FAILED);
}

static int NativeMatchSelectSession_IsLive(const struct NativeMatchSelectSession *session)
{
	return (session != NULL) && (session->initialized != 0);
}

/* Latch-once: a terminal session keeps its status and fault. */
static void NativeMatchSelectSession_Fail(struct NativeMatchSelectSession *session, uint32_t fault)
{
	if (NativeMatchSelectSession_IsTerminal(session))
	{
		return;
	}
	session->status = NATIVE_MATCH_SELECT_STATUS_FAILED;
	session->fault = fault;
}

static struct NativeMatchSelectHumanState *NativeMatchSelectSession_Local(struct NativeMatchSelectSession *session)
{
	return &session->humans[session->localHuman];
}

static uint32_t NativeMatchSelectSession_ItemTiming(const struct NativeMatchSelectSession *session, uint32_t item)
{
	switch (item)
	{
	case NATIVE_MATCH_SELECT_ITEM_CHARACTER:
	{
		return session->timings.characterTicks;
	}
	case NATIVE_MATCH_SELECT_ITEM_TRACK:
	{
		return session->timings.trackTicks;
	}
	case NATIVE_MATCH_SELECT_ITEM_LAPS:
	{
		return session->timings.lapTicks;
	}
	default:
	{
		return 0;
	}
	}
}

/* Steps one table value by +1 or -1 with wrap. The value is always a table member here. */
static uint8_t NativeMatchSelectSession_StepCharacter(uint8_t value, int forward)
{
	uint32_t index = 0;

	(void)NativeMatchSelect_CharacterIndex(value, &index);
	index = forward ? ((index + 1u) % NATIVE_MATCH_SELECT_CHARACTER_COUNT)
	                : ((index + NATIVE_MATCH_SELECT_CHARACTER_COUNT - 1u) % NATIVE_MATCH_SELECT_CHARACTER_COUNT);
	return NativeMatchSelect_CharacterAt(index);
}

static uint8_t NativeMatchSelectSession_StepTrack(uint8_t value, int forward)
{
	uint32_t index = 0;

	(void)NativeMatchSelect_TrackIndex(value, &index);
	index = forward ? ((index + 1u) % NATIVE_MATCH_SELECT_TRACK_COUNT) : ((index + NATIVE_MATCH_SELECT_TRACK_COUNT - 1u) % NATIVE_MATCH_SELECT_TRACK_COUNT);
	return NativeMatchSelect_TrackAt(index);
}

static uint8_t NativeMatchSelectSession_StepLaps(uint8_t value, int forward)
{
	uint32_t index = 0;

	(void)NativeMatchSelect_LapOptionIndex(value, &index);
	index = forward ? ((index + 1u) % NATIVE_MATCH_SELECT_LAP_OPTION_COUNT)
	                : ((index + NATIVE_MATCH_SELECT_LAP_OPTION_COUNT - 1u) % NATIVE_MATCH_SELECT_LAP_OPTION_COUNT);
	return NativeMatchSelect_LapOptionAt(index);
}

static int NativeMatchSelectSession_PeerHoldsCharacter(const struct NativeMatchSelectSession *session, uint8_t characterID)
{
	for (uint32_t h = 0; h < session->humanCount; h++)
	{
		const struct NativeMatchSelectHumanState *peer = &session->humans[h];

		if ((h != session->localHuman) && (peer->seen != 0) && ((peer->lockMask & NATIVE_MATCH_SELECT_LOCK_CHARACTER) != 0) &&
		    (peer->characterID == characterID))
		{
			return 1;
		}
	}
	return 0;
}

/* Locks the local current item on its cursor and advances; the countdown restarts. */
static void NativeMatchSelectSession_LockCurrent(struct NativeMatchSelectSession *session)
{
	struct NativeMatchSelectHumanState *local = NativeMatchSelectSession_Local(session);

	local->lockMask = (uint8_t)(local->lockMask | (1u << local->currentItem));
	local->currentItem = (uint8_t)(local->currentItem + 1u);
	session->itemTicks = 0;
}

static void NativeMatchSelectSession_Resolve(struct NativeMatchSelectSession *session)
{
	struct NativeMatchSelectHumanState *local = NativeMatchSelectSession_Local(session);
	struct NativeMatchSelectChoice choices[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	memset(choices, 0, sizeof(choices));
	for (uint32_t h = 0; h < session->humanCount; h++)
	{
		const struct NativeMatchSelectHumanState *human = &session->humans[h];

		choices[h].characterID = human->characterID;
		choices[h].trackID = human->trackID;
		choices[h].lapCount = human->lapCount;
		choices[h].nonce = human->nonce;
	}

	memset(&outcome, 0, sizeof(outcome));
	if (!NativeMatchSelect_Resolve(&session->base, session->humanCount, choices, &outcome) ||
	    !NativeMatchSelect_OutcomeDigest(session->baseDigest, &outcome, digest))
	{
		NativeMatchSelectSession_Fail(session, NATIVE_MATCH_SELECT_SESSION_FAULT_RESOLVE_FAILED);
		return;
	}

	session->outcome = outcome;
	memcpy(session->resolvedDigest, digest, sizeof(session->resolvedDigest));
	session->resolved = 1;
	local->phase = NATIVE_MATCH_SELECT_PHASE_RESOLVED;
	memcpy(local->resolvedDigest, digest, sizeof(local->resolvedDigest));
	session->status = NATIVE_MATCH_SELECT_STATUS_RESOLVED;
}

/* The resolve/confirm step. Callers run it only while not terminal. */
static void NativeMatchSelectSession_Step(struct NativeMatchSelectSession *session)
{
	const struct NativeMatchSelectHumanState *local = NativeMatchSelectSession_Local(session);

	if ((session->resolved == 0) && (local->currentItem == NATIVE_MATCH_SELECT_ITEM_DONE))
	{
		int everyPeerLocked = 1;

		for (uint32_t h = 0; h < session->humanCount; h++)
		{
			const struct NativeMatchSelectHumanState *peer = &session->humans[h];

			if ((h != session->localHuman) && ((peer->seen == 0) || (peer->lockMask != NATIVE_MATCH_SELECT_SESSION_ALL_LOCKED)))
			{
				everyPeerLocked = 0;
			}
		}
		if (everyPeerLocked)
		{
			NativeMatchSelectSession_Resolve(session);
			if (session->status == NATIVE_MATCH_SELECT_STATUS_FAILED)
			{
				return;
			}
		}
	}

	if (session->resolved != 0)
	{
		int everyPeerConfirms = 1;

		for (uint32_t h = 0; h < session->humanCount; h++)
		{
			const struct NativeMatchSelectHumanState *peer = &session->humans[h];

			if (h == session->localHuman)
			{
				continue;
			}
			if (peer->phase != NATIVE_MATCH_SELECT_PHASE_RESOLVED)
			{
				everyPeerConfirms = 0;
				continue;
			}
			if (memcmp(peer->resolvedDigest, session->resolvedDigest, sizeof(session->resolvedDigest)) != 0)
			{
				NativeMatchSelectSession_Fail(session, NATIVE_MATCH_SELECT_SESSION_FAULT_DIGEST_MISMATCH);
				return;
			}
		}
		session->status = everyPeerConfirms ? NATIVE_MATCH_SELECT_STATUS_CONFIRMED : NATIVE_MATCH_SELECT_STATUS_RESOLVED;
		return;
	}

	session->status = (local->currentItem == NATIVE_MATCH_SELECT_ITEM_DONE) ? NATIVE_MATCH_SELECT_STATUS_WAITING : NATIVE_MATCH_SELECT_STATUS_PICKING;
}

void NativeMatchSelectSession_DefaultTimings(struct NativeMatchSelectTimings *timings)
{
	if (timings == NULL)
	{
		return;
	}
	timings->characterTicks = NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS;
	timings->trackTicks = NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS;
	timings->lapTicks = NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS;
	timings->peerSilenceTicks = NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS;
}

int NativeMatchSelectSession_Init(struct NativeMatchSelectSession *session, const struct NativeMatchConfigV1 *base, uint32_t humanCount, uint32_t localHuman,
                                  uint64_t nonce, uint8_t initialCharacter, uint8_t initialTrack, uint8_t initialLaps,
                                  const struct NativeMatchSelectTimings *timings)
{
	struct NativeMatchSelectSession candidate;
	struct NativeMatchSelectTimings chosen;
	struct NativeMatchSelectHumanState *local;
	uint32_t index = 0;

	if ((session == NULL) || (base == NULL) || (humanCount == 0) || (humanCount > NATIVE_MATCH_SELECT_MAX_HUMANS) || (localHuman >= humanCount) ||
	    !NativeMatchSelect_CharacterIndex(initialCharacter, &index) || !NativeMatchSelect_TrackIndex(initialTrack, &index) ||
	    !NativeMatchSelect_LapOptionIndex(initialLaps, &index) || !NativeMatchConfigV1_Validate(base))
	{
		return 0;
	}
	if (timings != NULL)
	{
		chosen = *timings;
	}
	else
	{
		NativeMatchSelectSession_DefaultTimings(&chosen);
	}
	if ((chosen.characterTicks == 0) || (chosen.trackTicks == 0) || (chosen.lapTicks == 0) || (chosen.peerSilenceTicks == 0))
	{
		return 0;
	}

	memset(&candidate, 0, sizeof(candidate));
	candidate.base = *base;
	if (!NativeMatchConfigV1_Digest(base, candidate.baseDigest))
	{
		return 0;
	}
	candidate.timings = chosen;
	candidate.humanCount = humanCount;
	candidate.localHuman = localHuman;
	candidate.status = NATIVE_MATCH_SELECT_STATUS_PICKING;
	candidate.fault = NATIVE_MATCH_SELECT_SESSION_FAULT_NONE;

	local = &candidate.humans[localHuman];
	local->seen = 1;
	local->characterID = initialCharacter;
	local->trackID = initialTrack;
	local->lapCount = initialLaps;
	local->lockMask = 0;
	local->currentItem = NATIVE_MATCH_SELECT_ITEM_CHARACTER;
	local->phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
	local->sequence = 0;
	local->nonce = nonce;

	candidate.initialized = 1;
	*session = candidate;
	return 1;
}

int NativeMatchSelectSession_ApplyInput(struct NativeMatchSelectSession *session, enum NativeMatchSelectInput input)
{
	struct NativeMatchSelectHumanState *local;

	if (!NativeMatchSelectSession_IsLive(session) || NativeMatchSelectSession_IsTerminal(session))
	{
		return 0;
	}
	local = NativeMatchSelectSession_Local(session);
	if (local->currentItem >= NATIVE_MATCH_SELECT_ITEM_DONE)
	{
		return 0;
	}

	switch (input)
	{
	case NATIVE_MATCH_SELECT_INPUT_PREV:
	case NATIVE_MATCH_SELECT_INPUT_NEXT:
	{
		const int forward = (input == NATIVE_MATCH_SELECT_INPUT_NEXT);

		if (local->currentItem == NATIVE_MATCH_SELECT_ITEM_CHARACTER)
		{
			local->characterID = NativeMatchSelectSession_StepCharacter(local->characterID, forward);
		}
		else if (local->currentItem == NATIVE_MATCH_SELECT_ITEM_TRACK)
		{
			local->trackID = NativeMatchSelectSession_StepTrack(local->trackID, forward);
		}
		else
		{
			local->lapCount = NativeMatchSelectSession_StepLaps(local->lapCount, forward);
		}
		return 1;
	}
	case NATIVE_MATCH_SELECT_INPUT_CONFIRM:
	{
		if ((local->currentItem == NATIVE_MATCH_SELECT_ITEM_CHARACTER) && NativeMatchSelectSession_PeerHoldsCharacter(session, local->characterID))
		{
			return 0;
		}
		NativeMatchSelectSession_LockCurrent(session);
		NativeMatchSelectSession_Step(session);
		return 1;
	}
	default:
	{
		return 0;
	}
	}
}

void NativeMatchSelectSession_Tick(struct NativeMatchSelectSession *session)
{
	struct NativeMatchSelectHumanState *local;
	int silent = 0;

	if (!NativeMatchSelectSession_IsLive(session) || NativeMatchSelectSession_IsTerminal(session))
	{
		return;
	}
	local = NativeMatchSelectSession_Local(session);

	if (local->currentItem < NATIVE_MATCH_SELECT_ITEM_DONE)
	{
		session->itemTicks++;
		if (session->itemTicks >= NativeMatchSelectSession_ItemTiming(session, local->currentItem))
		{
			if ((local->currentItem == NATIVE_MATCH_SELECT_ITEM_CHARACTER) && NativeMatchSelectSession_PeerHoldsCharacter(session, local->characterID))
			{
				/* SEL-6: the first character after the cursor, in table order, that no peer holds. */
				uint8_t pick = local->characterID;

				for (uint32_t step = 1; step < NATIVE_MATCH_SELECT_CHARACTER_COUNT; step++)
				{
					pick = NativeMatchSelectSession_StepCharacter(pick, 1);
					if (!NativeMatchSelectSession_PeerHoldsCharacter(session, pick))
					{
						break;
					}
				}
				local->characterID = pick;
			}
			NativeMatchSelectSession_LockCurrent(session);
		}
	}

	for (uint32_t h = 0; h < session->humanCount; h++)
	{
		struct NativeMatchSelectHumanState *peer = &session->humans[h];

		if (h == session->localHuman)
		{
			continue;
		}
		if (peer->silentTicks < UINT32_MAX)
		{
			peer->silentTicks++;
		}
		if (peer->silentTicks >= session->timings.peerSilenceTicks)
		{
			silent = 1;
		}
	}
	if (silent)
	{
		NativeMatchSelectSession_Fail(session, NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
		return;
	}

	NativeMatchSelectSession_Step(session);
}

int NativeMatchSelectSession_Compose(struct NativeMatchSelectSession *session, uint8_t *bytes, size_t capacity, size_t *sizeOut)
{
	struct NativeMatchSelectMessageV1 message;
	struct NativeCodecWriter writer;
	const struct NativeMatchSelectHumanState *local;

	if (!NativeMatchSelectSession_IsLive(session) || (session->status == NATIVE_MATCH_SELECT_STATUS_FAILED) || (bytes == NULL) || (sizeOut == NULL))
	{
		return 0;
	}
	local = NativeMatchSelectSession_Local(session);
	if (local->sequence == UINT32_MAX)
	{
		return 0;
	}

	memset(&message, 0, sizeof(message));
	message.senderHuman = (uint8_t)session->localHuman;
	message.humanCount = (uint8_t)session->humanCount;
	message.phase = (uint8_t)((session->resolved != 0) ? NATIVE_MATCH_SELECT_PHASE_RESOLVED : NATIVE_MATCH_SELECT_PHASE_PICKING);
	message.lockMask = local->lockMask;
	message.sequence = local->sequence + 1u;
	memcpy(message.baseDigest, session->baseDigest, sizeof(message.baseDigest));
	message.nonce = local->nonce;
	message.characterID = local->characterID;
	message.trackID = local->trackID;
	message.lapCount = local->lapCount;
	message.currentItem = local->currentItem;
	if (session->resolved != 0)
	{
		memcpy(message.resolvedDigest, session->resolvedDigest, sizeof(message.resolvedDigest));
	}

	NativeCodecWriter_Init(&writer, bytes, capacity, NULL);
	if (!NativeMatchSelectMessageV1_Encode(&writer, &message))
	{
		return 0;
	}
	session->humans[session->localHuman].sequence = message.sequence;
	*sizeOut = NativeCodecWriter_Size(&writer);
	return 1;
}

/* An item the sender had locked must stay locked on the same value; RESOLVED must stay RESOLVED on the same digest. */
static int NativeMatchSelectSession_KeepsLocks(const struct NativeMatchSelectHumanState *known, const struct NativeMatchSelectMessageV1 *message)
{
	if (((known->lockMask & NATIVE_MATCH_SELECT_LOCK_CHARACTER) != 0) &&
	    (((message->lockMask & NATIVE_MATCH_SELECT_LOCK_CHARACTER) == 0) || (message->characterID != known->characterID)))
	{
		return 0;
	}
	if (((known->lockMask & NATIVE_MATCH_SELECT_LOCK_TRACK) != 0) &&
	    (((message->lockMask & NATIVE_MATCH_SELECT_LOCK_TRACK) == 0) || (message->trackID != known->trackID)))
	{
		return 0;
	}
	if (((known->lockMask & NATIVE_MATCH_SELECT_LOCK_LAPS) != 0) &&
	    (((message->lockMask & NATIVE_MATCH_SELECT_LOCK_LAPS) == 0) || (message->lapCount != known->lapCount)))
	{
		return 0;
	}
	if ((known->phase == NATIVE_MATCH_SELECT_PHASE_RESOLVED) && ((message->phase != NATIVE_MATCH_SELECT_PHASE_RESOLVED) ||
	                                                             (memcmp(message->resolvedDigest, known->resolvedDigest, sizeof(known->resolvedDigest)) != 0)))
	{
		return 0;
	}
	return 1;
}

enum NativeMatchSelectAcceptResult NativeMatchSelectSession_Accept(struct NativeMatchSelectSession *session, const uint8_t *bytes, size_t size)
{
	struct NativeMatchSelectMessageV1 message;
	struct NativeCodecReader reader;
	struct NativeMatchSelectHumanState *sender;

	if (!NativeMatchSelectSession_IsLive(session))
	{
		return NATIVE_MATCH_SELECT_ACCEPT_REJECTED_LOCAL_STATE;
	}
	if (NativeMatchSelectSession_IsTerminal(session))
	{
		return NATIVE_MATCH_SELECT_ACCEPT_IGNORED_TERMINAL;
	}

	memset(&message, 0, sizeof(message));
	NativeCodecReader_Init(&reader, bytes, size);
	if ((bytes == NULL) || !NativeMatchSelectMessageV1_Decode(&reader, &message, NULL))
	{
		if (session->droppedMalformed < UINT32_MAX)
		{
			session->droppedMalformed++;
		}
		return NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED;
	}
	if (memcmp(message.baseDigest, session->baseDigest, sizeof(message.baseDigest)) != 0)
	{
		if (session->droppedForeign < UINT32_MAX)
		{
			session->droppedForeign++;
		}
		return NATIVE_MATCH_SELECT_ACCEPT_DROPPED_FOREIGN;
	}
	if (message.humanCount != session->humanCount)
	{
		NativeMatchSelectSession_Fail(session, NATIVE_MATCH_SELECT_SESSION_FAULT_HUMAN_COUNT_MISMATCH);
		return NATIVE_MATCH_SELECT_ACCEPT_FAILED;
	}
	if (message.senderHuman == session->localHuman)
	{
		if (session->droppedSelf < UINT32_MAX)
		{
			session->droppedSelf++;
		}
		return NATIVE_MATCH_SELECT_ACCEPT_DROPPED_SELF;
	}

	/* The codec guarantees senderHuman < humanCount, which equals ours here. */
	sender = &session->humans[message.senderHuman];
	if ((sender->seen != 0) && (message.sequence <= sender->sequence))
	{
		if (session->droppedStale < UINT32_MAX)
		{
			session->droppedStale++;
		}
		return NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE;
	}
	if ((sender->seen != 0) && (message.nonce != sender->nonce))
	{
		NativeMatchSelectSession_Fail(session, NATIVE_MATCH_SELECT_SESSION_FAULT_NONCE_CHANGED);
		return NATIVE_MATCH_SELECT_ACCEPT_FAILED;
	}
	if (!NativeMatchSelectSession_KeepsLocks(sender, &message))
	{
		NativeMatchSelectSession_Fail(session, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED);
		return NATIVE_MATCH_SELECT_ACCEPT_FAILED;
	}

	sender->seen = 1;
	sender->characterID = message.characterID;
	sender->trackID = message.trackID;
	sender->lapCount = message.lapCount;
	sender->lockMask = message.lockMask;
	sender->currentItem = message.currentItem;
	sender->phase = message.phase;
	sender->sequence = message.sequence;
	sender->silentTicks = 0;
	sender->nonce = message.nonce;
	memcpy(sender->resolvedDigest, message.resolvedDigest, sizeof(sender->resolvedDigest));

	NativeMatchSelectSession_Step(session);
	return (session->status == NATIVE_MATCH_SELECT_STATUS_FAILED) ? NATIVE_MATCH_SELECT_ACCEPT_FAILED : NATIVE_MATCH_SELECT_ACCEPT_OK;
}

uint32_t NativeMatchSelectSession_Status(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->status : 0u;
}

uint32_t NativeMatchSelectSession_Fault(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->fault : 0u;
}

uint32_t NativeMatchSelectSession_CurrentItem(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->humans[session->localHuman].currentItem : 0u;
}

uint32_t NativeMatchSelectSession_TicksLeft(const struct NativeMatchSelectSession *session)
{
	uint32_t item;
	uint32_t timing;

	if (!NativeMatchSelectSession_IsLive(session))
	{
		return 0;
	}
	item = session->humans[session->localHuman].currentItem;
	timing = NativeMatchSelectSession_ItemTiming(session, item);
	return (timing > session->itemTicks) ? (timing - session->itemTicks) : 0u;
}

const struct NativeMatchSelectHumanState *NativeMatchSelectSession_Human(const struct NativeMatchSelectSession *session, uint32_t human)
{
	if (!NativeMatchSelectSession_IsLive(session) || (human >= session->humanCount))
	{
		return NULL;
	}
	return &session->humans[human];
}

int NativeMatchSelectSession_CharacterLockedByPeer(const struct NativeMatchSelectSession *session, uint8_t characterID)
{
	return NativeMatchSelectSession_IsLive(session) ? NativeMatchSelectSession_PeerHoldsCharacter(session, characterID) : 0;
}

uint16_t NativeMatchSelectSession_PeerLockedCharacterMask(const struct NativeMatchSelectSession *session)
{
	uint16_t mask = 0;

	if (!NativeMatchSelectSession_IsLive(session))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_MATCH_SELECT_CHARACTER_COUNT; i++)
	{
		const uint8_t characterID = NativeMatchSelect_CharacterAt(i);

		if ((characterID < 16u) && NativeMatchSelectSession_PeerHoldsCharacter(session, characterID))
		{
			mask = (uint16_t)(mask | (1u << characterID));
		}
	}
	return mask;
}

const struct NativeMatchConfigV1 *NativeMatchSelectSession_Base(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? &session->base : NULL;
}

uint32_t NativeMatchSelectSession_HumanCount(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->humanCount : 0u;
}

uint32_t NativeMatchSelectSession_LocalHuman(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->localHuman : 0u;
}

static int NativeMatchSelectSession_HasOutcome(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) && (session->resolved != 0) &&
	       ((session->status == NATIVE_MATCH_SELECT_STATUS_RESOLVED) || (session->status == NATIVE_MATCH_SELECT_STATUS_CONFIRMED));
}

const struct NativeMatchSelectOutcome *NativeMatchSelectSession_Outcome(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_HasOutcome(session) ? &session->outcome : NULL;
}

const uint8_t *NativeMatchSelectSession_ResolvedDigest(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_HasOutcome(session) ? session->resolvedDigest : NULL;
}

uint32_t NativeMatchSelectSession_DroppedMalformed(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->droppedMalformed : 0u;
}

uint32_t NativeMatchSelectSession_DroppedForeign(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->droppedForeign : 0u;
}

uint32_t NativeMatchSelectSession_DroppedSelf(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->droppedSelf : 0u;
}

uint32_t NativeMatchSelectSession_DroppedStale(const struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_IsLive(session) ? session->droppedStale : 0u;
}
