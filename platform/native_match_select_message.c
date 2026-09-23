#include "platform/native_match_select_message.h"

#include <string.h>

_Static_assert(NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET + sizeof(uint64_t) == NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES,
               "The select message digest must be the last field of the encoded record.");
_Static_assert(NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET ==
                   4u + 2u + 2u + 1u + 1u + 1u + 1u + 4u + NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES + 8u + 1u + 1u + 1u + 1u +
                       NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED0_BYTES + NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES +
                       NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED1_BYTES,
               "The digest offset must equal the sum of every field written before it.");
_Static_assert(NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES == 8u, "The wire layout reserves exactly 8 bytes for resolvedDigest.");
_Static_assert(NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES <= UINT16_MAX, "encodedSize is a u16 on the wire.");
_Static_assert(NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION <= UINT16_MAX, "messageVersion is a u16 on the wire.");

static int NativeMatchSelectMessage_IsAllZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;

	for (size_t i = 0; i < size; i++)
	{
		combined |= bytes[i];
	}
	return combined == 0;
}

uint32_t NativeMatchSelectMessageV1_ShapeCause(const struct NativeMatchSelectMessageV1 *message)
{
	uint32_t index = 0;

	if (message == NULL)
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE;
	}
	if (!NativeMatchSelectMessage_IsAllZero(message->reserved0, sizeof(message->reserved0)) ||
	    !NativeMatchSelectMessage_IsAllZero(message->reserved1, sizeof(message->reserved1)))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESERVED;
	}
	if ((message->humanCount < 1u) || (message->humanCount > NATIVE_MATCH_SELECT_MAX_HUMANS))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_HUMAN_COUNT;
	}
	if (message->senderHuman >= message->humanCount)
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SENDER;
	}
	if ((message->phase != NATIVE_MATCH_SELECT_PHASE_PICKING) && (message->phase != NATIVE_MATCH_SELECT_PHASE_RESOLVED))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_PHASE;
	}
	if (message->lockMask > (NATIVE_MATCH_SELECT_LOCK_CHARACTER | NATIVE_MATCH_SELECT_LOCK_TRACK | NATIVE_MATCH_SELECT_LOCK_LAPS))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LOCK_MASK;
	}
	if (message->sequence < 1u)
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SEQUENCE;
	}
	if (!NativeMatchSelect_CharacterIndex(message->characterID, &index))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_CHARACTER;
	}
	if (!NativeMatchSelect_TrackIndex(message->trackID, &index))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_TRACK;
	}
	if (!NativeMatchSelect_LapOptionIndex(message->lapCount, &index))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LAPS;
	}
	if (message->currentItem > NATIVE_MATCH_SELECT_ITEM_DONE)
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM;
	}
	if (message->lockMask != (uint8_t)((1u << message->currentItem) - 1u))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM_LOCK_MISMATCH;
	}
	if ((message->phase == NATIVE_MATCH_SELECT_PHASE_PICKING) &&
	    !NativeMatchSelectMessage_IsAllZero(message->resolvedDigest, sizeof(message->resolvedDigest)))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_DIGEST;
	}
	if ((message->phase == NATIVE_MATCH_SELECT_PHASE_RESOLVED) && (message->currentItem != NATIVE_MATCH_SELECT_ITEM_DONE))
	{
		return NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_ITEM;
	}
	return NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE;
}

