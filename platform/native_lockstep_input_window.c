#include "platform/native_lockstep_input_window.h"

#include <string.h>

/*
 * Compile-time only: raising one constant without the other must fail to
 * build.  The window must hold the frame being consumed plus the configured
 * delay, and the slot index is a modulus of the capacity, so the capacity has
 * to stay a power of two for the occupancy bit and the index to agree.
 */
_Static_assert(NATIVE_LOCKSTEP_RING_CAPACITY >= NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1,
               "The ring capacity must be at least NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1.");
_Static_assert((NATIVE_LOCKSTEP_RING_CAPACITY & (NATIVE_LOCKSTEP_RING_CAPACITY - 1)) == 0,
               "The ring capacity must be a power of two.");

static uint32_t NativeLockstepInputWindow_SlotIndex(uint32_t frameIndex)
{
	return frameIndex % (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY;
}

static int NativeLockstepInputWindow_SlotOccupied(const struct NativeLockstepInputWindow *window, uint32_t slotIndex)
{
	return (window->occupancyMask & (UINT32_C(1) << slotIndex)) != 0;
}

/*
 * The upper bound is computed in 64-bit so a large frameIndex cannot wrap a
 * uint32 back into the window.
 */
static int NativeLockstepInputWindow_InWindow(const struct NativeLockstepInputWindow *window, uint32_t frameIndex)
{
	return (frameIndex >= window->consumedFrame) &&
	       ((uint64_t)frameIndex < (uint64_t)window->consumedFrame + (uint64_t)NATIVE_LOCKSTEP_RING_CAPACITY);
}

int NativeLockstepInputWindow_Init(struct NativeLockstepInputWindow *window, uint32_t inputDelay)
{
	if ((window == NULL) || (inputDelay < (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY) ||
	    (inputDelay > (uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY) ||
	    ((uint64_t)inputDelay + 1u > (uint64_t)NATIVE_LOCKSTEP_RING_CAPACITY))
	{
		return 0;
	}

	/* Zeroed whole, so a whole-struct memcmp against a fresh window is meaningful. */
	memset(window, 0, sizeof(*window));
	window->inputDelay = inputDelay;
	window->consumedFrame = 0;
	return 1;
}

enum NativeLockstepInputWindowResult NativeLockstepInputWindow_Offer(struct NativeLockstepInputWindow *window, const uint8_t *bytes,
                                                                     size_t byteCount, const struct NativeLockstepBundleV1 *bundle,
                                                                     uint32_t *faultCauseOut)
{
	uint32_t slotIndex;

	if ((window == NULL) || (bytes == NULL) || (bundle == NULL) || (byteCount != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES))
	{
		return NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED;
	}

	/* A duplicating or delaying transport legitimately re-delivers a frame the
	 * simulation already consumed, so this is a drop, not an error. */
	if (bundle->frameIndex < window->consumedFrame)
	{
		window->staleDropCount++;
		return NATIVE_LOCKSTEP_INPUT_WINDOW_STALE;
	}

	/* Ahead of the window: nothing in the struct changes, not even a counter,
	 * because the ring never grows and never evicts an unconsumed frame. */
	if (!NativeLockstepInputWindow_InWindow(window, bundle->frameIndex))
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN;
		}
		return NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT;
	}

	slotIndex = NativeLockstepInputWindow_SlotIndex(bundle->frameIndex);
	if (NativeLockstepInputWindow_SlotOccupied(window, slotIndex))
	{
		/* Equality is decided over the stored wire record: a struct compare
		 * would read interior padding. */
		if (memcmp(window->bytes[slotIndex], bytes, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES) != 0)
		{
			if (faultCauseOut != NULL)
			{
				*faultCauseOut = NATIVE_LOCKSTEP_FAULT_CONFLICTING_INPUT;
			}
			return NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT;
		}
		window->duplicateAcceptCount++;
		return NATIVE_LOCKSTEP_INPUT_WINDOW_DUPLICATE;
	}

	/* The fresh and the out-of-order in-window arrivals are deliberately one
	 * path: the ring is indexed by frame, not by arrival order. */
	memcpy(window->bytes[slotIndex], bytes, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	window->bundles[slotIndex] = *bundle;
	window->occupancyMask |= UINT32_C(1) << slotIndex;
	return NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED;
}

enum NativeLockstepInputWindowResult NativeLockstepInputWindow_Take(struct NativeLockstepInputWindow *window, uint32_t frameIndex,
                                                                    struct NativeLockstepBundleV1 *bundleOut)
{
	uint32_t slotIndex;

	if ((window == NULL) || (bundleOut == NULL) || (frameIndex != window->consumedFrame))
	{
		return NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED;
	}

	slotIndex = NativeLockstepInputWindow_SlotIndex(frameIndex);
	if (!NativeLockstepInputWindow_SlotOccupied(window, slotIndex))
	{
		/* Not an error and not latched: the caller must not advance the
		 * simulation and should service the transport and retry. */
		return NATIVE_LOCKSTEP_INPUT_WINDOW_STALL;
	}

	*bundleOut = window->bundles[slotIndex];
	window->occupancyMask &= ~(UINT32_C(1) << slotIndex);
	window->consumedFrame = frameIndex + 1u;
	return NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED;
}

const struct NativeLockstepBundleV1 *NativeLockstepInputWindow_Peek(const struct NativeLockstepInputWindow *window, uint32_t frameIndex)
{
	uint32_t slotIndex;

	if ((window == NULL) || !NativeLockstepInputWindow_InWindow(window, frameIndex))
	{
		return NULL;
	}

	slotIndex = NativeLockstepInputWindow_SlotIndex(frameIndex);
	if (!NativeLockstepInputWindow_SlotOccupied(window, slotIndex))
	{
		return NULL;
	}
	return &window->bundles[slotIndex];
}
