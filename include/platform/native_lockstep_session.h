#ifndef PLATFORM_NATIVE_LOCKSTEP_SESSION_H
#define PLATFORM_NATIVE_LOCKSTEP_SESSION_H

#include "platform/native_canonical_state_v4.h"
#include "platform/native_lockstep_input_window.h"
#include "platform/native_match_config.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-delay lockstep session: the local input delay line, one delay / reorder
 * window per remote peer, a short history of local per-frame canonical digests,
 * and the two once-only reports.  Like the codec and the window it is
 * transport-agnostic by design: it composes into and accepts from caller-owned
 * byte buffers and has no socket, OS networking, SDL, clock, or game
 * dependency, and every byte lives in the caller-owned struct, so no heap is
 * involved at all.  It consumes match config, canonical state, and replay
 * digests read-only and never writes back to them.
 *
 * There is no rollback and no prediction.  Simulation frame N consumes only
 * inputs that every peer sampled at frame N - D, so the simulation stays a pure
 * function of a fully committed input set and a lockstep match is still a V4
 * replay with no format change.
 */

/* One window per config slot, so a sender slot indexes the peer array directly. */
#define NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY NATIVE_MATCH_CONFIG_V1_SLOT_COUNT
/* The local digest history is inputDelay + 2 frames deep; this is its bound. */
#define NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY (NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 2u)
/* One local entry plus every other slot's full bundle pad capacity. */
#define NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY (NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY * NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY)

/*
 * DIVERGED and FAULTED are both terminal for simulation.  Only DIVERGED
 * outranks FAULTED for retention: once a divergence is latched the mode stays
 * DIVERGED even if a protocol fault is latched afterwards, the same asymmetry
 * NativeReplaySchedulerV4 Poison() gives Match().
 */
enum NativeLockstepSessionMode
{
	NATIVE_LOCKSTEP_IDLE = 0,
	NATIVE_LOCKSTEP_RUNNING = 1,
	NATIVE_LOCKSTEP_DIVERGED = 2,
	NATIVE_LOCKSTEP_FAULTED = 3
};

/*
 * Accept and Take share one result.  The enum is append-only: results are
 * intended to be latched and logged verbatim, so new results may only be
 * appended at the end and an existing numeric value must never change.
 */
enum NativeLockstepSessionResult
{
	NATIVE_LOCKSTEP_SESSION_OK = 0,        /* Stored, or consumed. */
	NATIVE_LOCKSTEP_SESSION_STALE = 1,     /* Below the peer window; dropped; not an error. */
	NATIVE_LOCKSTEP_SESSION_DUPLICATE = 2, /* Byte-identical re-delivery, accepted as a no-op. */
	NATIVE_LOCKSTEP_SESSION_STALL = 3,     /* A peer frame has not arrived; not an error; nothing latched. */
	NATIVE_LOCKSTEP_SESSION_REJECTED = 4,  /* Caller misuse: NULL, wrong frame, wrong mode. */
	NATIVE_LOCKSTEP_SESSION_FAULT = 5,     /* The record is a protocol fault. */
	NATIVE_LOCKSTEP_SESSION_DIVERGENCE = 6 /* The record's verified digest disagrees with the local one. */
};

/*
 * Same bit-flag idiom as enum NativeReplaySchedulerV4MismatchMask.
 * FRAME_UNAVAILABLE covers a peer digest for a frame the local side cannot
 * compare, because it never simulated it or has already retired it from the
 * digest history.  It is a divergence, not a protocol fault: the bundle is well
 * formed but the two simulations are no longer comparable, so it never produces
 * an enum NativeLockstepFaultCause value.
 */
enum NativeLockstepDivergenceMask
{
	NATIVE_LOCKSTEP_DIVERGENCE_COMBINED = 1u,
	NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN = 2u,
	NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE = 4u
};

