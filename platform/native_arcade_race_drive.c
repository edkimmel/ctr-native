#include "platform/native_arcade_race_drive.h"

#include "platform/native_canonical_state.h"
#include "platform/native_lockstep_session.h"
#include "platform/native_match_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Linked-race drive core (docs/LOCKSTEP_RACE_MILESTONE.md LR-1). LR-S7: pad
 * normalization. LR-S8: the per-tick drive over a caller-owned session, kept
 * ring, and callbacks (LR-41..LR-48).
 */

/* The drive's phases. IDLE: not begun. RUNNING: the next call is Step.
 * HELD: the next call is Hold. ENDED: every call returns END. */
#define NATIVE_ARCADE_RACE_DRIVE_PHASE_IDLE    0u
#define NATIVE_ARCADE_RACE_DRIVE_PHASE_RUNNING 1u
#define NATIVE_ARCADE_RACE_DRIVE_PHASE_HELD    2u
#define NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED   3u

void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out)
{
	if (out == NULL)
	{
		return;
	}

	memset(out, 0, sizeof(*out));
	out->status = 0u;
	out->id = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL;
	out->buttons[0] = (uint8_t)(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS & 0xffu);
	out->buttons[1] = (uint8_t)(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS >> 8);
	out->analog[0] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[1] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[2] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[3] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->connected = 1u;
}

void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out)
{
	struct NativeCanonicalInputPadV1 pad;

	if ((in == NULL) || (out == NULL))
	{
		return;
	}

	/* LR-40: only the connected byte decides disconnection. */
	if (in->connected == 0u)
	{
		NativeArcadeRaceDrive_NeutralPad(&pad);
	}
	else
	{
		pad = *in;
		pad.connected = 1u;
		pad.status = 0u;
		pad.id =
		    (uint8_t)((in->id == NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG) ? NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG : NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL);
		/* Active-low: setting the bits releases START. */
		pad.buttons[0] = (uint8_t)(pad.buttons[0] | (NATIVE_ARCADE_RACE_DRIVE_START_MASK & 0xffu));
		pad.buttons[1] = (uint8_t)(pad.buttons[1] | (NATIVE_ARCADE_RACE_DRIVE_START_MASK >> 8));
	}

	*out = pad;
}

void NativeArcadeRaceDrive_DisconnectedPad(struct NativeCanonicalInputPadV1 *out)
{
	if (out == NULL)
	{
		return;
	}

	/* LR-43: the RL-10 rehearsal's TWO_CAB bytes for pads 2 and 3. */
	memset(out, 0, sizeof(*out));
	out->status = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_DISCONNECTED_STATUS;
	out->id = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_DISCONNECTED_ID;
	out->buttons[0] = 0xffu;
	out->buttons[1] = 0xffu;
	out->analog[0] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[1] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[2] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[3] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->connected = 0u;
}

void NativeArcadeRaceDrive_Init(struct NativeArcadeRaceDrive *drive)
{
	if (drive == NULL)
	{
		return;
	}

	memset(drive, 0, sizeof(*drive));
	drive->phase = NATIVE_ARCADE_RACE_DRIVE_PHASE_IDLE;
	drive->raceTick = NATIVE_ARCADE_RACE_DRIVE_NO_TICK;
	drive->graceStartTick = NATIVE_ARCADE_RACE_DRIVE_NO_TICK;
	drive->endTick = NATIVE_ARCADE_RACE_DRIVE_NO_TICK;
	drive->endKind = NATIVE_ARCADE_RACE_DRIVE_END_NONE;
	drive->failure = NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE;
}

static int NativeArcadeRaceDrive_KindIsFinish(uint32_t kind)
{
	return (kind == NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE) || (kind == NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE) ||
	       (kind == NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT);
}

/* Ends the drive once; a finish-kind end arms the linger (LR-13). */
static enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_End(struct NativeArcadeRaceDrive *drive, uint32_t kind, uint32_t failure)
{
	if (drive->phase != NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED)
	{
		drive->phase = NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED;
		drive->endKind = kind;
		drive->failure = failure;
		drive->endTick = drive->raceTick;
		drive->lingerTicksLeft = NativeArcadeRaceDrive_KindIsFinish(kind) ? NATIVE_ARCADE_RACE_DRIVE_FINISH_LINGER_TICKS : 0u;
	}
	return NATIVE_ARCADE_RACE_DRIVE_END;
}

static enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Fail(struct NativeArcadeRaceDrive *drive, uint32_t failure)
{
	return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE, failure);
}