/* Writes offsets 0..55. The trailer digest is appended by the caller. */
static int NativeMatchSelectMessage_WriteBody(struct NativeCodecWriter *writer, const struct NativeMatchSelectMessageV1 *message)
{
	if (!NativeCodecWriter_WriteU32(writer, NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU16(writer, (uint16_t)NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION) ||
	    !NativeCodecWriter_WriteU16(writer, (uint16_t)NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteU8(writer, message->senderHuman) || !NativeCodecWriter_WriteU8(writer, message->humanCount) ||
	    !NativeCodecWriter_WriteU8(writer, message->phase) || !NativeCodecWriter_WriteU8(writer, message->lockMask) ||
	    !NativeCodecWriter_WriteU32(writer, message->sequence) ||
	    !NativeCodecWriter_WriteBytes(writer, message->baseDigest, sizeof(message->baseDigest)) ||
	    !NativeCodecWriter_WriteU64(writer, message->nonce) || !NativeCodecWriter_WriteU8(writer, message->characterID) ||
	    !NativeCodecWriter_WriteU8(writer, message->trackID) || !NativeCodecWriter_WriteU8(writer, message->lapCount) ||
	    !NativeCodecWriter_WriteU8(writer, message->currentItem) ||
	    !NativeCodecWriter_WriteBytes(writer, message->reserved0, sizeof(message->reserved0)) ||
	    !NativeCodecWriter_WriteBytes(writer, message->resolvedDigest, sizeof(message->resolvedDigest)) ||
	    !NativeCodecWriter_WriteBytes(writer, message->reserved1, sizeof(message->reserved1)))
	{
		return 0;
	}
	return 1;
}

size_t NativeMatchSelectMessageV1_EncodedSize(void)
{
	return NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES;
}

int NativeMatchSelectMessageV1_Encode(struct NativeCodecWriter *writer, const struct NativeMatchSelectMessageV1 *message)
{
	uint8_t bytes[NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES];
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter staged;
	struct NativeCodecWriter encoded;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES > writer->capacity - writer->offset) ||
	    (NativeMatchSelectMessageV1_ShapeCause(message) != NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE))
	{
		return 0;
	}

	/* The digest covers the body only, so it is taken before it is written. */
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&staged, bytes, sizeof(bytes), &digest);
	if (!NativeMatchSelectMessage_WriteBody(&staged, message) || (NativeCodecWriter_Size(&staged) != NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET))
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

/*
 * FNV-1a 64 over the record body (offsets 0..55) at the reader's current
 * offset. Only called once a full read of the record through a copy of this
 * reader has succeeded, which proves data is non-NULL and the record is in
 * bounds.
 */
static uint64_t NativeMatchSelectMessage_BodyDigest(const struct NativeCodecReader *validated)
{
	struct NativeCodecDigest64 digest;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, &validated->data[validated->offset], NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET);
	return digest.value;
}

int NativeMatchSelectMessageV1_Decode(struct NativeCodecReader *reader, struct NativeMatchSelectMessageV1 *message,
                                      uint32_t *faultCauseOut)
{
	struct NativeCodecReader encoded;
	struct NativeMatchSelectMessageV1 decoded;
	uint32_t magic = 0;
	uint16_t messageVersion = 0;
	uint16_t encodedSize = 0;
	uint32_t cause;
	uint64_t trailerDigest = 0;

	if ((reader == NULL) || (message == NULL) || !NativeCodecReader_Ok(reader) ||
	    (NativeCodecReader_Remaining(reader) != NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES))
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE;
		}
		return 0;
	}

	memset(&decoded, 0, sizeof(decoded));
	encoded = *reader;

	/*
	 * The field reads validate the reader (non-NULL data, offset and size in
	 * bounds, exactly the record remaining) before any pointer into its bytes
	 * is formed: the body digest is taken from the caller's reader only once
	 * they have all succeeded.
	 */
	if (!NativeCodecReader_ReadU32(&encoded, &magic) || !NativeCodecReader_ReadU16(&encoded, &messageVersion) ||
	    !NativeCodecReader_ReadU16(&encoded, &encodedSize) || !NativeCodecReader_ReadU8(&encoded, &decoded.senderHuman) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.humanCount) || !NativeCodecReader_ReadU8(&encoded, &decoded.phase) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.lockMask) || !NativeCodecReader_ReadU32(&encoded, &decoded.sequence) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.baseDigest, sizeof(decoded.baseDigest)) ||
	    !NativeCodecReader_ReadU64(&encoded, &decoded.nonce) || !NativeCodecReader_ReadU8(&encoded, &decoded.characterID) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.trackID) || !NativeCodecReader_ReadU8(&encoded, &decoded.lapCount) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.currentItem) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.reserved0, sizeof(decoded.reserved0)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.resolvedDigest, sizeof(decoded.resolvedDigest)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.reserved1, sizeof(decoded.reserved1)) ||
	    !NativeCodecReader_ReadU64(&encoded, &trailerDigest) || (NativeCodecReader_Remaining(&encoded) != 0))
	{
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE;
	}
	else if (magic != NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC)
	{
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC;
	}
	else if (messageVersion != NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION)
	{
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_VERSION;
	}
	else if (encodedSize != NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES)
	{
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ENCODED_SIZE;
	}
	else if (trailerDigest != NativeMatchSelectMessage_BodyDigest(reader))
	{
		cause = NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST;
	}
	else
	{
		cause = NativeMatchSelectMessageV1_ShapeCause(&decoded);
	}

	if (faultCauseOut != NULL)
	{
		*faultCauseOut = cause;
	}
	if (cause != NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE)
	{
		return 0;
	}
	*message = decoded;
	*reader = encoded;
	return 1;
}
