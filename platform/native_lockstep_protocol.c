#include "platform/native_lockstep_protocol.h"

/* Compile-time only: the shared slot count and digest widths are pinned here
 * so a future divergence fails to build.  No link dependency is added. */
#include "platform/native_match_config.h"

#include <string.h>

_Static_assert(NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT == NATIVE_MATCH_CONFIG_V1_SLOT_COUNT,
               "The lockstep bundle slot count must mirror NATIVE_MATCH_CONFIG_V1_SLOT_COUNT.");
_Static_assert(NATIVE_LOCKSTEP_BUNDLE_V1_DIGEST_OFFSET + sizeof(uint64_t) == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES,
               "The bundle digest must be the last field of the encoded record.");
_Static_assert(NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES <= NATIVE_SHA256_DIGEST_BYTES,
               "The match identity is a truncation of the SHA-256 config digest.");

static int NativeLockstepBundle_IsAllZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;

	for (size_t i = 0; i < size; i++)
	{
		combined |= bytes[i];
	}
	return combined == 0;
}

static int NativeLockstepBundle_PadIsAllZero(const struct NativeCanonicalInputPadV1 *pad)
{
	return (pad->status == 0) && (pad->id == 0) && NativeLockstepBundle_IsAllZero(pad->buttons, sizeof(pad->buttons)) &&
	       NativeLockstepBundle_IsAllZero(pad->analog, sizeof(pad->analog)) && (pad->connected == 0);
}

/* Returns NATIVE_LOCKSTEP_FAULT_NONE when the record invariants all hold. */
static uint32_t NativeLockstepBundle_RecordCause(const struct NativeLockstepBundleV1 *bundle)
{
	if (bundle == NULL)
	{
		return NATIVE_LOCKSTEP_FAULT_BAD_SIZE;
	}
	if ((bundle->reserved0 != 0) || !NativeLockstepBundle_IsAllZero(bundle->reserved1, sizeof(bundle->reserved1)))
	{
		return NATIVE_LOCKSTEP_FAULT_BAD_RESERVED;
	}
	if (bundle->senderSlot >= NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT)
	{
		return NATIVE_LOCKSTEP_FAULT_BAD_SLOT;
	}
	if (bundle->padCount > NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY)
	{
		return NATIVE_LOCKSTEP_FAULT_BAD_PAD_COUNT;
	}
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY; i++)
	{
		const struct NativeLockstepBundlePadV1 *entry = &bundle->pads[i];

		if (i >= bundle->padCount)
		{
			/* An unused entry is slot 0xFF followed by nine zero bytes. */
			if ((entry->slotIndex != NATIVE_LOCKSTEP_BUNDLE_PAD_UNUSED_SLOT) || !NativeLockstepBundle_PadIsAllZero(&entry->pad))
			{
				return NATIVE_LOCKSTEP_FAULT_BAD_SLOT;
			}
			continue;
		}
		if (entry->slotIndex >= NATIVE_LOCKSTEP_BUNDLE_SLOT_COUNT)
		{
			return NATIVE_LOCKSTEP_FAULT_BAD_SLOT;
		}
		for (uint32_t j = 0; j < i; j++)
		{
			if (bundle->pads[j].slotIndex == entry->slotIndex)
			{
				return NATIVE_LOCKSTEP_FAULT_BAD_SLOT;
			}
		}
	}
	/* Shape only.  The lag invariant needs the peer input delay, so it lives
	 * in Decode, not here. */
	if (bundle->verifiedPresent > 1)
	{
		return NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE;
	}
	if (bundle->verifiedPresent == 0)
	{
		if ((bundle->verifiedFrameIndex != 0) || (bundle->verifiedCombinedDigest != 0))
		{
			return NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE;
		}
		for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
		{
			if (bundle->verifiedDomainDigests[i] != 0)
			{
				return NATIVE_LOCKSTEP_FAULT_VERIFY_SHAPE;
			}
		}
	}
	return NATIVE_LOCKSTEP_FAULT_NONE;
}

