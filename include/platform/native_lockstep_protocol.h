#ifndef PLATFORM_NATIVE_LOCKSTEP_PROTOCOL_H
#define PLATFORM_NATIVE_LOCKSTEP_PROTOCOL_H

#include "platform/native_canonical_codec.h"
#include "platform/native_canonical_state.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-delay lockstep frame bundle, one record per peer per frame.  This
 * header is transport-agnostic by design: it has no OS transport, windowing,
 * clock, or game dependency, and the codec is fixed little-endian, so the wire
 * is identical on any host.  The bundle is a new sibling format; it never
 * reinterprets canonical state, replay, or match-config records.
 */

#define NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC UINT32_C(0x31424c4e) /* Little-endian "NLB1". */
#define NATIVE_LOCKSTEP_BUNDLE_V1_VERSION UINT32_C(1)
#define NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES 128u
#define NATIVE_LOCKSTEP_BUNDLE_V1_DIGEST_OFFSET 120u
#define NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES 8u
#define NATIVE_LOCKSTEP_BUNDLE_V1_RESERVED1_BYTES 4u
#define NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY 2u
#define NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT 0xFFu
/* Mirrors NATIVE_MATCH_CONFIG_V1_SLOT_COUNT without depending on that seam. */
#define NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT 8u

/*
 * Protocol fault causes.  The enum is append-only: they are reported out of
 * the codec and are intended to be latched and logged verbatim, so new causes
 * may only be appended at the end and an existing numeric value must never
 * change.
 *
 * NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD is a local session cause, not a codec
 * one: NativeLockstepBundleV1_Decode never returns it and it is never encoded
 * on the wire.  The lockstep session latches it for a well formed, window
 * accepted record whose verified frame is more than inputDelay frames after
 * the last locally recorded frame (docs/LOCKSTEP_RACE_MILESTONE.md LR-11).
 */
enum NativeLockstepFaultCause
{
	NATIVE_LOCKSTEP_FAULT_NONE = 0,
	NATIVE_LOCKSTEP_FAULT_BAD_MAGIC = 1,
	NATIVE_LOCKSTEP_FAULT_BAD_VERSION = 2,
	NATIVE_LOCKSTEP_FAULT_BAD_SIZE = 3,
	NATIVE_LOCKSTEP_FAULT_BAD_DIGEST = 4,
	NATIVE_LOCKSTEP_FAULT_BAD_RESERVED = 5,
	NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY = 6,
	NATIVE_LOCKSTEP_FAULT_PROTOCOL_VERSION = 7,
	NATIVE_LOCKSTEP_FAULT_INPUT_DELAY = 8,
	NATIVE_LOCKSTEP_FAULT_BAD_SLOT = 9,
	NATIVE_LOCKSTEP_FAULT_BAD_PAD_COUNT = 10,
	NATIVE_LOCKSTEP_FAULT_CONFLICTING_INPUT = 11,
	NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN = 12,
	NATIVE_LOCKSTEP_FAULT_VERIFY_LAG = 13,
	NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE = 14,
	NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD = 15 /* Session-local; never decoded, never on the wire. */
};

/* One owned input slot plus the canonical pad bytes the INPUT domain records. */
struct NativeLockstepBundlePadV1
{
	uint8_t slotIndex;
	struct NativeCanonicalInputPadV1 pad;
};

/*
 * The magic, bundle version, encoded size, and bundle digest are wire-only:
 * they are constants or derived values, so they are not members here.  The
 * reserved fields are members because they are part of the validated record.
 */
struct NativeLockstepBundleV1
{
	uint32_t protocolVersion;
	uint8_t matchIdentity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES];
	uint32_t frameIndex;
	uint32_t inputDelay;
	uint8_t senderSlot;
	uint8_t padCount;
	uint8_t verifiedPresent;
	uint8_t reserved0;
	struct NativeLockstepBundlePadV1 pads[NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY];
	uint32_t verifiedFrameIndex;
	/* Index i is domain NativeCanonicalDomainOrder[i]. */
	uint64_t verifiedDomainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint64_t verifiedCombinedDigest;
	uint8_t reserved1[NATIVE_LOCKSTEP_BUNDLE_V1_RESERVED1_BYTES];
};

size_t NativeLockstepBundleV1_EncodedSize(void);

/*
 * Validates only the sender-local invariants: zero reserved bytes, an in-range
 * sender slot, a pad count within capacity, distinct in-range slot indices for
 * used pad entries, unused entries encoded as slot 0xFF with nine zero bytes,
 * and a well formed verified-block shape: verifiedPresent is 0 or 1, and when
 * it is 0 every verification field is zero (NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE
 * otherwise).  The verified-digest lag invariant is not checked here: it needs
 * the peer-expected input delay, so it is checked only at decode.  Peer
 * expectations (match identity, protocol version, input delay) are likewise
 * checked at decode, not here.
 */
int NativeLockstepBundleV1_Validate(const struct NativeLockstepBundleV1 *bundle);

/*
 * Encode/decode are transactional: on any failure the caller's writer, reader,
 * and output bundle are left untouched.  The record is always exactly
 * NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES bytes and is fully written.  The
 * bundle digest covers bytes 0..119 only and never digests itself.
 */
int NativeLockstepBundleV1_Encode(struct NativeCodecWriter *writer, const struct NativeLockstepBundleV1 *bundle);

/*
 * Accepts exactly one complete record: the reader must hold precisely
 * NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES remaining bytes.  Every failure path
 * reports a distinct enum NativeLockstepFaultCause through faultCauseOut when
 * that pointer is non-NULL; success reports NATIVE_LOCKSTEP_FAULT_NONE.  NULL
 * arguments and any remaining length other than the encoded width report
 * NATIVE_LOCKSTEP_FAULT_BAD_SIZE.  Checks run in wire order: magic, bundle
 * version, encoded size, bundle digest, match identity, protocol version,
 * input delay, the record invariants of NativeLockstepBundleV1_Validate, then
 * the verified-digest lag invariant
 * verifiedFrameIndex + expectedInputDelay + 1 == frameIndex, applied only when
 * verifiedPresent is 1.
 */
int NativeLockstepBundleV1_Decode(struct NativeCodecReader *reader,
                                  const uint8_t expectedMatchIdentity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES],
                                  uint32_t expectedProtocolVersion, uint32_t expectedInputDelay, struct NativeLockstepBundleV1 *bundle,
                                  uint32_t *faultCauseOut);

#endif
