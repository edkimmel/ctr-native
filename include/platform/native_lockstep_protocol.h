#ifndef PLATFORM_NATIVE_LOCKSTEP_PROTOCOL_H
#define PLATFORM_NATIVE_LOCKSTEP_PROTOCOL_H

#include "platform/native_canonical_codec.h"
#include "platform/native_canonical_state.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed-delay lockstep frame bundle, one record per peer per frame.  This
 * header is transport-agnostic by design: it has no socket, OS networking,
 * SDL, clock, or game dependency, and the codec is fixed little-endian, so the
 * wire is identical on any host.  The bundle is a new sibling format; it never
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
 * Protocol fault causes.  Values are frozen: they are reported out of the
 * codec and are intended to be latched and logged verbatim.
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
	NATIVE_LOCKSTEP_FAULT_VERIFY_LAG = 13
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
	uint64_t verifiedDomainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
	uint64_t verifiedCombinedDigest;
	uint8_t reserved1[NATIVE_LOCKSTEP_BUNDLE_V1_RESERVED1_BYTES];
};

size_t NativeLockstepBundleV1_EncodedSize(void);

/*
 * Validates only the sender-local invariants: zero reserved bytes, an in-range
 * sender slot, a pad count within capacity, distinct in-range slot indices for
 * used pad entries, unused entries encoded as slot 0xFF with nine zero bytes,
 * and zero verification fields whenever verifiedPresent is 0.  Peer
 * expectations (match identity, protocol version, input delay) are checked at
 * decode, not here.
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
 * input delay, then the record invariants of NativeLockstepBundleV1_Validate.
 */
int NativeLockstepBundleV1_Decode(struct NativeCodecReader *reader,
                                  const uint8_t expectedMatchIdentity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES],
                                  uint32_t expectedProtocolVersion, uint32_t expectedInputDelay, struct NativeLockstepBundleV1 *bundle,
                                  uint32_t *faultCauseOut);

#endif