/* Writes offsets 0..119.  The bundle digest is appended by the caller. */
static int NativeLockstepBundle_WriteBody(struct NativeCodecWriter *writer, const struct NativeLockstepBundleV1 *bundle)
{
	if (!NativeCodecWriter_WriteU32(writer, NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU32(writer, NATIVE_LOCKSTEP_BUNDLE_V1_VERSION) ||
	    !NativeCodecWriter_WriteU32(writer, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteU32(writer, bundle->protocolVersion) ||
	    !NativeCodecWriter_WriteBytes(writer, bundle->matchIdentity, sizeof(bundle->matchIdentity)) ||
	    !NativeCodecWriter_WriteU32(writer, bundle->frameIndex) || !NativeCodecWriter_WriteU32(writer, bundle->inputDelay) ||
	    !NativeCodecWriter_WriteU8(writer, bundle->senderSlot) || !NativeCodecWriter_WriteU8(writer, bundle->padCount) ||
	    !NativeCodecWriter_WriteU8(writer, bundle->verifiedPresent) || !NativeCodecWriter_WriteU8(writer, bundle->reserved0))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY; i++)
	{
		const struct NativeLockstepBundlePadV1 *entry = &bundle->pads[i];
		if (!NativeCodecWriter_WriteU8(writer, entry->slotIndex) || !NativeCodecWriter_WriteU8(writer, entry->pad.status) ||
		    !NativeCodecWriter_WriteU8(writer, entry->pad.id) ||
		    !NativeCodecWriter_WriteBytes(writer, entry->pad.buttons, sizeof(entry->pad.buttons)) ||
		    !NativeCodecWriter_WriteBytes(writer, entry->pad.analog, sizeof(entry->pad.analog)) ||
		    !NativeCodecWriter_WriteU8(writer, entry->pad.connected))
		{
			return 0;
		}
	}
	if (!NativeCodecWriter_WriteU32(writer, bundle->verifiedFrameIndex))
	{
		return 0;
	}
	/* Index i is domain NativeCanonicalDomainOrder[i]. */
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		if (!NativeCodecWriter_WriteU64(writer, bundle->verifiedDomainDigests[i]))
		{
			return 0;
		}
	}
	if (!NativeCodecWriter_WriteU64(writer, bundle->verifiedCombinedDigest) ||
	    !NativeCodecWriter_WriteBytes(writer, bundle->reserved1, sizeof(bundle->reserved1)))
	{
		return 0;
	}
	return 1;
}

static int NativeLockstepBundle_ReadBody(struct NativeCodecReader *reader, struct NativeLockstepBundleV1 *bundle, uint32_t *magic,
                                         uint32_t *bundleVersion, uint32_t *encodedSize, uint64_t *bundleDigest)
{
	if (!NativeCodecReader_ReadU32(reader, magic) || !NativeCodecReader_ReadU32(reader, bundleVersion) ||
	    !NativeCodecReader_ReadU32(reader, encodedSize) || !NativeCodecReader_ReadU32(reader, &bundle->protocolVersion) ||
	    !NativeCodecReader_ReadBytes(reader, bundle->matchIdentity, sizeof(bundle->matchIdentity)) ||
	    !NativeCodecReader_ReadU32(reader, &bundle->frameIndex) || !NativeCodecReader_ReadU32(reader, &bundle->inputDelay) ||
	    !NativeCodecReader_ReadU8(reader, &bundle->senderSlot) || !NativeCodecReader_ReadU8(reader, &bundle->padCount) ||
	    !NativeCodecReader_ReadU8(reader, &bundle->verifiedPresent) || !NativeCodecReader_ReadU8(reader, &bundle->reserved0))
	{
		return 0;
	}
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY; i++)
	{
		struct NativeLockstepBundlePadV1 *entry = &bundle->pads[i];
		if (!NativeCodecReader_ReadU8(reader, &entry->slotIndex) || !NativeCodecReader_ReadU8(reader, &entry->pad.status) ||
		    !NativeCodecReader_ReadU8(reader, &entry->pad.id) ||
		    !NativeCodecReader_ReadBytes(reader, entry->pad.buttons, sizeof(entry->pad.buttons)) ||
		    !NativeCodecReader_ReadBytes(reader, entry->pad.analog, sizeof(entry->pad.analog)) ||
		    !NativeCodecReader_ReadU8(reader, &entry->pad.connected))
		{
			return 0;
		}
	}
	if (!NativeCodecReader_ReadU32(reader, &bundle->verifiedFrameIndex))
	{
		return 0;
	}
	/* Index i is domain NativeCanonicalDomainOrder[i]. */
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DOMAIN_COUNT; i++)
	{
		if (!NativeCodecReader_ReadU64(reader, &bundle->verifiedDomainDigests[i]))
		{
			return 0;
		}
	}
	if (!NativeCodecReader_ReadU64(reader, &bundle->verifiedCombinedDigest) ||
	    !NativeCodecReader_ReadBytes(reader, bundle->reserved1, sizeof(bundle->reserved1)) ||
	    !NativeCodecReader_ReadU64(reader, bundleDigest))
	{
		return 0;
	}
	return 1;
}

size_t NativeLockstepBundleV1_EncodedSize(void)
{
	return NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES;
}

int NativeLockstepBundleV1_Validate(const struct NativeLockstepBundleV1 *bundle)
{
	return NativeLockstepBundle_RecordCause(bundle) == NATIVE_LOCKSTEP_FAULT_NONE;
}

