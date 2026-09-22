#include "platform/native_lockstep_session.h"

#include <string.h>

/*
 * Compile-time only: the peer array is indexed by config slot, so raising one
 * slot count without the other must fail to build; every delay Open's range
 * check admits must be carriable by the independently sized peer rings, which
 * is what makes the InputWindow_Init below unable to fail; and the frame input
 * set has to hold the local pad plus every other slot's full bundle pad
 * capacity.  The history depth is not asserted here: it is defined as
 * NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 2, so an assertion against that expression
 * could not fail, and FindDigests and RecordLocalDigests each guard their use
 * of historyCapacity as a modulus at runtime instead.
 */
_Static_assert(NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY == NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT,
               "The lockstep peer array must be indexable by bundle sender slot.");
_Static_assert(NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1 <= NATIVE_LOCKSTEP_RING_CAPACITY,
               "Open's maximum input delay must fit the fixed peer input rings.");
_Static_assert(1u + (NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY - 1u) * NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY <=
                   NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY,
               "The frame input set must hold the local pad plus every peer's pads.");

/*
 * Bot inputs are not on the wire: both peers run the same bots from the shared
 * seed and roster, so only human slots are lockstep peers and bot divergence
 * surfaces as a DRIVERS or RNG domain mismatch instead.
 */
static int NativeLockstepSession_IsHumanSlot(const struct NativeMatchConfigV1 *config, uint32_t slot)
{
	const uint8_t role = config->slots[slot].role;

	return (role == NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) || (role == NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN);
}

/*
 * The history is exactly inputDelay + 2 frames deep, so a frame older than that
 * has been retired and is no longer comparable.  The frame tag is checked as
 * well, so a modular slot that still holds a retired frame's bytes can never be
 * mistaken for the requested frame.
 */
static const struct NativeLockstepSessionDigestRecord *NativeLockstepSession_FindDigests(const struct NativeLockstepSession *session,
                                                                                         uint32_t frameIndex)
{
	const struct NativeLockstepSessionDigestRecord *record;

	/* AcceptBundle is wire facing and its only structural precondition is a
	 * non-IDLE mode, so the modulus is validated here rather than trusted: a
	 * depth Open never wrote can index nothing. */
	if ((session->historyCapacity == 0u) || (session->historyCapacity > NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY))
	{
		return NULL;
	}
	if (session->recordedAny == 0)
	{
		return NULL;
	}
	if ((frameIndex > session->recordedFrame) ||
	    (((uint64_t)session->recordedFrame - (uint64_t)frameIndex) >= (uint64_t)session->historyCapacity))
	{
		return NULL;
	}
	record = &session->localDigests[frameIndex % session->historyCapacity];
	if ((record->present == 0) || (record->frameIndex != frameIndex))
	{
		return NULL;
	}
	return record;
}

/*
 * The latch in the shape of NativeReplaySchedulerV4 Match(): the whole body is
 * guarded, so the first divergence can never be overwritten.  record is NULL
 * for a FRAME_UNAVAILABLE report, where there was nothing to compare.
 */
static void NativeLockstepSession_LatchDivergence(struct NativeLockstepSession *session, uint32_t mask, uint32_t canonicalDomainMask,
                                                  const struct NativeLockstepBundleV1 *bundle,
                                                  const struct NativeLockstepSessionDigestRecord *record)
{
	if (session->mode == NATIVE_LOCKSTEP_DIVERGED)
	{
		return;
	}

	memset(&session->divergence, 0, sizeof(session->divergence));
	session->divergence.mask = mask;
	session->divergence.canonicalDomainMask = canonicalDomainMask;
	/* The frame that actually diverged, not the frame that noticed it. */
	session->divergence.frameIndex = bundle->verifiedFrameIndex;
	session->divergence.senderSlot = bundle->senderSlot;
	session->divergence.remoteCombinedDigest = bundle->verifiedCombinedDigest;
	memcpy(session->divergence.remoteDomainDigests, bundle->verifiedDomainDigests, sizeof(session->divergence.remoteDomainDigests));
	if (record != NULL)
	{
		session->divergence.localCombinedDigest = record->combinedDigest;
		memcpy(session->divergence.localDomainDigests, record->domainDigests, sizeof(session->divergence.localDomainDigests));
	}
	/* DIVERGED outranks FAULTED, so a later protocol fault cannot erase this. */
	session->mode = NATIVE_LOCKSTEP_DIVERGED;
}

