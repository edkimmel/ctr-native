#ifndef PLATFORM_NATIVE_MATCH_SELECT_SESSION_H
#define PLATFORM_NATIVE_MATCH_SELECT_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "platform/native_match_config.h"
#include "platform/native_match_select_message.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

/*
 * Match-select session (docs/MATCH_SELECT_MILESTONE.md section 2.3, MS-4):
 * one cabinet's selection state machine. It holds the agreed base config, the
 * local human's cursors, locks, current item, and per-item countdown, the
 * latest accepted select message from every peer, and a latch-once status.
 *
 * Replication, not a protocol: Compose encodes the local human's full state
 * (NativeMatchSelectMessageV1) and the caller sends it to every peer every
 * tick; Accept takes one received record. A lost record only delays; a lock
 * arrives with any later record. Once every human has locked all three items,
 * each cabinet resolves the outcome locally with NativeMatchSelect_Resolve
 * and publishes the first 8 bytes of its outcome digest (resolvedDigest); a
 * cabinet is CONFIRMED once every peer is RESOLVED with an equal digest.
 *
 * Pure: the session is caller-owned and fixed-size; there is no dynamic
 * memory, no I/O, no wall time (time advances only through Tick), and no
 * transport. Every entry point is NULL-tolerant.
 *
 * Status (enum NativeMatchSelectStatus):
 * - PICKING: the local human is still picking.
 * - WAITING: the local human is done, some peer has not locked everything.
 * - RESOLVED: every human locked, resolved locally, awaiting every peer's
 *   RESOLVED record.
 * - CONFIRMED: every peer is RESOLVED with a resolvedDigest equal to ours.
 * - FAILED: see enum NativeMatchSelectSessionFault.
 * CONFIRMED and FAILED are terminal and latch once: nothing changes them.
 *
 * The resolve/confirm step (run after a lock, at the end of Tick, and after a
 * stored Accept): if not resolved, the local human is DONE, and every peer is
 * seen with lockMask 7, the humans' choices (indexed by human; each peer's
 * locked values and nonce) are resolved and digested. Resolution failure
 * latches FAILED/RESOLVE_FAILED; otherwise the session is resolved (status
 * RESOLVED). While resolved, a peer that is RESOLVED with a different
 * resolvedDigest latches FAILED/DIGEST_MISMATCH, and every peer RESOLVED with
 * an equal digest (vacuously true for one human) latches CONFIRMED. While not
 * resolved, the status is PICKING until the local human is DONE, then WAITING.
 */

#define NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS         600u /* OD-1: 20 s at 30 Hz */
#define NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS 90u  /* SEL-9 */

/* Per-item countdowns and the peer silence limit, in Tick calls. Each is >= 1. */
struct NativeMatchSelectTimings
{
	uint32_t characterTicks;
	uint32_t trackTicks;
	uint32_t lapTicks;
	uint32_t peerSilenceTicks;
};

/* Every item countdown DEFAULT_ITEM_TICKS, silence DEFAULT_PEER_SILENCE_TICKS. */
void NativeMatchSelectSession_DefaultTimings(struct NativeMatchSelectTimings *timings);

enum NativeMatchSelectStatus
{
	NATIVE_MATCH_SELECT_STATUS_PICKING = 0,
	NATIVE_MATCH_SELECT_STATUS_WAITING = 1,
	NATIVE_MATCH_SELECT_STATUS_RESOLVED = 2,
	NATIVE_MATCH_SELECT_STATUS_CONFIRMED = 3,
	NATIVE_MATCH_SELECT_STATUS_FAILED = 4
};

/*
 * Why a session FAILED. The enum is append-only: new causes may only be
 * appended at the end and an existing numeric value must never change.
 */
enum NativeMatchSelectSessionFault
{
	NATIVE_MATCH_SELECT_SESSION_FAULT_NONE = 0,
	NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT = 1,
	NATIVE_MATCH_SELECT_SESSION_FAULT_HUMAN_COUNT_MISMATCH = 2,
	NATIVE_MATCH_SELECT_SESSION_FAULT_NONCE_CHANGED = 3,
	NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED = 4,
	NATIVE_MATCH_SELECT_SESSION_FAULT_DIGEST_MISMATCH = 5,
	NATIVE_MATCH_SELECT_SESSION_FAULT_RESOLVE_FAILED = 6
};

enum NativeMatchSelectInput
{
	NATIVE_MATCH_SELECT_INPUT_NONE = 0,
	NATIVE_MATCH_SELECT_INPUT_PREV = 1,
	NATIVE_MATCH_SELECT_INPUT_NEXT = 2,
	NATIVE_MATCH_SELECT_INPUT_CONFIRM = 3,
	NATIVE_MATCH_SELECT_INPUT_BACK = 4
};

