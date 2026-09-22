#ifndef PLATFORM_NATIVE_LOCKSTEP_INPUT_WINDOW_H
#define PLATFORM_NATIVE_LOCKSTEP_INPUT_WINDOW_H

#include "platform/native_lockstep_protocol.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-capacity delay / reorder window, one ring per remote peer.  Like the
 * frame-bundle codec it is transport-agnostic by design: it has no OS
 * transport, windowing, clock, or game dependency, it never touches a wire
 * format of its own, and every byte lives in the caller-owned struct, so no
 * heap is involved at all.  The ring is indexed by frame, not by arrival
 * order, so an out-of-order arrival is stored exactly where the in-order
 * arrival would have been.  Consumption stalls rather than erroring when the
 * next frame has not arrived yet.
 */

#define NATIVE_LOCKSTEP_RING_CAPACITY 8 /* Power of two; the ring index is frameIndex % capacity. */
#define NATIVE_LOCKSTEP_MIN_INPUT_DELAY 1
#define NATIVE_LOCKSTEP_MAX_INPUT_DELAY 6

/*
 * Offer and Take share one result.  The enum is append-only: results are
 * intended to be latched and logged verbatim, so new results may only be
 * appended at the end and an existing numeric value must never change.
 */
enum NativeLockstepInputWindowResult
{
	NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED = 0,  /* Stored, or taken; the slot bit was set, or cleared. */
	NATIVE_LOCKSTEP_INPUT_WINDOW_DUPLICATE = 1, /* Byte-identical re-delivery, accepted as a no-op. */
	NATIVE_LOCKSTEP_INPUT_WINDOW_STALE = 2,     /* Below consumedFrame; dropped; not an error. */
	NATIVE_LOCKSTEP_INPUT_WINDOW_STALL = 3,     /* Take found the slot empty; not an error; nothing latched. */
	NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED = 4,  /* Caller misuse: NULL, wrong byte count, wrong frame. */
	NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT = 5      /* Protocol fault; the cause is written to faultCauseOut. */
};

/*
 * The exact encoded bytes are kept beside the decoded bundle so duplicate
 * equality is a memcmp over the wire record: a struct compare would read
 * interior padding.  Occupancy is bit frameIndex % capacity of occupancyMask,
 * and the acceptance window is [consumedFrame, consumedFrame + capacity - 1].
 */
struct NativeLockstepInputWindow
{
	uint8_t bytes[NATIVE_LOCKSTEP_RING_CAPACITY][NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	struct NativeLockstepBundleV1 bundles[NATIVE_LOCKSTEP_RING_CAPACITY];
	uint32_t occupancyMask;
	uint32_t consumedFrame;
	uint32_t inputDelay;
	uint32_t staleDropCount;
	uint32_t duplicateAcceptCount;
};

/*
 * Zeroes the whole struct, then stores inputDelay and starts the window at
 * frame 0, so consumedFrame is 0 and the first frame the simulation may consume
 * is 0.  Returns 0 with *window untouched when window is NULL, when inputDelay
 * is outside [NATIVE_LOCKSTEP_MIN_INPUT_DELAY, NATIVE_LOCKSTEP_MAX_INPUT_DELAY],
 * or when inputDelay + 1 exceeds NATIVE_LOCKSTEP_RING_CAPACITY.
 */
int NativeLockstepInputWindow_Init(struct NativeLockstepInputWindow *window, uint32_t inputDelay);

/*
 * Offers one already decoded and already validated record for bundle frame
 * bundle->frameIndex: this function never decodes and never re-validates the
 * wire, which is NativeLockstepBundleV1_Decode's job.  byteCount must be
 * exactly NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES; any other count, or a NULL
 * window, bytes, or bundle, is REJECTED with the window unchanged.  A frame
 * below consumedFrame is STALE and increments staleDropCount, because a
 * duplicating or delaying transport legitimately re-delivers a consumed frame.
 * A frame at or above consumedFrame + NATIVE_LOCKSTEP_RING_CAPACITY is a FAULT
 * with cause NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN and changes nothing at all:
 * the ring never grows and never evicts an unconsumed frame.  An in-window
 * frame whose slot is empty is ACCEPTED and stored.  An in-window frame whose
 * slot is occupied is DUPLICATE when the offered bytes are byte-identical to
 * the stored bytes, incrementing duplicateAcceptCount and keeping the stored
 * record, and otherwise a FAULT with cause
 * NATIVE_LOCKSTEP_FAULT_CONFLICTING_INPUT that changes nothing.  faultCauseOut
 * is optional and is written only on FAULT, with a value from
 * enum NativeLockstepFaultCause.
 */
enum NativeLockstepInputWindowResult NativeLockstepInputWindow_Offer(struct NativeLockstepInputWindow *window, const uint8_t *bytes,
                                                                     size_t byteCount, const struct NativeLockstepBundleV1 *bundle,
                                                                     uint32_t *faultCauseOut);

/*
 * Consumes frame frameIndex, which must equal consumedFrame; any other frame,
 * or a NULL window or bundleOut, is REJECTED and leaves consumedFrame unmoved.
 * An empty slot is a STALL: not an error, nothing is latched and consumedFrame
 * does not move, so the caller must not advance the simulation and should
 * service the transport and retry.  On ACCEPTED the stored bundle is copied to
 * *bundleOut, the slot bit is cleared, and consumedFrame becomes
 * frameIndex + 1.
 */
enum NativeLockstepInputWindowResult NativeLockstepInputWindow_Take(struct NativeLockstepInputWindow *window, uint32_t frameIndex,
                                                                    struct NativeLockstepBundleV1 *bundleOut);

/*
 * Non-mutating.  Returns the stored bundle for frameIndex when it is in
 * [consumedFrame, consumedFrame + NATIVE_LOCKSTEP_RING_CAPACITY - 1] and the
 * slot is occupied, and NULL otherwise.
 */
const struct NativeLockstepBundleV1 *NativeLockstepInputWindow_Peek(const struct NativeLockstepInputWindow *window, uint32_t frameIndex);

#endif