/*
 * The second, independent latch.  It is also once-only, and it deliberately
 * refuses to demote a DIVERGED mode: a diagnostic divergence always survives a
 * subsequent protocol fault.
 */
static void NativeLockstepSession_LatchFault(struct NativeLockstepSession *session, uint32_t cause, uint32_t frameIndex,
                                             uint32_t senderSlot, uint32_t detail)
{
	if (session->faulted != 0)
	{
		return;
	}

	memset(&session->fault, 0, sizeof(session->fault));
	session->fault.cause = cause;
	session->fault.frameIndex = frameIndex;
	session->fault.senderSlot = senderSlot;
	session->fault.detail = detail;
	session->faulted = 1u;
	if (session->mode != NATIVE_LOCKSTEP_DIVERGED)
	{
		session->mode = NATIVE_LOCKSTEP_FAULTED;
	}
}

/*
 * Returns 1 when this record's verified digest block disagrees with the local
 * history, whether or not the latch accepted it.  Only a record the peer window
 * ACCEPTED reaches here: a stale, duplicate, or faulted record is not part of
 * the match and is never digest-compared.  A bundle with verifiedPresent 0
 * carries no digest, which is the first inputDelay + 1 frames of a session, and
 * is never a divergence.
 */
static int NativeLockstepSession_Verify(struct NativeLockstepSession *session, const struct NativeLockstepBundleV1 *bundle)
{
	const struct NativeLockstepSessionDigestRecord *record;
	uint32_t canonicalDomainMask = 0;
	uint32_t mask = 0;

	if (bundle->verifiedPresent == 0)
	{
		return 0;
	}

	record = NativeLockstepSession_FindDigests(session, bundle->verifiedFrameIndex);
	if (record == NULL)
	{
		/* Well formed, but the two simulations are no longer comparable.  This
		 * is deliberately a divergence and never a protocol fault: an
		 * incomparable frame was never compared, so suppressing it would hide a
		 * real report on a record the window did accept. */
		NativeLockstepSession_LatchDivergence(session, NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE, 0u, bundle, NULL);
		return 1;
	}
	/* No dedup bookkeeping is needed to keep this comparison at most once per
	 * verifiedFrameIndex: only the caller's ACCEPTED case reaches here, and the
	 * window's occupancy-slot check makes ACCEPTED happen at most once per
	 * frameIndex (platform/native_lockstep_input_window.c:83-97, with the
	 * below-window half at :65-69).  The codec pins
	 * verifiedFrameIndex + inputDelay + 1 == frameIndex on every decode
	 * (platform/native_lockstep_protocol.c:308-312), a bijection between the two,
	 * so at most one ACCEPTED record can ever carry this verifiedFrameIndex
	 * regardless of wire reordering. */

	/* Identical to platform/native_replay_scheduler_v4.c EndFrame: one bit per
	 * differing domain digest, with the combined digest reported separately. */
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		if (record->domainDigests[i] != bundle->verifiedDomainDigests[i])
		{
			canonicalDomainMask |= UINT32_C(1) << i;
		}
	}
	if (canonicalDomainMask != 0)
	{
		mask |= NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN;
	}
	if (record->combinedDigest != bundle->verifiedCombinedDigest)
	{
		mask |= NATIVE_LOCKSTEP_DIVERGENCE_COMBINED;
	}
	if (mask == 0)
	{
		return 0;
	}

	NativeLockstepSession_LatchDivergence(session, mask, canonicalDomainMask, bundle, record);
	return 1;
}