/*
 * canonicalDomainMask is UINT32_C(1) << i per differing domainDigests[i], the
 * same shape as NativeReplaySchedulerV4MismatchReport.canonicalDomainMask, so
 * index i names domain NativeCanonicalDomainOrder[i].  frameIndex is the peer
 * bundle's verifiedFrameIndex: the frame that actually diverged, not the frame
 * on which the disagreement was noticed.  senderSlot names which peer reported
 * it.  Unlike the replay scheduler both sides' digests are retained, because a
 * lockstep peer cannot re-read the other side's state afterwards.  On a
 * FRAME_UNAVAILABLE report the local digests are zero: there was nothing to
 * compare.
 */
struct NativeLockstepDivergenceReport
{
	uint32_t mask;
	uint32_t canonicalDomainMask;
	uint32_t frameIndex;
	uint32_t senderSlot;
	uint64_t localCombinedDigest;
	uint64_t remoteCombinedDigest;
	uint64_t localDomainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint64_t remoteDomainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
};

/*
 * cause is an enum NativeLockstepFaultCause value.  frameIndex and senderSlot
 * are the offending bundle's when it decoded and are both zero when it did not,
 * because nothing read out of an undecodable record can be trusted.  detail is
 * cause-specific and zero unless documented: for
 * NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN it is the peer window's consumedFrame,
 * and for a NATIVE_LOCKSTEP_FAULT_BAD_SLOT raised by an unexpected sender it is
 * the local slot.
 */
struct NativeLockstepFaultReport
{
	uint32_t cause;
	uint32_t frameIndex;
	uint32_t senderSlot;
	uint32_t detail;
};

/* One consumption frame of the local delay line, tagged with its frame. */
struct NativeLockstepSessionLocalInput
{
	uint32_t frameIndex;
	uint8_t present;
	struct NativeCanonicalInputPadV1 pad;
};

/* One frame of local canonical verification history, tagged with its frame. */
struct NativeLockstepSessionDigestRecord
{
	uint32_t frameIndex;
	uint8_t present;
	uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint64_t combinedDigest;
};

/*
 * The committed input set for one simulation frame: the local pad first, then
 * every peer's pads in slot order.  Unused entries are slot 0xFF with a zero
 * pad, the same convention the bundle uses.
 */
struct NativeLockstepSessionFrameInputs
{
	uint32_t frameIndex;
	uint32_t padCount;
	struct NativeLockstepBundlePadV1 pads[NATIVE_LOCKSTEP_SESSION_FRAME_PAD_CAPACITY];
};

struct NativeLockstepSession
{
	enum NativeLockstepSessionMode mode;
	struct NativeMatchConfigV1 config;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES];
	/* The deliberate 8-byte truncation of configDigest that travels per frame. */
	uint8_t matchIdentity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES];
	uint32_t protocolVersion;
	uint32_t inputDelay;
	uint32_t historyCapacity;
	uint32_t peerCount;
	uint32_t consumedFrame;
	uint32_t recordedFrame;
	uint8_t recordedAny;
	uint8_t localSlot;
	uint8_t faulted;
	uint8_t peerActive[NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY];
	struct NativeLockstepSessionLocalInput localInputs[NATIVE_LOCKSTEP_RING_CAPACITY];
	struct NativeLockstepSessionDigestRecord localDigests[NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY];
	struct NativeLockstepInputWindow peers[NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY];
	struct NativeLockstepDivergenceReport divergence;
	struct NativeLockstepFaultReport fault;
};

/* Zeroes the whole struct and leaves the session IDLE. */
void NativeLockstepSession_Init(struct NativeLockstepSession *session);

