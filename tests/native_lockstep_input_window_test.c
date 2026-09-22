#include "platform/native_lockstep_input_window.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define BUNDLE_BYTES NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES
#define CAPACITY ((uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY)
/* Any cause value the module never writes, so "untouched" is observable. */
#define CAUSE_SENTINEL UINT32_C(0xA5A5A5A5)

static const uint8_t g_identity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES] = {
	0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u
};
static const uint32_t g_protocolVersion = UINT32_C(0x01020304);
static const uint32_t g_inputDelay = 2u;

/* One genuine wire record plus the decoded bundle it encodes. */
struct Record
{
	struct NativeLockstepBundleV1 bundle;
	uint8_t bytes[BUNDLE_BYTES];
};

/*
 * The frame index and the variant both reach the pad bytes, so every record in
 * this test is distinguishable on the wire and two variants of one frame differ
 * only in their pad bytes.  verifiedPresent stays 0, so no verified-digest lag
 * invariant applies to any frame index used here.
 */
static int MakeRecord(struct Record *record, uint32_t frameIndex, uint8_t variant)
{
	struct NativeLockstepBundleV1 *bundle = &record->bundle;
	struct NativeCodecWriter writer;

	memset(record, 0, sizeof(*record));
	bundle->protocolVersion = g_protocolVersion;
	memcpy(bundle->matchIdentity, g_identity, sizeof(bundle->matchIdentity));
	bundle->frameIndex = frameIndex;
	bundle->inputDelay = g_inputDelay;
	bundle->senderSlot = 1u;
	bundle->padCount = 1u;
	bundle->pads[0].slotIndex = 1u;
	bundle->pads[0].pad.status = 0x5Au;
	bundle->pads[0].pad.id = 0x01u;
	bundle->pads[0].pad.buttons[0] = (uint8_t)(frameIndex & 0xFFu);
	bundle->pads[0].pad.buttons[1] = variant;
	bundle->pads[0].pad.analog[0] = (uint8_t)((frameIndex >> 8u) & 0xFFu);
	bundle->pads[0].pad.connected = 1u;
	bundle->pads[1].slotIndex = NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT;

	NativeCodecWriter_Init(&writer, record->bytes, sizeof(record->bytes), NULL);
	CHECK(NativeLockstepBundleV1_Encode(&writer, bundle));
	CHECK(NativeCodecWriter_Size(&writer) == BUNDLE_BYTES);
	return 0;
}

static enum NativeLockstepInputWindowResult OfferRecord(struct NativeLockstepInputWindow *window, const struct Record *record,
                                                        uint32_t *faultCauseOut)
{
	return NativeLockstepInputWindow_Offer(window, record->bytes, sizeof(record->bytes), &record->bundle, faultCauseOut);
}

/*
 * Re-encodes a bundle and compares the wire records, which avoids the interior
 * padding a struct compare would read.
 */
static int SameAsRecord(const struct NativeLockstepBundleV1 *bundle, const struct Record *record)
{
	struct NativeCodecWriter writer;
	uint8_t bytes[BUNDLE_BYTES];

	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(NativeLockstepBundleV1_Encode(&writer, bundle));
	CHECK(memcmp(bytes, record->bytes, sizeof(bytes)) == 0);
	return 0;
}

/* Byte snapshots, in the style of tests/native_virtual_datagram_test.c:8-18. */
static void Snapshot(struct NativeLockstepInputWindow *destination, const struct NativeLockstepInputWindow *window)
{
	memcpy(destination, window, sizeof(*destination));
}

static int Unchanged(const struct NativeLockstepInputWindow *window, const struct NativeLockstepInputWindow *before)
{
	return memcmp(window, before, sizeof(*window)) == 0;
}