void NativeLockstepSession_Init(struct NativeLockstepSession *session)
{
	if (session == NULL)
	{
		return;
	}

	/* Zeroed whole, so a whole-struct memcmp against a fresh session is
	 * meaningful and every output field starts at a known value. */
	memset(session, 0, sizeof(*session));
	session->mode = NATIVE_LOCKSTEP_IDLE;
}

int NativeLockstepSession_Open(struct NativeLockstepSession *session, const struct NativeMatchConfigV1 *config, uint32_t inputDelay,
                               uint8_t localSlot)
{
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	uint32_t peerCount = 0;

	if ((session == NULL) || (config == NULL) || (session->mode != NATIVE_LOCKSTEP_IDLE) || !NativeMatchConfigV1_Validate(config))
	{
		return 0;
	}
	/* D is configured, not negotiated, and both the peer rings and the digest
	 * history are fixed size, so an out-of-range D is rejected here rather than
	 * overflowing either of them. */
	if ((inputDelay < (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY) || (inputDelay > (uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY) ||
	    (((uint64_t)inputDelay + 1u) > (uint64_t)NATIVE_LOCKSTEP_RING_CAPACITY) ||
	    (((uint64_t)inputDelay + 2u) > (uint64_t)NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY))
	{
		return 0;
	}
	if ((localSlot >= NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY) || !NativeLockstepSession_IsHumanSlot(config, localSlot))
	{
		return 0;
	}
	/* The full 32-byte SHA-256 config digest is compared once, here; only its
	 * 8-byte prefix travels per frame. */
	if (!NativeMatchConfigV1_Digest(config, digest))
	{
		return 0;
	}

	/* Nothing above this line touches the session, so a rejected open leaves
	 * the caller's struct exactly as it was. */
	memset(session, 0, sizeof(*session));
	session->config = *config;
	memcpy(session->configDigest, digest, sizeof(session->configDigest));
	memcpy(session->matchIdentity, digest, sizeof(session->matchIdentity));
	session->protocolVersion = config->protocolVersion;
	session->inputDelay = inputDelay;
	session->historyCapacity = inputDelay + 2u;
	session->localSlot = localSlot;
	session->consumedFrame = 0u;
	for (uint32_t slot = 0; slot < NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY; slot++)
	{
		if ((slot == (uint32_t)localSlot) || !NativeLockstepSession_IsHumanSlot(config, slot))
		{
			continue;
		}
		/* Cannot fail: inputDelay was validated against the ring above. */
		if (!NativeLockstepInputWindow_Init(&session->peers[slot], inputDelay))
		{
			NativeLockstepSession_Init(session);
			return 0;
		}
		session->peerActive[slot] = 1u;
		peerCount++;
	}
	session->peerCount = peerCount;
	session->mode = NATIVE_LOCKSTEP_RUNNING;
	return 1;
}

int NativeLockstepSession_SubmitLocalInput(struct NativeLockstepSession *session, uint32_t sampleFrame,
                                           const struct NativeCanonicalInputPadV1 *pad)
{
	struct NativeLockstepSessionLocalInput *record;
	uint64_t consumeFrame;

	if ((session == NULL) || (pad == NULL) || (session->mode != NATIVE_LOCKSTEP_RUNNING))
	{
		return 0;
	}

	/* The session owns the delay: input sampled at S is consumed at S + D. */
	consumeFrame = (uint64_t)sampleFrame + (uint64_t)session->inputDelay;
	if (consumeFrame > (uint64_t)UINT32_MAX)
	{
		return 0;
	}

	/*
	 * A buffered pad is the pad already put on the wire, so it is never
	 * replaced: a consumption frame the simulation has already passed is
	 * refused, and so is a ring slot still holding a pad for an unconsumed
	 * frame, which covers both a re-submission for the same sample frame and a
	 * modular collision with a different one.  Overwriting either would make the
	 * locally consumed pad differ from the pad the peer was sent.
	 */
	if ((uint32_t)consumeFrame < session->consumedFrame)
	{
		return 0;
	}
	record = &session->localInputs[(uint32_t)consumeFrame % (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY];
	if ((record->present != 0) && (record->frameIndex >= session->consumedFrame))
	{
		return 0;
	}
	memset(record, 0, sizeof(*record));
	record->frameIndex = (uint32_t)consumeFrame;
	record->present = 1u;
	record->pad = *pad;
	return 1;
}

int NativeLockstepSession_RecordLocalDigests(struct NativeLockstepSession *session, const struct NativeCanonicalStateV4 *state)
{
	struct NativeLockstepSessionDigestRecord *record;

	if ((session == NULL) || (state == NULL) || (session->mode != NATIVE_LOCKSTEP_RUNNING) || !NativeCanonicalStateV4_Validate(state))
	{
		return 0;
	}
	/* Strictly increasing: recording backwards would retire a newer frame the
	 * verification lag still needs. */
	if ((session->recordedAny != 0) && (state->frameNumber <= session->recordedFrame))
	{
		return 0;
	}
	/* historyCapacity is always inputDelay + 2 with inputDelay in
	 * [MIN_INPUT_DELAY, MAX_INPUT_DELAY] once Open has succeeded, so it can
	 * never be 0 or exceed NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY
	 * through the public API.  The same modulus is guarded the same way in
	 * FindDigests, whose comment explains why it is checked rather than
	 * trusted; this call is not wire facing, but the array below is sized to
	 * that capacity, so a 0 divides by zero and an over-large one indexes past
	 * it, and neither is worth trusting an invariant to avoid checking. */
	if ((session->historyCapacity == 0u) || (session->historyCapacity > NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY))
	{
		return 0;
	}

	record = &session->localDigests[state->frameNumber % session->historyCapacity];
	memset(record, 0, sizeof(*record));
	record->frameIndex = state->frameNumber;
	record->present = 1u;
	/* Index i is domain NativeCanonicalDomainOrder[i].  The state is read-only. */
	memcpy(record->domainDigests, state->domainDigests, sizeof(record->domainDigests));
	record->combinedDigest = state->combinedDigest;
	session->recordedFrame = state->frameNumber;
	session->recordedAny = 1u;
	return 1;
}

int NativeLockstepSession_ComposeBundle(const struct NativeLockstepSession *session, uint32_t frameIndex, uint8_t *bytes, size_t capacity,
                                        size_t *sizeOut)
{
	struct NativeLockstepBundleV1 bundle;
	struct NativeCodecWriter writer;
	const struct NativeLockstepSessionLocalInput *input;

	if ((session == NULL) || (bytes == NULL) || (sizeOut == NULL) || (capacity < NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES) ||
	    (session->mode != NATIVE_LOCKSTEP_RUNNING))
	{
		return 0;
	}

	memset(&bundle, 0, sizeof(bundle));
	bundle.protocolVersion = session->protocolVersion;
	memcpy(bundle.matchIdentity, session->matchIdentity, sizeof(bundle.matchIdentity));
	bundle.frameIndex = frameIndex;
	bundle.inputDelay = session->inputDelay;
	bundle.senderSlot = session->localSlot;
	/* The two-cabinet profile gives each peer exactly one human slot. */
	bundle.padCount = 1u;
	bundle.pads[0].slotIndex = session->localSlot;
	input = &session->localInputs[frameIndex % (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY];
	if ((input->present != 0) && (input->frameIndex == frameIndex))
	{
		bundle.pads[0].pad = input->pad;
	}
	for (uint32_t i = bundle.padCount; i < NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY; i++)
	{
		bundle.pads[i].slotIndex = NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT;
	}

	/*
	 * The newest digest that exists when this bundle is composed is frame
	 * frameIndex - D - 1, so the lag is fixed at D + 1 frames and the first
	 * D + 1 frames of a session carry no digest at all.
	 */
	if (frameIndex >= (session->inputDelay + 1u))
	{
		const uint32_t verifiedFrameIndex = frameIndex - session->inputDelay - 1u;
		const struct NativeLockstepSessionDigestRecord *record = NativeLockstepSession_FindDigests(session, verifiedFrameIndex);

		if (record == NULL)
		{
			/* Refused rather than silently sending an unverified bundle. */
			return 0;
		}
		bundle.verifiedFrameIndex = verifiedFrameIndex;
		bundle.verifiedPresent = 1u;
		memcpy(bundle.verifiedDomainDigests, record->domainDigests, sizeof(bundle.verifiedDomainDigests));
		bundle.verifiedCombinedDigest = record->combinedDigest;
	}

	/* Bounded at the encoded width, so a larger capacity is never written to. */
	NativeCodecWriter_Init(&writer, bytes, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES, NULL);
	if (!NativeLockstepBundleV1_Encode(&writer, &bundle) ||
	    (NativeCodecWriter_Size(&writer) != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES))
	{
		return 0;
	}
	*sizeOut = NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES;
	return 1;
}

enum NativeLockstepSessionResult NativeLockstepSession_AcceptBundle(struct NativeLockstepSession *session, const uint8_t *bytes,
                                                                    size_t size)
{
	struct NativeCodecReader reader;
	struct NativeLockstepBundleV1 bundle;
	struct NativeLockstepInputWindow *window;
	enum NativeLockstepInputWindowResult offered;
	enum NativeLockstepSessionResult result;
	uint32_t cause = NATIVE_LOCKSTEP_FAULT_NONE;
	uint32_t slot;

	if ((session == NULL) || (bytes == NULL) || (session->mode == NATIVE_LOCKSTEP_IDLE))
	{
		return NATIVE_LOCKSTEP_SESSION_REJECTED;
	}
	/* A wrong-width datagram is wire data, not caller misuse. */
	if (size != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
	{
		NativeLockstepSession_LatchFault(session, NATIVE_LOCKSTEP_FAULT_BAD_SIZE, 0u, 0u, 0u);
		return NATIVE_LOCKSTEP_SESSION_FAULT;
	}

	memset(&bundle, 0, sizeof(bundle));
	NativeCodecReader_Init(&reader, bytes, size);
	if (!NativeLockstepBundleV1_Decode(&reader, session->matchIdentity, session->protocolVersion, session->inputDelay, &bundle, &cause))
	{
		/* Nothing read out of an undecodable record can be trusted, so the
		 * frame index and sender slot of the report stay zero. */
		NativeLockstepSession_LatchFault(session, cause, 0u, 0u, 0u);
		return NATIVE_LOCKSTEP_SESSION_FAULT;
	}

	slot = bundle.senderSlot;
	if ((slot >= NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY) || (session->peerActive[slot] == 0))
	{
		NativeLockstepSession_LatchFault(session, NATIVE_LOCKSTEP_FAULT_BAD_SLOT, bundle.frameIndex, slot, session->localSlot);
		return NATIVE_LOCKSTEP_SESSION_FAULT;
	}

	/*
	 * The window classifies the record first, and only a record it ACCEPTED is
	 * digest-verified.  A stale or duplicate re-delivery is a legitimate drop
	 * from a delaying or duplicating transport, and a faulted record was not
	 * taken into the match at all, so neither may latch a divergence against a
	 * history that has since retired its verified frame.
	 */
	window = &session->peers[slot];
	offered = NativeLockstepInputWindow_Offer(window, bytes, size, &bundle, &cause);
	switch (offered)
	{
	case NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED:
		result = NativeLockstepSession_Verify(session, &bundle) ? NATIVE_LOCKSTEP_SESSION_DIVERGENCE : NATIVE_LOCKSTEP_SESSION_OK;
		break;
	case NATIVE_LOCKSTEP_INPUT_WINDOW_DUPLICATE:
		result = NATIVE_LOCKSTEP_SESSION_DUPLICATE;
		break;
	case NATIVE_LOCKSTEP_INPUT_WINDOW_STALE:
		result = NATIVE_LOCKSTEP_SESSION_STALE;
		break;
	case NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT:
		NativeLockstepSession_LatchFault(session, cause, bundle.frameIndex, slot, window->consumedFrame);
		result = NATIVE_LOCKSTEP_SESSION_FAULT;
		break;
	default:
		/* Unreachable: Offer only rejects NULLs and a wrong byte count. */
		result = NATIVE_LOCKSTEP_SESSION_REJECTED;
		break;
	}
	return result;
}

enum NativeLockstepSessionResult NativeLockstepSession_TakeFrameInputs(struct NativeLockstepSession *session, uint32_t frameIndex,
                                                                       struct NativeLockstepSessionFrameInputs *inputsOut)
{
	struct NativeLockstepSessionFrameInputs inputs;
	const struct NativeLockstepSessionLocalInput *local;

	if ((session == NULL) || (inputsOut == NULL) || (session->mode != NATIVE_LOCKSTEP_RUNNING) ||
	    (frameIndex != session->consumedFrame))
	{
		return NATIVE_LOCKSTEP_SESSION_REJECTED;
	}

	/* All peers or none: a partial consumption could not be retried. */
	for (uint32_t slot = 0; slot < NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY; slot++)
	{
		if ((session->peerActive[slot] != 0) && (NativeLockstepInputWindow_Peek(&session->peers[slot], frameIndex) == NULL))
		{
			return NATIVE_LOCKSTEP_SESSION_STALL;
		}
	}

	memset(&inputs, 0, sizeof(inputs));
	inputs.frameIndex = frameIndex;
	/* The local pad first.  An absent entry is the neutral zero pad, which is
	 * what the first inputDelay consumption frames legitimately consume. */
	inputs.pads[0].slotIndex = session->localSlot;
	local = &session->localInputs[frameIndex % (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY];
	if ((local->present != 0) && (local->frameIndex == frameIndex))
	{
		inputs.pads[0].pad = local->pad;
	}
	inputs.padCount = 1u;

	for (uint32_t slot = 0; slot < NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY; slot++)
	{
		struct NativeLockstepBundleV1 taken;

		if (session->peerActive[slot] == 0)
		{
			continue;
		}
		/* Cannot stall: every active peer was peeked above. */
		if (NativeLockstepInputWindow_Take(&session->peers[slot], frameIndex, &taken) != NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED)
		{
			return NATIVE_LOCKSTEP_SESSION_REJECTED;
		}
		for (uint32_t i = 0; i < taken.padCount; i++)
		{
			if (inputs.padCount >= NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY)
			{
				return NATIVE_LOCKSTEP_SESSION_REJECTED;
			}
			inputs.pads[inputs.padCount] = taken.pads[i];
			inputs.padCount++;
		}
	}
	/* Unused entries carry the bundle's own convention. */
	for (uint32_t i = inputs.padCount; i < NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY; i++)
	{
		inputs.pads[i].slotIndex = NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT;
	}

	session->consumedFrame = frameIndex + 1u;
	*inputsOut = inputs;
	return NATIVE_LOCKSTEP_SESSION_OK;
}

const struct NativeLockstepDivergenceReport *NativeLockstepSession_FirstDivergence(const struct NativeLockstepSession *session)
{
	return ((session != NULL) && (session->mode == NATIVE_LOCKSTEP_DIVERGED)) ? &session->divergence : NULL;
}

const struct NativeLockstepFaultReport *NativeLockstepSession_FirstFault(const struct NativeLockstepSession *session)
{
	return ((session != NULL) && (session->faulted != 0)) ? &session->fault : NULL;
}

enum NativeLockstepSessionMode NativeLockstepSession_Mode(const struct NativeLockstepSession *session)
{
	return (session != NULL) ? session->mode : NATIVE_LOCKSTEP_IDLE;
}