enum NativeMatchSelectAcceptResult
{
	NATIVE_MATCH_SELECT_ACCEPT_OK = 0,
	NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED = 1,
	NATIVE_MATCH_SELECT_ACCEPT_DROPPED_FOREIGN = 2,
	NATIVE_MATCH_SELECT_ACCEPT_DROPPED_SELF = 3,
	NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE = 4,
	NATIVE_MATCH_SELECT_ACCEPT_IGNORED_TERMINAL = 5,
	NATIVE_MATCH_SELECT_ACCEPT_FAILED = 6,
	NATIVE_MATCH_SELECT_ACCEPT_REJECTED_LOCAL_STATE = 7
};

/* One human's view: the local human's own state, or a peer's last accepted record. */
struct NativeMatchSelectHumanState
{
	uint8_t seen;        /* local: 1; peer: 1 once a message was accepted */
	uint8_t characterID; /* cursor or locked value */
	uint8_t trackID;     /* cursor or locked value (the vote) */
	uint8_t lapCount;    /* cursor or locked value (the vote) */
	uint8_t lockMask;    /* NATIVE_MATCH_SELECT_LOCK_* bits */
	uint8_t currentItem; /* enum NativeMatchSelectItem */
	uint8_t phase;       /* enum NativeMatchSelectPhase */
	uint8_t reserved;
	uint32_t sequence;    /* local: last composed; peer: last accepted */
	uint32_t silentTicks; /* peers: ticks since the last accepted message (from Init if none) */
	uint64_t nonce;
	uint8_t resolvedDigest[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES];
};

/* Caller-owned and fixed-size. Treat as opaque; use the accessors. */
struct NativeMatchSelectSession
{
	struct NativeMatchConfigV1 base;
	uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES]; /* NativeMatchConfigV1_Digest(&base) */
	struct NativeMatchSelectTimings timings;
	uint32_t humanCount;
	uint32_t localHuman;
	uint32_t status;    /* enum NativeMatchSelectStatus */
	uint32_t fault;     /* enum NativeMatchSelectSessionFault */
	uint32_t itemTicks; /* Tick calls spent on the local current item */
	struct NativeMatchSelectHumanState humans[NATIVE_MATCH_SELECT_MAX_HUMANS];
	struct NativeMatchSelectOutcome outcome; /* valid once resolved */
	uint8_t resolved;
	uint8_t initialized;
	uint8_t reserved[2];
	uint8_t resolvedDigest[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES];
	uint32_t droppedMalformed;
	uint32_t droppedForeign;
	uint32_t droppedSelf;
	uint32_t droppedStale;
};

/*
 * Starts a select. Fails (returns 0, *session untouched) on a NULL session or
 * base, a base failing NativeMatchConfigV1_Validate, humanCount outside 1..4,
 * localHuman >= humanCount, an initial character, track, or lap count outside
 * the rules tables, or any timing of 0. timings NULL means the defaults.
 * The local human starts seen, on the CHARACTER item, with the initial values
 * as cursors, no locks, phase PICKING, sequence 0, and the given nonce; peers
 * start unseen and zeroed. Status PICKING, fault NONE.
 */
int NativeMatchSelectSession_Init(struct NativeMatchSelectSession *session, const struct NativeMatchConfigV1 *base, uint32_t humanCount, uint32_t localHuman,
                                  uint64_t nonce, uint8_t initialCharacter, uint8_t initialTrack, uint8_t initialLaps,
                                  const struct NativeMatchSelectTimings *timings);

/*
 * One local menu event. Returns 1 if the state changed, else 0. Ignored (0)
 * when uninitialized, terminal, or once the local human is DONE.
 * - PREV/NEXT: the current item's cursor steps backward/forward through its
 *   table (rules module order) with wrap.
 * - CONFIRM: locks the current item's cursor and advances to the next item
 *   (the countdown restarts), then runs the resolve/confirm step. Refused (0)
 *   on the CHARACTER item when a peer has locked the cursor's character
 *   (NativeMatchSelectSession_CharacterLockedByPeer; OD-2, the
 *   MM_Characters_boolIsInvalid rule).
 * - BACK (SEL-7) and NONE: no-op.
 */
int NativeMatchSelectSession_ApplyInput(struct NativeMatchSelectSession *session, enum NativeMatchSelectInput input);