/* Fresh in-window accept, then the frame comes straight back out. */
static int TestFreshAccept(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepBundleV1 taken;
	struct Record record;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(MakeRecord(&record, 0u, 1u) == 0);
	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(window.consumedFrame == 0u);
	CHECK(window.occupancyMask == 0u);
	CHECK(window.inputDelay == g_inputDelay);

	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(window.occupancyMask == 1u);
	CHECK(memcmp(window.bytes[0], record.bytes, BUNDLE_BYTES) == 0);
	CHECK(window.staleDropCount == 0u);
	CHECK(window.duplicateAcceptCount == 0u);

	memset(&taken, 0xCD, sizeof(taken));
	CHECK(NativeLockstepInputWindow_Take(&window, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(SameAsRecord(&taken, &record) == 0);
	CHECK(window.consumedFrame == 1u);
	CHECK(window.occupancyMask == 0u);
	/* A NULL fault sink is accepted on every non-fault path. */
	CHECK(MakeRecord(&record, 1u, 2u) == 0);
	CHECK(OfferRecord(&window, &record, NULL) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	return 0;
}

/* Out of order in: N+2, N+1, N.  In order out: N, N+1, N+2. */
static int TestOutOfOrderAccept(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepBundleV1 taken;
	struct Record records[3];
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	for (uint32_t i = 0; i < 3u; i++)
	{
		CHECK(MakeRecord(&records[i], i, (uint8_t)(0x10u + i)) == 0);
	}
	CHECK(OfferRecord(&window, &records[2], &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(OfferRecord(&window, &records[1], &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(OfferRecord(&window, &records[0], &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(window.occupancyMask == 7u);
	CHECK(window.consumedFrame == 0u);

	for (uint32_t i = 0; i < 3u; i++)
	{
		memset(&taken, 0xCD, sizeof(taken));
		CHECK(NativeLockstepInputWindow_Take(&window, i, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
		CHECK(taken.frameIndex == i);
		CHECK(SameAsRecord(&taken, &records[i]) == 0);
		CHECK(window.consumedFrame == i + 1u);
	}
	CHECK(window.occupancyMask == 0u);
	return 0;
}

/* Byte-identical re-delivery is a no-op; a differing one is a protocol fault. */
static int TestDuplicates(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct NativeLockstepInputWindow after;
	struct Record record;
	struct Record clone;
	struct Record differing;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(MakeRecord(&record, 3u, 1u) == 0);
	CHECK(MakeRecord(&clone, 3u, 1u) == 0);
	CHECK(MakeRecord(&differing, 3u, 2u) == 0);
	/* Same frame, different pad bytes, so the records really do differ. */
	CHECK(memcmp(clone.bytes, record.bytes, BUNDLE_BYTES) == 0);
	CHECK(memcmp(differing.bytes, record.bytes, BUNDLE_BYTES) != 0);
	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);

	Snapshot(&before, &window);
	CHECK(OfferRecord(&window, &clone, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_DUPLICATE);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(window.duplicateAcceptCount == 1u);
	/* Only the duplicate counter moved: the stored bytes are still the first
	 * delivery's, byte for byte. */
	Snapshot(&after, &window);
	after.duplicateAcceptCount = before.duplicateAcceptCount;
	CHECK(Unchanged(&after, &before));
	CHECK(memcmp(window.bytes[3], record.bytes, BUNDLE_BYTES) == 0);

	Snapshot(&before, &window);
	CHECK(OfferRecord(&window, &differing, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT);
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_CONFLICTING_INPUT);
	CHECK(Unchanged(&window, &before));
	CHECK(memcmp(window.bytes[3], record.bytes, BUNDLE_BYTES) == 0);
	CHECK(window.duplicateAcceptCount == 1u);
	return 0;
}

/* A consumed frame re-delivered is a drop, not an error. */
static int TestStaleDrop(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct NativeLockstepInputWindow after;
	struct NativeLockstepBundleV1 taken;
	struct Record record;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(MakeRecord(&record, 0u, 1u) == 0);
	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(NativeLockstepInputWindow_Take(&window, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(window.consumedFrame == 1u);

	Snapshot(&before, &window);
	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALE);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(window.staleDropCount == 1u);
	CHECK(window.occupancyMask == 0u);
	CHECK(window.consumedFrame == 1u);
	Snapshot(&after, &window);
	after.staleDropCount = before.staleDropCount;
	CHECK(Unchanged(&after, &before));
	return 0;
}

/*
 * The acceptance window is [consumedFrame, consumedFrame + capacity - 1]:
 * inclusive at the top, and one frame past it changes nothing at all.
 */
static int TestWindowBounds(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct Record last;
	struct Record overrun;
	struct Record farFuture;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(MakeRecord(&last, CAPACITY - 1u, 1u) == 0);
	CHECK(MakeRecord(&overrun, CAPACITY, 2u) == 0);
	CHECK(MakeRecord(&farFuture, UINT32_C(0xFFFFFFFE), 3u) == 0);

	CHECK(OfferRecord(&window, &last, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(window.occupancyMask == (UINT32_C(1) << (CAPACITY - 1u)));

	Snapshot(&before, &window);
	CHECK(OfferRecord(&window, &overrun, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT);
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
	/* Not even a counter moved: the ring never grows and never evicts. */
	CHECK(Unchanged(&window, &before));

	/* A frame index near UINT32_MAX must fault, not wrap into the window. */
	cause = CAUSE_SENTINEL;
	CHECK(OfferRecord(&window, &farFuture, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT);
	CHECK(cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
	CHECK(Unchanged(&window, &before));
	/* The overrun path with a NULL fault sink still changes nothing. */
	CHECK(OfferRecord(&window, &overrun, NULL) == NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT);
	CHECK(Unchanged(&window, &before));
	return 0;
}

/*
 * The same bounds at the top of the frame range.  consumedFrame is seeded
 * directly because the struct is caller-owned and reaching frame 0xFFFFFFF8 by
 * consumption would take four billion iterations.
 */
static int TestHighFrameBounds(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepBundleV1 taken;
	struct Record first;
	struct Record last;
	struct Record belowWindow;
	struct Record wrapped;
	uint32_t base = UINT32_MAX - (CAPACITY - 1u);
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	window.consumedFrame = base;
	CHECK(MakeRecord(&first, base, 1u) == 0);
	CHECK(MakeRecord(&last, UINT32_MAX, 2u) == 0);
	CHECK(MakeRecord(&belowWindow, base - 1u, 3u) == 0);
	CHECK(MakeRecord(&wrapped, 0u, 4u) == 0);

	/* The whole window, up to and including the last representable frame. */
	CHECK(OfferRecord(&window, &first, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(OfferRecord(&window, &last, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(cause == CAUSE_SENTINEL);
	/* Below consumedFrame is stale even at the top of the range, and frame 0,
	 * which a 32-bit upper bound would have wrapped into the window, is not
	 * accepted. */
	CHECK(OfferRecord(&window, &belowWindow, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALE);
	CHECK(OfferRecord(&window, &wrapped, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALE);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(window.staleDropCount == 2u);
	CHECK(NativeLockstepInputWindow_Peek(&window, 0u) == NULL);

	CHECK(NativeLockstepInputWindow_Take(&window, base, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(SameAsRecord(&taken, &first) == 0);
	CHECK(window.consumedFrame == base + 1u);
	CHECK(OfferRecord(&window, &wrapped, NULL) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALE);
	CHECK(window.staleDropCount == 3u);
	CHECK(NativeLockstepInputWindow_Peek(&window, UINT32_MAX) != NULL);
	CHECK(SameAsRecord(NativeLockstepInputWindow_Peek(&window, UINT32_MAX), &last) == 0);
	return 0;
}

/* An empty slot stalls; it is not an error and nothing is latched. */
static int TestStall(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct NativeLockstepBundleV1 taken;
	struct Record record;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	memset(&taken, 0xAA, sizeof(taken));
	Snapshot(&before, &window);
	CHECK(NativeLockstepInputWindow_Take(&window, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALL);
	CHECK(window.consumedFrame == 0u);
	CHECK(Unchanged(&window, &before));
	/* Nothing was latched into the caller's bundle either. */
	for (size_t i = 0; i < sizeof(taken); i++)
	{
		CHECK(((const uint8_t *)&taken)[i] == 0xAAu);
	}

	/* A frame arriving later clears the stall; the retry then succeeds. */
	CHECK(MakeRecord(&record, 0u, 1u) == 0);
	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(NativeLockstepInputWindow_Take(&window, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(SameAsRecord(&taken, &record) == 0);

	/* The next frame has not arrived, so consumption stalls again. */
	Snapshot(&before, &window);
	CHECK(NativeLockstepInputWindow_Take(&window, 1u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALL);
	CHECK(Unchanged(&window, &before));
	return 0;
}

/* Only consumedFrame may be taken, and a refusal moves nothing. */
static int TestTakeRejects(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct NativeLockstepBundleV1 taken;
	struct Record records[2];
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(MakeRecord(&records[0], 0u, 1u) == 0);
	CHECK(MakeRecord(&records[1], 1u, 2u) == 0);
	CHECK(OfferRecord(&window, &records[0], &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(OfferRecord(&window, &records[1], &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);

	Snapshot(&before, &window);
	memset(&taken, 0xAA, sizeof(taken));
	/* An occupied but out-of-turn frame, a frame past the window, and NULLs. */
	CHECK(NativeLockstepInputWindow_Take(&window, 1u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Take(&window, CAPACITY + 4u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Take(&window, 0u, NULL) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Take(NULL, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(window.consumedFrame == 0u);
	CHECK(Unchanged(&window, &before));
	for (size_t i = 0; i < sizeof(taken); i++)
	{
		CHECK(((const uint8_t *)&taken)[i] == 0xAAu);
	}

	CHECK(NativeLockstepInputWindow_Take(&window, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(window.consumedFrame == 1u);
	return 0;
}

/*
 * Fill to capacity, drain, refill across the wrap point: the modular index has
 * to land every frame, and a slot reused after consumption must not look like a
 * duplicate of the frame that used to live there.
 */
static int TestFillDrainWrap(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct NativeLockstepBundleV1 taken;
	struct Record records[2u * NATIVE_LOCKSTEP_RING_CAPACITY];
	struct Record overrun;
	uint32_t cause = CAUSE_SENTINEL;
	uint32_t total = 2u * CAPACITY;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	for (uint32_t frame = 0; frame < total; frame++)
	{
		CHECK(MakeRecord(&records[frame], frame, (uint8_t)(0x20u + frame)) == 0);
	}

	for (uint32_t pass = 0; pass < 2u; pass++)
	{
		uint32_t base = pass * CAPACITY;

		for (uint32_t i = 0; i < CAPACITY; i++)
		{
			/* Pass 1 reuses slots that still hold pass 0's bytes. */
			CHECK(OfferRecord(&window, &records[base + i], &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
			CHECK(memcmp(window.bytes[(base + i) % CAPACITY], records[base + i].bytes, BUNDLE_BYTES) == 0);
		}
		CHECK(window.occupancyMask == 0xFFu);
		CHECK(window.consumedFrame == base);

		/* The window is full, so the next frame is an overrun, and every stored
		 * frame is still readable. */
		Snapshot(&before, &window);
		CHECK(MakeRecord(&overrun, base + CAPACITY, 0x70u) == 0);
		CHECK(OfferRecord(&window, &overrun, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_FAULT);
		CHECK(cause == NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN);
		CHECK(Unchanged(&window, &before));
		cause = CAUSE_SENTINEL;

		for (uint32_t i = 0; i < CAPACITY; i++)
		{
			memset(&taken, 0xCD, sizeof(taken));
			CHECK(NativeLockstepInputWindow_Take(&window, base + i, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
			CHECK(taken.frameIndex == base + i);
			CHECK(SameAsRecord(&taken, &records[base + i]) == 0);
		}
		CHECK(window.occupancyMask == 0u);
		CHECK(window.consumedFrame == base + CAPACITY);
	}
	CHECK(window.consumedFrame == total);
	CHECK(window.duplicateAcceptCount == 0u);
	CHECK(window.staleDropCount == 0u);
	return 0;
}

/* Peek reads and only reads. */
static int TestPeek(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct NativeLockstepBundleV1 taken;
	const struct NativeLockstepBundleV1 *peeked;
	struct Record record;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(NativeLockstepInputWindow_Peek(&window, 0u) == NULL);
	CHECK(NativeLockstepInputWindow_Peek(NULL, 0u) == NULL);
	CHECK(MakeRecord(&record, 2u, 1u) == 0);
	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);

	Snapshot(&before, &window);
	peeked = NativeLockstepInputWindow_Peek(&window, 2u);
	CHECK(peeked != NULL);
	CHECK(peeked->frameIndex == 2u);
	CHECK(SameAsRecord(peeked, &record) == 0);
	/* Empty in-window slot, and one frame past the window. */
	CHECK(NativeLockstepInputWindow_Peek(&window, 3u) == NULL);
	CHECK(NativeLockstepInputWindow_Peek(&window, CAPACITY) == NULL);
	CHECK(Unchanged(&window, &before));

	/* A consumed frame reads back as NULL even though slot 0 still holds its
	 * bytes, and the still-stored frame 2 is unaffected. */
	CHECK(MakeRecord(&record, 0u, 2u) == 0);
	CHECK(OfferRecord(&window, &record, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(NativeLockstepInputWindow_Take(&window, 0u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	CHECK(NativeLockstepInputWindow_Peek(&window, 0u) == NULL);
	CHECK(NativeLockstepInputWindow_Peek(&window, 2u) != NULL);
	CHECK(NativeLockstepInputWindow_Take(&window, 1u, &taken) == NATIVE_LOCKSTEP_INPUT_WINDOW_STALL);
	Snapshot(&before, &window);
	CHECK(NativeLockstepInputWindow_Peek(&window, 1u) == NULL);
	CHECK(Unchanged(&window, &before));
	return 0;
}

/* Init bounds, and a rejected Init leaves the caller's struct alone. */
static int TestInitBounds(void)
{
	struct NativeLockstepInputWindow window;
	uint8_t pattern[sizeof(struct NativeLockstepInputWindow)];

	CHECK(NativeLockstepInputWindow_Init(NULL, g_inputDelay) == 0);

	memset(pattern, 0x5Au, sizeof(pattern));
	memset(&window, 0x5Au, sizeof(window));
	CHECK(NativeLockstepInputWindow_Init(&window, 0u) == 0);
	CHECK(memcmp(&window, pattern, sizeof(window)) == 0);
	CHECK(NativeLockstepInputWindow_Init(&window, NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1u) == 0);
	CHECK(memcmp(&window, pattern, sizeof(window)) == 0);
	CHECK(NativeLockstepInputWindow_Init(&window, UINT32_MAX) == 0);
	CHECK(memcmp(&window, pattern, sizeof(window)) == 0);

	for (uint32_t delay = NATIVE_LOCKSTEP_MIN_INPUT_DELAY; delay <= NATIVE_LOCKSTEP_MAX_INPUT_DELAY; delay++)
	{
		memset(&window, 0x5Au, sizeof(window));
		CHECK(NativeLockstepInputWindow_Init(&window, delay) == 1);
		CHECK(window.inputDelay == delay);
		CHECK(window.consumedFrame == 0u);
		CHECK(window.occupancyMask == 0u);
		CHECK(window.staleDropCount == 0u);
		CHECK(window.duplicateAcceptCount == 0u);
		/* Init zeroes the whole struct, so the ring is zero too. */
		for (size_t i = 0; i < sizeof(window.bytes); i++)
		{
			CHECK(((const uint8_t *)window.bytes)[i] == 0u);
		}
	}
	/*
	 * The inputDelay + 1 > capacity clause is unreachable at runtime while
	 * capacity is 8 and the maximum delay is 6: the range check rejects every
	 * such delay first.  The relationship is enforced at compile time by the
	 * _Static_assert in platform/native_lockstep_input_window.c, which is
	 * asserted here as a constant expression rather than faked at runtime.
	 */
	CHECK(NATIVE_LOCKSTEP_RING_CAPACITY >= NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1);
	CHECK(NATIVE_LOCKSTEP_MIN_INPUT_DELAY == 1);
	CHECK(CAPACITY == 8u);
	return 0;
}

/* Caller misuse of Offer: wrong widths and NULLs, all non-mutating. */
static int TestOfferRejects(void)
{
	struct NativeLockstepInputWindow window;
	struct NativeLockstepInputWindow before;
	struct Record record;
	uint32_t cause = CAUSE_SENTINEL;

	CHECK(NativeLockstepInputWindow_Init(&window, g_inputDelay) == 1);
	CHECK(MakeRecord(&record, 0u, 1u) == 0);
	Snapshot(&before, &window);

	CHECK(NativeLockstepInputWindow_Offer(&window, record.bytes, BUNDLE_BYTES - 1u, &record.bundle, &cause) ==
	      NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Offer(&window, record.bytes, BUNDLE_BYTES + 1u, &record.bundle, &cause) ==
	      NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Offer(&window, record.bytes, 0u, &record.bundle, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Offer(&window, NULL, BUNDLE_BYTES, &record.bundle, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Offer(&window, record.bytes, BUNDLE_BYTES, NULL, &cause) == NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(NativeLockstepInputWindow_Offer(NULL, record.bytes, BUNDLE_BYTES, &record.bundle, &cause) ==
	      NATIVE_LOCKSTEP_INPUT_WINDOW_REJECTED);
	CHECK(cause == CAUSE_SENTINEL);
	CHECK(Unchanged(&window, &before));

	CHECK(NativeLockstepInputWindow_Offer(&window, record.bytes, BUNDLE_BYTES, &record.bundle, &cause) ==
	      NATIVE_LOCKSTEP_INPUT_WINDOW_ACCEPTED);
	return 0;
}

int main(void)
{
	CHECK(BUNDLE_BYTES == 128u);
	CHECK(TestFreshAccept() == 0);
	CHECK(TestOutOfOrderAccept() == 0);
	CHECK(TestDuplicates() == 0);
	CHECK(TestStaleDrop() == 0);
	CHECK(TestWindowBounds() == 0);
	CHECK(TestHighFrameBounds() == 0);
	CHECK(TestStall() == 0);
	CHECK(TestTakeRejects() == 0);
	CHECK(TestFillDrainWrap() == 0);
	CHECK(TestPeek() == 0);
	CHECK(TestInitBounds() == 0);
	CHECK(TestOfferRejects() == 0);
	return 0;
}