static int NativeArcadeRaceDrive_SessionRunning(const struct NativeArcadeRaceDrive *drive)
{
	return NativeLockstepSession_Mode(drive->session) == NATIVE_LOCKSTEP_RUNNING;
}

/*
 * A session that has left RUNNING ends the drive as the outcome, after
 * OnTakeResult(REJECTED) has let the adapter latch the cause (LR-9, LR-45):
 * REJECTED is what every take returns from then on. Nothing more is
 * composed, sent, or taken.
 */
static enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_OutcomeNow(struct NativeArcadeRaceDrive *drive)
{
	(void)drive->callbacks.onTakeResult(drive->callbacks.context, NATIVE_LOCKSTEP_SESSION_REJECTED, drive->raceTick);
	return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
}

/* A failed session call: a local failure while the session is RUNNING, else
 * the outcome (LR-9). */
static enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_FailOrOutcome(struct NativeArcadeRaceDrive *drive, uint32_t failure)
{
	if (!NativeArcadeRaceDrive_SessionRunning(drive))
	{
		return NativeArcadeRaceDrive_OutcomeNow(drive);
	}
	return NativeArcadeRaceDrive_Fail(drive, failure);
}

/*
 * The send-order guard (LR-2, LR-29, LR-3). The bundle for frame f may be
 * composed or sent only while the session is RUNNING, after the first
 * record, and after frame f - D is recorded. Every compose and every send
 * goes through it.
 */
static int NativeArcadeRaceDrive_MaySend(const struct NativeArcadeRaceDrive *drive, uint32_t frameIndex)
{
	const struct NativeLockstepSession *session = drive->session;

	if ((session->mode != NATIVE_LOCKSTEP_RUNNING) || (session->recordedAny == 0u))
	{
		return 0;
	}
	return (uint64_t)frameIndex <= ((uint64_t)session->recordedFrame + (uint64_t)drive->inputDelay);
}

/* Composes frame frameIndex into the kept ring, exactly once. Returns the
 * failure reason, or FAILURE_NONE. */
static uint32_t NativeArcadeRaceDrive_ComposeKept(struct NativeArcadeRaceDrive *drive, uint32_t frameIndex)
{
	struct NativeArcadeRaceDriveKeptBundle *entry;
	size_t size = 0u;

	if (!NativeArcadeRaceDrive_MaySend(drive, frameIndex))
	{
		return NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEND_ORDER;
	}
	entry = &drive->kept->entries[frameIndex % NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY];
	if ((entry->present != 0u) && (entry->frameIndex == frameIndex))
	{
		/* Composed once already: composing again would not be byte-identical
		 * to what the peer may hold. Unreachable. */
		return NATIVE_ARCADE_RACE_DRIVE_FAILURE_COMPOSE;
	}
	entry->present = 0u;
	if (!NativeLockstepSession_ComposeBundle(drive->session, frameIndex, entry->bytes, sizeof(entry->bytes), &size) ||
	    (size != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES))
	{
		return NATIVE_ARCADE_RACE_DRIVE_FAILURE_COMPOSE;
	}
	entry->frameIndex = frameIndex;
	entry->present = 1u;
	drive->composedCount++;
	return NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE;
}

/* Sends the kept bundle of frameIndex verbatim. Returns 1 if sent, 0 if the
 * link refused it, -1 if not sent at all (not kept, or the guard forbids). */
static int NativeArcadeRaceDrive_SendKept(struct NativeArcadeRaceDrive *drive, uint32_t frameIndex)
{
	const struct NativeArcadeRaceDriveKeptBundle *entry = &drive->kept->entries[frameIndex % NATIVE_ARCADE_RACE_DRIVE_KEPT_CAPACITY];

	if (!NativeArcadeRaceDrive_MaySend(drive, frameIndex))
	{
		return -1;
	}
	if ((entry->present == 0u) || (entry->frameIndex != frameIndex))
	{
		return -1;
	}
	return drive->callbacks.sendBundle(drive->callbacks.context, frameIndex, entry->bytes, sizeof(entry->bytes)) != 0 ? 1 : 0;
}

/* The oldest frame of race tick k's resend window, max(0, k - D - 1). */
static uint32_t NativeArcadeRaceDrive_WindowLow(const struct NativeArcadeRaceDrive *drive, uint32_t raceTick)
{
	return (raceTick > drive->inputDelay) ? (raceTick - drive->inputDelay - 1u) : 0u;
}