/*
 * Opens a session on a validated match config.  Computes the full 32-byte
 * NativeMatchConfigV1_Digest, retains it, and stores its 8-byte prefix as the
 * per-frame match identity.  localSlot must be a human slot of the config; a
 * lockstep peer is every other human slot, because bot inputs are not on the
 * wire: both peers run the same bots from the shared seed and roster.
 *
 * D is configured, not negotiated, so an out-of-range D is rejected here rather
 * than overflowing a fixed buffer: inputDelay must lie in
 * [NATIVE_LOCKSTEP_MIN_INPUT_DELAY, NATIVE_LOCKSTEP_MAX_INPUT_DELAY], must
 * satisfy inputDelay + 1 <= NATIVE_LOCKSTEP_RING_CAPACITY for the peer rings,
 * and must satisfy
 * inputDelay + 2 <= NATIVE_LOCKSTEP_SESSION_DIGEST_HISTORY_CAPACITY for the
 * digest history.  Returns 0 with *session untouched on any rejection, and only
 * a session that is still IDLE may be opened.  On success the mode is RUNNING
 * and consumedFrame is 0.
 */
int NativeLockstepSession_Open(struct NativeLockstepSession *session, const struct NativeMatchConfigV1 *config, uint32_t inputDelay,
                               uint8_t localSlot);

/*
 * Buffers one locally sampled pad.  The session owns the delay: input sampled
 * at frame S is tagged for consumption at frame S + inputDelay.  The first
 * inputDelay consumption frames therefore have no submitted local input and
 * consume a zero pad, which is the neutral input every peer agrees on.
 * Requires the RUNNING mode; returns 0 and changes nothing otherwise.
 *
 * A buffered pad is the pad already put on the wire, so a submission never
 * replaces one: the call is refused, with the delay line untouched, when the
 * consumption frame is below consumedFrame and so already simulated, and when
 * the ring slot still holds a pad for a consumption frame at or above
 * consumedFrame.  That covers both a re-submission for the same sampleFrame and
 * a sample whose consumption frame collides modulo
 * NATIVE_LOCKSTEP_RING_CAPACITY with an already buffered one; either would make
 * the locally consumed pad differ from the pad the peer was sent, which is a
 * silent desync that would only surface later as a divergence report.
 */
int NativeLockstepSession_SubmitLocalInput(struct NativeLockstepSession *session, uint32_t sampleFrame,
                                           const struct NativeCanonicalInputPadV1 *pad);

/*
 * Records the already computed per-frame verification digests of one simulated
 * frame, read out of state->domainDigests, state->combinedDigest, and
 * state->frameNumber.  The state is consumed read-only and nothing is written
 * back to it.  Frames must be recorded in strictly increasing simulation order,
 * because recording backwards would retire a newer frame the verification lag
 * still needs.  Requires the RUNNING mode and a state that validates; returns 0
 * and changes nothing otherwise.
 */
int NativeLockstepSession_RecordLocalDigests(struct NativeLockstepSession *session, const struct NativeCanonicalStateV4 *state);

/*
 * Composes this peer's bundle carrying the local input for consumption frame
 * frameIndex into exactly NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES bytes, which
 * is written to *sizeOut on success.  capacity must be at least that width; no
 * byte beyond it is ever written.
 *
 * Verification lags simulation necessarily: a frame's digest does not exist
 * until that frame has been simulated, and the bundle must arrive before that
 * frame can be simulated at all, because it carries the input the frame needs.
 * So verifiedFrameIndex is frameIndex - inputDelay - 1, a fixed lag of
 * inputDelay + 1 frames, and for the first inputDelay + 1 frames of a session
 * verifiedPresent is 0 and the digest fields are zero.  Beyond those frames the
 * lagging frame's digests must already have been recorded; if they have not, or
 * have been retired from the history, the call is refused rather than silently
 * sending an unverified bundle.  Requires the RUNNING mode.
 */
int NativeLockstepSession_ComposeBundle(const struct NativeLockstepSession *session, uint32_t frameIndex, uint8_t *bytes, size_t capacity,
                                        size_t *sizeOut);