/*
 * One select tick. No-op when uninitialized or terminal.
 * 1. While the local human is not DONE: the countdown advances; when it
 *    reaches the current item's timing the item auto-locks on its cursor
 *    (SEL-6). On the CHARACTER item, if a peer has locked the cursor's
 *    character, the pick is the first character in table order after the
 *    cursor (wrapping) that no peer has locked. The item advances and the
 *    countdown restarts.
 * 2. Every peer's silentTicks advances (a peer that never sent counts from
 *    Init); any peer at or above peerSilenceTicks latches FAILED/PEER_SILENT.
 * 3. The resolve/confirm step.
 */
void NativeMatchSelectSession_Tick(struct NativeMatchSelectSession *session);

/*
 * Encodes the local state as one NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES
 * record with the next sequence: phase RESOLVED iff resolved, baseDigest the
 * first 8 bytes of the base digest, resolvedDigest zero unless resolved.
 * Allowed in every status but FAILED (CONFIRMED composes: the linger).
 * Returns 1 and *sizeOut, or 0 with the sequence, bytes, and *sizeOut
 * untouched (uninitialized, FAILED, NULL, capacity too small, or the
 * sequence exhausted).
 */
int NativeMatchSelectSession_Compose(struct NativeMatchSelectSession *session, uint8_t *bytes, size_t capacity, size_t *sizeOut);

/*
 * Takes one received record. Checks, in this order:
 * 1. NULL or uninitialized session: REJECTED_LOCAL_STATE.
 * 2. CONFIRMED or FAILED: IGNORED_TERMINAL (nothing changes).
 * 3. NULL bytes or a decode failure: DROPPED_MALFORMED (counted).
 * 4. baseDigest not ours: DROPPED_FOREIGN (counted).
 * 5. humanCount not ours: FAILED/HUMAN_COUNT_MISMATCH.
 * 6. senderHuman == localHuman: DROPPED_SELF (counted).
 * 7. Seen sender, sequence not above its last accepted: DROPPED_STALE
 *    (counted).
 * 8. Seen sender, nonce changed: FAILED/NONCE_CHANGED.
 * 9. An item the sender had locked is unlocked or changed value, or a sender
 *    seen RESOLVED is no longer RESOLVED with the same resolvedDigest:
 *    FAILED/LOCK_CHANGED.
 * Otherwise the record is stored (the sender becomes seen, its silence
 * restarts), the resolve/confirm step runs, and the result is OK, or FAILED
 * if that step latched FAILED.
 */
enum NativeMatchSelectAcceptResult NativeMatchSelectSession_Accept(struct NativeMatchSelectSession *session, const uint8_t *bytes, size_t size);

/* Accessors. NULL or uninitialized: 0 (or NULL). */
uint32_t NativeMatchSelectSession_Status(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_Fault(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_CurrentItem(const struct NativeMatchSelectSession *session);
/* Ticks until the local current item auto-locks; 0 once the local human is DONE. */
uint32_t NativeMatchSelectSession_TicksLeft(const struct NativeMatchSelectSession *session);
/* NULL if human >= humanCount. */
const struct NativeMatchSelectHumanState *NativeMatchSelectSession_Human(const struct NativeMatchSelectSession *session, uint32_t human);
/* 1 if any seen peer has locked the character item on characterID. */
int NativeMatchSelectSession_CharacterLockedByPeer(const struct NativeMatchSelectSession *session, uint8_t characterID);
/* Bit c set for every rules-table character c (below 16) that a seen peer has
 * locked (NativeMatchSelectSession_CharacterLockedByPeer), so a view can grey
 * it out. */
uint16_t NativeMatchSelectSession_PeerLockedCharacterMask(const struct NativeMatchSelectSession *session);
/* The agreed base config the select started on (the config its baseDigest
 * covers); NULL when NULL or uninitialized. */
const struct NativeMatchConfigV1 *NativeMatchSelectSession_Base(const struct NativeMatchSelectSession *session);
/* The humanCount and localHuman given to Init. */
uint32_t NativeMatchSelectSession_HumanCount(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_LocalHuman(const struct NativeMatchSelectSession *session);
/* Non-NULL only in RESOLVED or CONFIRMED. */
const struct NativeMatchSelectOutcome *NativeMatchSelectSession_Outcome(const struct NativeMatchSelectSession *session);
/* NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES bytes; non-NULL only in RESOLVED or CONFIRMED. */
const uint8_t *NativeMatchSelectSession_ResolvedDigest(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_DroppedMalformed(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_DroppedForeign(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_DroppedSelf(const struct NativeMatchSelectSession *session);
uint32_t NativeMatchSelectSession_DroppedStale(const struct NativeMatchSelectSession *session);

#endif