/* Resends frames low..high inclusive (LR-3). A refused send is not a
 * failure here: the take classifies what the link or session did. */
static void NativeArcadeRaceDrive_Resend(struct NativeArcadeRaceDrive *drive, uint32_t low, uint32_t high)
{
	for (uint64_t frame = low; frame <= (uint64_t)high; frame++)
	{
		(void)NativeArcadeRaceDrive_SendKept(drive, (uint32_t)frame);
	}
}

/* LR-5: the CAB1_HUMAN pad to retail pad 0, CAB2_HUMAN to pad 1, each
 * normalized again (all-zero is neutral), pads 2 and 3 disconnected. */
static int NativeArcadeRaceDrive_MapPads(const struct NativeArcadeRaceDrive *drive, const struct NativeLockstepSessionFrameInputs *inputs,
                                         struct NativeCanonicalInputPadV1 padsOut[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT])
{
	struct NativeCanonicalInputPadV1 pads[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT];
	int found1 = 0;
	int found2 = 0;
	uint32_t count = inputs->padCount;

	if (count > NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY)
	{
		count = NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY;
	}
	for (uint32_t i = 0; i < count; i++)
	{
		const struct NativeLockstepBundlePadV1 *entry = &inputs->pads[i];

		if ((entry->slotIndex == drive->cab1Slot) && !found1)
		{
			NativeArcadeRaceDrive_NormalizePad(&entry->pad, &pads[0]);
			found1 = 1;
		}
		else if ((entry->slotIndex == drive->cab2Slot) && !found2)
		{
			NativeArcadeRaceDrive_NormalizePad(&entry->pad, &pads[1]);
			found2 = 1;
		}
	}
	if (!found1 || !found2)
	{
		return 0;
	}
	NativeArcadeRaceDrive_DisconnectedPad(&pads[2]);
	NativeArcadeRaceDrive_DisconnectedPad(&pads[3]);
	memcpy(padsOut, pads, sizeof(pads));
	return 1;
}

/*
 * Takes frame k and classifies the result (LR-9, LR-45). countStall: a
 * stall calls onTakeResult(STALL), once per full held period outside the
 * start grace.
 */
static enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Take(struct NativeArcadeRaceDrive *drive, int countStall,
                                                                   struct NativeCanonicalInputPadV1 padsOut[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT])
{
	struct NativeLockstepSessionFrameInputs inputs;
	enum NativeLockstepSessionResult result;
	const uint32_t raceTick = drive->raceTick;
	int latched;

	memset(&inputs, 0, sizeof(inputs));
	result = NativeLockstepSession_TakeFrameInputs(drive->session, raceTick, &inputs);
	if (result == NATIVE_LOCKSTEP_SESSION_STALL)
	{
		if (countStall && (drive->callbacks.onTakeResult(drive->callbacks.context, result, raceTick) != 0))
		{
			return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
		}
		drive->phase = NATIVE_ARCADE_RACE_DRIVE_PHASE_HELD;
		return NATIVE_ARCADE_RACE_DRIVE_HOLD;
	}

	/* OnTakeResult first, so the adapter latches the real cause (LR-9). */
	latched = drive->callbacks.onTakeResult(drive->callbacks.context, result, raceTick) != 0;
	if (latched)
	{
		return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	}
	if (result != NATIVE_LOCKSTEP_SESSION_OK)
	{
		if (!NativeArcadeRaceDrive_SessionRunning(drive))
		{
			return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
		}
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_TAKE);
	}
	if ((inputs.frameIndex != raceTick) || !NativeArcadeRaceDrive_MapPads(drive, &inputs, padsOut))
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_PAD);
	}
	drive->phase = NATIVE_ARCADE_RACE_DRIVE_PHASE_RUNNING;
	drive->nextTick = raceTick + 1u;
	return NATIVE_ARCADE_RACE_DRIVE_GO;
}