int NativeLockstepBundleV1_Encode(struct NativeCodecWriter *writer, const struct NativeLockstepBundleV1 *bundle)
{
	uint8_t bytes[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter staged;
	struct NativeCodecWriter encoded;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES > writer->capacity - writer->offset) || !NativeLockstepBundleV1_Validate(bundle))
	{
		return 0;
	}

	/* The digest covers the body only, so it is taken before it is written. */
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&staged, bytes, sizeof(bytes), &digest);
	if (!NativeLockstepBundle_WriteBody(&staged, bundle) || (NativeCodecWriter_Size(&staged) != NATIVE_LOCKSTEP_BUNDLE_V1_DIGEST_OFFSET))
	{
		return 0;
	}
	staged.digest = NULL;
	if (!NativeCodecWriter_WriteU64(&staged, digest.value) || (NativeCodecWriter_Size(&staged) != sizeof(bytes)))
	{
		return 0;
	}

	encoded = *writer;
	if (!NativeCodecWriter_WriteBytes(&encoded, bytes, sizeof(bytes)))
	{
		return 0;
	}
	*writer = encoded;
	return 1;
}

int NativeLockstepBundleV1_Decode(struct NativeCodecReader *reader,
                                  const uint8_t expectedMatchIdentity[NATIVE_LOCKSTEP_BUNDLE_V1_MATCH_IDENTITY_BYTES],
                                  uint32_t expectedProtocolVersion, uint32_t expectedInputDelay, struct NativeLockstepBundleV1 *bundle,
                                  uint32_t *faultCauseOut)
{
	struct NativeCodecReader encoded;
	struct NativeLockstepBundleV1 decoded;
	struct NativeCodecDigest64 digest;
	uint32_t magic = 0;
	uint32_t bundleVersion = 0;
	uint32_t encodedSize = 0;
	uint32_t cause;
	uint64_t bundleDigest = 0;

	if ((reader == NULL) || (expectedMatchIdentity == NULL) || (bundle == NULL) || !NativeCodecReader_Ok(reader) ||
	    (NativeCodecReader_Remaining(reader) != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES))
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = NATIVE_LOCKSTEP_FAULT_BAD_SIZE;
		}
		return 0;
	}

	memset(&decoded, 0, sizeof(decoded));
	encoded = *reader;
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, &encoded.data[encoded.offset], NATIVE_LOCKSTEP_BUNDLE_V1_DIGEST_OFFSET);
	if (!NativeLockstepBundle_ReadBody(&encoded, &decoded, &magic, &bundleVersion, &encodedSize, &bundleDigest) ||
	    (NativeCodecReader_Remaining(&encoded) != 0))
	{
		cause = NATIVE_LOCKSTEP_FAULT_BAD_SIZE;
	}
	else if (magic != NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC)
	{
		cause = NATIVE_LOCKSTEP_FAULT_BAD_MAGIC;
	}
	else if (bundleVersion != NATIVE_LOCKSTEP_BUNDLE_V1_VERSION)
	{
		cause = NATIVE_LOCKSTEP_FAULT_BAD_VERSION;
	}
	else if (encodedSize != NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
	{
		cause = NATIVE_LOCKSTEP_FAULT_BAD_SIZE;
	}
	else if (bundleDigest != digest.value)
	{
		cause = NATIVE_LOCKSTEP_FAULT_BAD_DIGEST;
	}
	else if (memcmp(decoded.matchIdentity, expectedMatchIdentity, sizeof(decoded.matchIdentity)) != 0)
	{
		cause = NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY;
	}
	else if (decoded.protocolVersion != expectedProtocolVersion)
	{
		cause = NATIVE_LOCKSTEP_FAULT_PROTOCOL_VERSION;
	}
	else if (decoded.inputDelay != expectedInputDelay)
	{
		cause = NATIVE_LOCKSTEP_FAULT_INPUT_DELAY;
	}
	else
	{
		cause = NativeLockstepBundle_RecordCause(&decoded);
		/*
		 * The verified digest lags the carried input frame by exactly D + 1
		 * frames, so a present digest must satisfy
		 * verifiedFrameIndex + D + 1 == frameIndex.  The arithmetic is 64-bit
		 * so a hostile or early-session verifiedFrameIndex cannot wrap a
		 * uint32 into a false pass.  With verifiedPresent 0 there is no digest
		 * to lag-check: that is the first D + 1 frames of a session.
		 */
		if ((cause == NATIVE_LOCKSTEP_FAULT_NONE) && (decoded.verifiedPresent == 1) &&
		    (((uint64_t)decoded.verifiedFrameIndex + expectedInputDelay + 1u) != (uint64_t)decoded.frameIndex))
		{
			cause = NATIVE_LOCKSTEP_FAULT_VERIFY_LAG;
		}
	}

	if (cause != NATIVE_LOCKSTEP_FAULT_NONE)
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = cause;
		}
		return 0;
	}
	*reader = encoded;
	*bundle = decoded;
	if (faultCauseOut != NULL)
	{
		*faultCauseOut = NATIVE_LOCKSTEP_FAULT_NONE;
	}
	return 1;
}