/*
 * Accepts one received wire record.  A NULL session or bytes, or a session that
 * is still IDLE, is REJECTED and latches nothing.  Everything else is wire
 * data, so a wrong byte count is a NATIVE_LOCKSTEP_FAULT_BAD_SIZE fault rather
 * than caller misuse.  The record is decoded against the session's match
 * identity, protocol version, and input delay; a sender slot that is not a
 * known peer of this session is NATIVE_LOCKSTEP_FAULT_BAD_SLOT.
 *
 * The record is offered to the sender's window first and its verified digest
 * block is compared against the local digest history only when the window
 * ACCEPTED it, that is only for a genuinely new in-window record.  A STALE,
 * DUPLICATE, or faulted record is never digest-compared and can never latch a
 * divergence: a duplicating or delaying transport legitimately re-delivers a
 * record whose verified frame the local history has already retired, and a
 * window fault means the record was not taken into the match at all.  No
 * explicit dedup structure keeps one verifiedFrameIndex from being compared
 * twice: the window's occupancy-slot check in Offer
 * (platform/native_lockstep_input_window.c:83-97) accepts a given frameIndex
 * at most once, having already dropped a below-window re-delivery as STALE
 * (platform/native_lockstep_input_window.c:65-69), and the codec pins
 * verifiedFrameIndex + inputDelay + 1 == frameIndex on every decode
 * (platform/native_lockstep_protocol.c:308-312).  Composed, at-most-once
 * ACCEPTED delivery per frameIndex plus that bijection between frameIndex and
 * verifiedFrameIndex means at most one ACCEPTED record can ever carry a given
 * verifiedFrameIndex, so a record cannot be re-verified however the transport
 * reorders the wire.  The two latches
 * remain independent and each is once-only: a fault arriving after a divergence
 * leaves the divergence report byte-identical and the mode DIVERGED, and a
 * divergence arriving after a fault leaves the fault report byte-identical and
 * raises the mode to DIVERGED.
 *
 * The result describes this record, not the latch state: FAULT when this record
 * is a protocol fault, DUPLICATE or STALE from the sender's window, and for an
 * accepted record DIVERGENCE when its verified digest disagrees with the local
 * one and OK otherwise.  A later record of the same kind therefore still reports
 * itself even though the report it would have written is ignored.  Deliberately
 * usable in every non-IDLE mode, which is what lets a fault be latched after a
 * divergence has ended the match.
 */
enum NativeLockstepSessionResult NativeLockstepSession_AcceptBundle(struct NativeLockstepSession *session, const uint8_t *bytes,
                                                                    size_t size);

/*
 * Consumes the committed input set for simulation frame frameIndex, which must
 * equal consumedFrame.  A wrong frame, a NULL argument, or a mode other than
 * RUNNING is REJECTED and moves nothing: DIVERGED and FAULTED are terminal for
 * simulation.  If any peer's frame has not arrived the call is a STALL, which
 * is not an error, latches nothing, consumes from no peer, and leaves
 * consumedFrame unmoved: the caller must not advance the simulation and should
 * service the transport and retry.  Consumption is all peers or none, so a
 * stalled frame can always be retried unchanged.  On OK *inputsOut holds the
 * local pad followed by every peer's pads in slot order and consumedFrame
 * becomes frameIndex + 1.
 */
enum NativeLockstepSessionResult NativeLockstepSession_TakeFrameInputs(struct NativeLockstepSession *session, uint32_t frameIndex,
                                                                       struct NativeLockstepSessionFrameInputs *inputsOut);

/* &session->divergence once a divergence is latched, NULL until then. */
const struct NativeLockstepDivergenceReport *NativeLockstepSession_FirstDivergence(const struct NativeLockstepSession *session);

/* &session->fault once a protocol fault is latched, NULL until then. */
const struct NativeLockstepFaultReport *NativeLockstepSession_FirstFault(const struct NativeLockstepSession *session);

enum NativeLockstepSessionMode NativeLockstepSession_Mode(const struct NativeLockstepSession *session);

#endif