int NativeArcadeRaceDrive_Begin(struct NativeArcadeRaceDrive *drive, struct NativeLockstepSession *session, struct NativeArcadeRaceDriveKept *kept,
                                const struct NativeArcadeRaceDriveCallbacks *callbacks, uint32_t raceTickLimit)
{
	uint8_t cab1Slot = 0u;
	uint8_t cab2Slot = 0u;

	if (drive == NULL)
	{
		return 0;
	}
	NativeArcadeRaceDrive_Init(drive);

	if ((session == NULL) || (kept == NULL) || (callbacks == NULL) || (callbacks->sendBundle == NULL) || (callbacks->poll == NULL) ||
	    (callbacks->onTakeResult == NULL))
	{
		(void)NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT);
		return 0;
	}
	if (NativeLockstepSession_Mode(session) != NATIVE_LOCKSTEP_RUNNING)
	{
		(void)NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_MODE);
		return 0;
	}
	/* LR-41: one session per race, begun before its first record or take. */
	if ((session->recordedAny != 0u) || (session->consumedFrame != 0u))
	{
		(void)NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_STARTED);
		return 0;
	}
	/* LR-3: the 2D + 1 lead must stay below the peer window, and the resend
	 * window of 2D + 2 frames must fit the kept ring. */
	if ((session->inputDelay < 1u) || (session->inputDelay > NATIVE_ARCADE_RACE_DRIVE_MAX_INPUT_DELAY))
	{
		(void)NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_INPUT_DELAY);
		return 0;
	}
	if (!NativeMatchConfigV1_FindRoleSlot(&session->config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, &cab1Slot) ||
	    !NativeMatchConfigV1_FindRoleSlot(&session->config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, &cab2Slot) || (cab1Slot == cab2Slot))
	{
		(void)NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_SLOT);
		return 0;
	}
	/* LR-42: the internal override may only lower the bound. */
	if (raceTickLimit > NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT)
	{
		(void)NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_TICK_LIMIT);
		return 0;
	}

	memset(kept, 0, sizeof(*kept));
	drive->session = session;
	drive->kept = kept;
	drive->callbacks = *callbacks;
	drive->inputDelay = session->inputDelay;
	drive->raceTickLimit = (raceTickLimit == 0u) ? NATIVE_ARCADE_RACE_DRIVE_RACE_TICK_LIMIT : raceTickLimit;
	drive->cab1Slot = cab1Slot;
	drive->cab2Slot = cab2Slot;
	drive->nextTick = 0u;
	drive->phase = NATIVE_ARCADE_RACE_DRIVE_PHASE_RUNNING;
	return 1;
}

enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Step(struct NativeArcadeRaceDrive *drive, uint32_t raceTick, const struct NativeCanonicalStateV4 *state,
                                                            const struct NativeCanonicalInputPadV1 *localSample, const struct NativeArcadeRaceDriveFacts *facts,
                                                            struct NativeCanonicalInputPadV1 padsOut[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT])
{
	struct NativeCanonicalInputPadV1 sample;
	uint32_t graceThreshold;
	uint32_t firstNew;
	int recorded;

	if (drive == NULL)
	{
		return NATIVE_ARCADE_RACE_DRIVE_END;
	}
	if (drive->phase == NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED)
	{
		return NATIVE_ARCADE_RACE_DRIVE_END;
	}
	if (drive->phase == NATIVE_ARCADE_RACE_DRIVE_PHASE_IDLE)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NOT_BEGUN);
	}
	if (drive->phase != NATIVE_ARCADE_RACE_DRIVE_PHASE_RUNNING)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEQUENCE);
	}
	if ((state == NULL) || (localSample == NULL) || (facts == NULL) || (padsOut == NULL))
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT);
	}
	if (raceTick != drive->nextTick)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_RACE_TICK);
	}
	if (state->frameNumber != raceTick)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_STATE_FRAME);
	}
	if ((facts->humans < 1u) || (facts->humans > NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT) || (facts->finishedHumans > facts->humans))
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_FACTS);
	}
	drive->raceTick = raceTick;

	/* 1. Record frame k. A session that is not RUNNING afterwards (a parked
	 *    digest diverged inside the record, or an earlier drain latched)
	 *    ends the drive as the outcome at once (LR-9). */
	recorded = NativeLockstepSession_RecordLocalDigests(drive->session, state);
	if (!NativeArcadeRaceDrive_SessionRunning(drive))
	{
		return NativeArcadeRaceDrive_OutcomeNow(drive);
	}
	if (!recorded)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_RECORD);
	}

	/* 2. The end checks in LR-18's tie order. A finish-kind end has recorded
	 *    the tick and composes, sends, and takes nothing (LR-13). */
	if (facts->endOfRace != 0u)
	{
		return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	}
	graceThreshold = (facts->humans > 2u) ? (facts->humans - 1u) : 1u;
	if ((drive->graceStartTick == NATIVE_ARCADE_RACE_DRIVE_NO_TICK) && (facts->finishedHumans >= graceThreshold))
	{
		drive->graceStartTick = raceTick;
	}
	if ((drive->graceStartTick != NATIVE_ARCADE_RACE_DRIVE_NO_TICK) &&
	    ((uint64_t)raceTick >= (uint64_t)drive->graceStartTick + NATIVE_ARCADE_RACE_DRIVE_FINISH_GRACE_TICKS))
	{
		return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	}
	if (raceTick >= drive->raceTickLimit)
	{
		return NativeArcadeRaceDrive_End(drive, NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE);
	}

	/* 3. Submit the normalized sample of frame k, consumed at k + D. */
	NativeArcadeRaceDrive_NormalizePad(localSample, &sample);
	if (!NativeLockstepSession_SubmitLocalInput(drive->session, raceTick, &sample))
	{
		return NativeArcadeRaceDrive_FailOrOutcome(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SUBMIT);
	}

	/* 4. Compose and send: frames 0..D on race tick 0, else frame k + D. */
	firstNew = (raceTick == 0u) ? 0u : (raceTick + drive->inputDelay);
	for (uint32_t frame = firstNew; frame <= raceTick + drive->inputDelay; frame++)
	{
		const uint32_t failure = NativeArcadeRaceDrive_ComposeKept(drive, frame);

		if (failure != NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE)
		{
			return NativeArcadeRaceDrive_FailOrOutcome(drive, failure);
		}
		(void)NativeArcadeRaceDrive_SendKept(drive, frame);
	}

	/* 5. Resend the kept window max(0, k - D - 1)..k + D - 1, minus what step
	 *    4 sent (all of it on race tick 0). */
	if (firstNew > 0u)
	{
		NativeArcadeRaceDrive_Resend(drive, NativeArcadeRaceDrive_WindowLow(drive, raceTick), firstNew - 1u);
	}

	/* 6. Poll. 7. Take frame k. */
	drive->callbacks.poll(drive->callbacks.context);
	drive->heldPeriods = 0u;
	return NativeArcadeRaceDrive_Take(drive, 0, padsOut);
}

enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Hold(struct NativeArcadeRaceDrive *drive, int newPeriod,
                                                            struct NativeCanonicalInputPadV1 padsOut[NATIVE_ARCADE_RACE_DRIVE_PAD_COUNT])
{
	int countStall = 0;

	if (drive == NULL)
	{
		return NATIVE_ARCADE_RACE_DRIVE_END;
	}
	if (drive->phase == NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED)
	{
		return NATIVE_ARCADE_RACE_DRIVE_END;
	}
	if (drive->phase == NATIVE_ARCADE_RACE_DRIVE_PHASE_IDLE)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_NOT_BEGUN);
	}
	if (drive->phase != NATIVE_ARCADE_RACE_DRIVE_PHASE_HELD)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEQUENCE);
	}
	if (padsOut == NULL)
	{
		return NativeArcadeRaceDrive_Fail(drive, NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT);
	}

	/* Every iteration drains the link (LR-9). */
	drive->callbacks.poll(drive->callbacks.context);
	if (newPeriod != 0)
	{
		/* Once per full period, never per iteration (LR-44). */
		drive->heldPeriods++;
		NativeArcadeRaceDrive_Resend(drive, NativeArcadeRaceDrive_WindowLow(drive, drive->raceTick), drive->raceTick + drive->inputDelay);
		if (drive->callbacks.servicePeriod != NULL)
		{
			drive->callbacks.servicePeriod(drive->callbacks.context);
		}
		/* The start grace: race tick 0's first 810 periods do not count. */
		countStall = !((drive->raceTick == 0u) && (drive->heldPeriods <= NATIVE_ARCADE_RACE_DRIVE_START_GRACE_PERIODS));
	}
	return NativeArcadeRaceDrive_Take(drive, countStall, padsOut);
}

uint32_t NativeArcadeRaceDrive_LingerTick(struct NativeArcadeRaceDrive *drive, int onResults)
{
	uint32_t sent = 0u;

	if ((drive == NULL) || (drive->phase != NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED) || !NativeArcadeRaceDrive_KindIsFinish(drive->endKind) ||
	    (drive->lingerTicksLeft == 0u))
	{
		return 0u;
	}
	/* LR-46: off RESULTS or a session that left RUNNING stops it for good. */
	if ((onResults == 0) || !NativeArcadeRaceDrive_SessionRunning(drive))
	{
		drive->lingerTicksLeft = 0u;
		return 0u;
	}
	if (drive->endTick + drive->inputDelay > 0u)
	{
		const uint32_t high = drive->endTick + drive->inputDelay - 1u;

		for (uint64_t frame = NativeArcadeRaceDrive_WindowLow(drive, drive->endTick); frame <= (uint64_t)high; frame++)
		{
			const int result = NativeArcadeRaceDrive_SendKept(drive, (uint32_t)frame);

			if (result == 0)
			{
				/* The link refused: it is closed. Stop for good. */
				drive->lingerTicksLeft = 0u;
				return sent;
			}
			if (result > 0)
			{
				sent++;
			}
		}
	}
	drive->lingerTicksLeft--;
	return sent;
}

int NativeArcadeRaceDrive_BannerDue(uint32_t periods)
{
	return periods >= NATIVE_ARCADE_RACE_DRIVE_HOLD_GRACE_PERIODS;
}

enum NativeArcadeRaceDriveEndKind NativeArcadeRaceDrive_EndKind(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? NATIVE_ARCADE_RACE_DRIVE_END_NONE : (enum NativeArcadeRaceDriveEndKind)drive->endKind;
}

enum NativeArcadeRaceDriveFailure NativeArcadeRaceDrive_FailureReason(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE : (enum NativeArcadeRaceDriveFailure)drive->failure;
}

int NativeArcadeRaceDrive_EndIsFinish(const struct NativeArcadeRaceDrive *drive)
{
	return (drive != NULL) && (drive->phase == NATIVE_ARCADE_RACE_DRIVE_PHASE_ENDED) && NativeArcadeRaceDrive_KindIsFinish(drive->endKind);
}

uint32_t NativeArcadeRaceDrive_RaceTick(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? NATIVE_ARCADE_RACE_DRIVE_NO_TICK : drive->raceTick;
}

uint32_t NativeArcadeRaceDrive_EndTick(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? NATIVE_ARCADE_RACE_DRIVE_NO_TICK : drive->endTick;
}

uint32_t NativeArcadeRaceDrive_GraceStartTick(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? NATIVE_ARCADE_RACE_DRIVE_NO_TICK : drive->graceStartTick;
}

uint32_t NativeArcadeRaceDrive_HeldPeriods(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? 0u : drive->heldPeriods;
}

uint32_t NativeArcadeRaceDrive_LingerTicksLeft(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? 0u : drive->lingerTicksLeft;
}

uint32_t NativeArcadeRaceDrive_RaceTickLimit(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? 0u : drive->raceTickLimit;
}

uint32_t NativeArcadeRaceDrive_ComposedCount(const struct NativeArcadeRaceDrive *drive)
{
	return (drive == NULL) ? 0u : drive->composedCount;
}

const char *NativeArcadeRaceDrive_EndKindName(enum NativeArcadeRaceDriveEndKind kind)
{
	switch (kind)
	{
	case NATIVE_ARCADE_RACE_DRIVE_END_NONE:
		return "none";
	case NATIVE_ARCADE_RACE_DRIVE_END_OF_RACE:
		return "end of race";
	case NATIVE_ARCADE_RACE_DRIVE_END_FINISH_GRACE:
		return "finish grace";
	case NATIVE_ARCADE_RACE_DRIVE_END_RACE_TICK_LIMIT:
		return "race tick limit";
	case NATIVE_ARCADE_RACE_DRIVE_END_OUTCOME:
		return "outcome";
	case NATIVE_ARCADE_RACE_DRIVE_END_LOCAL_FAILURE:
		return "local failure";
	default:
		return "unknown";
	}
}

const char *NativeArcadeRaceDrive_FailureName(enum NativeArcadeRaceDriveFailure failure)
{
	switch (failure)
	{
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_NONE:
		return "none";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_ARGUMENT:
		return "argument";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_MODE:
		return "session not running";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_SESSION_STARTED:
		return "session already started";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_INPUT_DELAY:
		return "input delay above 3";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_SLOT:
		return "role slot missing";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_TICK_LIMIT:
		return "race tick limit out of range";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_NOT_BEGUN:
		return "not begun";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEQUENCE:
		return "call sequence";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_RACE_TICK:
		return "race tick out of order";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_STATE_FRAME:
		return "state frame mismatch";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_FACTS:
		return "race facts invalid";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_RECORD:
		return "record";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_SUBMIT:
		return "submit";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_COMPOSE:
		return "compose";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_SEND_ORDER:
		return "send order";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_TAKE:
		return "take rejected";
	case NATIVE_ARCADE_RACE_DRIVE_FAILURE_ROLE_PAD:
		return "role pad missing";
	default:
		return "unknown";
	}
}
