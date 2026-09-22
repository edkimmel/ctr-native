#include "platform/native_lockstep_handshake.h"

#include <string.h>

_Static_assert(NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET + sizeof(uint64_t) == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES,
               "The handshake digest must be the last field of the encoded record.");
_Static_assert(NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET ==
                   4u + 4u + 4u + 1u + 1u + 1u + 1u + NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES + NATIVE_LOCKSTEP_HANDSHAKE_V1_RESERVED1_BYTES,
               "The digest offset must equal the sum of every field written before it.");

static int NativeLockstepHandshake_IsAllZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;

	for (size_t i = 0; i < size; i++)
	{
		combined |= bytes[i];
	}
	return combined == 0;
}

/* Shape-only checks that do not touch the nested config: messageType, senderRole,
 * rejectReason shape, and the reserved bytes. */
static uint32_t NativeLockstepHandshakeMessage_ShapeCause(const struct NativeLockstepHandshakeMessageV1 *message)
{
	if (message == NULL)
	{
		return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE;
	}
	if ((message->messageType != NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO) &&
	    (message->messageType != NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT) &&
	    (message->messageType != NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT))
	{
		return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE;
	}
	if ((message->senderRole != NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (message->senderRole != NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
	{
		return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE;
	}
	if (message->messageType == NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT)
	{
		if ((message->rejectReason < NATIVE_LOCKSTEP_HANDSHAKE_REJECT_VERSION_MISMATCH) ||
		    (message->rejectReason > NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED))
		{
			return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON;
		}
	}
	else if (message->rejectReason != NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE)
	{
		return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON;
	}
	if ((message->reserved0 != 0) || !NativeLockstepHandshake_IsAllZero(message->reserved1, sizeof(message->reserved1)))
	{
		return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_RESERVED;
	}
	return NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;
}

/* Writes offsets 0..275.  The bundle digest is appended by the caller. */
static int NativeLockstepHandshakeMessage_WriteBody(struct NativeCodecWriter *writer, const struct NativeLockstepHandshakeMessageV1 *message,
                                                     const uint8_t configBytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES])
{
	if (!NativeCodecWriter_WriteU32(writer, NATIVE_LOCKSTEP_HANDSHAKE_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU32(writer, NATIVE_LOCKSTEP_HANDSHAKE_V1_VERSION) ||
	    !NativeCodecWriter_WriteU32(writer, NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteU8(writer, message->messageType) || !NativeCodecWriter_WriteU8(writer, message->senderRole) ||
	    !NativeCodecWriter_WriteU8(writer, message->rejectReason) || !NativeCodecWriter_WriteU8(writer, message->reserved0) ||
	    !NativeCodecWriter_WriteBytes(writer, configBytes, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteBytes(writer, message->reserved1, sizeof(message->reserved1)))
	{
		return 0;
	}
	return 1;
}

size_t NativeLockstepHandshakeMessageV1_EncodedSize(void)
{
	return NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES;
}

int NativeLockstepHandshakeMessageV1_Encode(struct NativeCodecWriter *writer, const struct NativeLockstepHandshakeMessageV1 *message)
{
	uint8_t bytes[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t configBytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	struct NativeCodecWriter configWriter;
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter staged;
	struct NativeCodecWriter encoded;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES > writer->capacity - writer->offset) ||
	    (NativeLockstepHandshakeMessage_ShapeCause(message) != NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE))
	{
		return 0;
	}

	/* NativeMatchConfigV1_Encode validates the nested config internally, so an
	 * invalid config fails here rather than being written malformed. */
	NativeCodecWriter_Init(&configWriter, configBytes, sizeof(configBytes), NULL);
	if (!NativeMatchConfigV1_Encode(&configWriter, &message->config) || (NativeCodecWriter_Size(&configWriter) != sizeof(configBytes)))
	{
		return 0;
	}

	/* The digest covers the body only, so it is taken before it is written. */
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&staged, bytes, sizeof(bytes), &digest);
	if (!NativeLockstepHandshakeMessage_WriteBody(&staged, message, configBytes) ||
	    (NativeCodecWriter_Size(&staged) != NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET))
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

int NativeLockstepHandshakeMessageV1_Decode(struct NativeCodecReader *reader, struct NativeLockstepHandshakeMessageV1 *message,
                                            uint32_t *faultCauseOut)
{
	struct NativeCodecReader encoded;
	struct NativeLockstepHandshakeMessageV1 decoded;
	struct NativeCodecDigest64 digest;
	uint8_t configBytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint32_t magic = 0;
	uint32_t messageVersion = 0;
	uint32_t encodedSize = 0;
	uint32_t cause;
	uint64_t bundleDigest = 0;

	if ((reader == NULL) || (message == NULL) || !NativeCodecReader_Ok(reader) ||
	    (NativeCodecReader_Remaining(reader) != NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES))
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE;
		}
		return 0;
	}

	memset(&decoded, 0, sizeof(decoded));
	encoded = *reader;
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, &encoded.data[encoded.offset], NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET);

	if (!NativeCodecReader_ReadU32(&encoded, &magic) || !NativeCodecReader_ReadU32(&encoded, &messageVersion) ||
	    !NativeCodecReader_ReadU32(&encoded, &encodedSize) || !NativeCodecReader_ReadU8(&encoded, &decoded.messageType) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.senderRole) || !NativeCodecReader_ReadU8(&encoded, &decoded.rejectReason) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.reserved0) ||
	    !NativeCodecReader_ReadBytes(&encoded, configBytes, sizeof(configBytes)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.reserved1, sizeof(decoded.reserved1)) ||
	    !NativeCodecReader_ReadU64(&encoded, &bundleDigest) || (NativeCodecReader_Remaining(&encoded) != 0))
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE;
	}
	else if (magic != NATIVE_LOCKSTEP_HANDSHAKE_V1_MAGIC)
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MAGIC;
	}
	else if (messageVersion != NATIVE_LOCKSTEP_HANDSHAKE_V1_VERSION)
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_VERSION;
	}
	else if (encodedSize != NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES)
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE;
	}
	else if (bundleDigest != digest.value)
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_DIGEST;
	}
	else if ((decoded.reserved0 != 0) || !NativeLockstepHandshake_IsAllZero(decoded.reserved1, sizeof(decoded.reserved1)))
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_RESERVED;
	}
	else if ((decoded.messageType != NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO) &&
	         (decoded.messageType != NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT) &&
	         (decoded.messageType != NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT))
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE;
	}
	else if ((decoded.senderRole != NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (decoded.senderRole != NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN))
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE;
	}
	else if ((decoded.messageType == NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT)
	             ? ((decoded.rejectReason < NATIVE_LOCKSTEP_HANDSHAKE_REJECT_VERSION_MISMATCH) ||
	                (decoded.rejectReason > NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED))
	             : (decoded.rejectReason != NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE))
	{
		cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON;
	}
	else
	{
		/*
		 * NativeMatchConfigV1_Decode validates the nested record internally
		 * and returns 0 for either a structurally malformed record or one
		 * that merely fails NativeMatchConfigV1_Validate, indistinguishably.
		 * Peeking the nested magic/encodedSize first tells the two apart:
		 * CONFIG_DECODE for a structural mismatch, CONFIG_INVALID once the
		 * nested wire shape itself is sound.
		 */
		struct NativeCodecReader peek;
		struct NativeCodecReader configReader;
		uint32_t nestedMagic = 0;
		uint32_t nestedSize = 0;

		NativeCodecReader_Init(&peek, configBytes, sizeof(configBytes));
		if (!NativeCodecReader_ReadU32(&peek, &nestedMagic) || !NativeCodecReader_ReadU32(&peek, &nestedSize) ||
		    (nestedMagic != NATIVE_MATCH_CONFIG_V1_MAGIC) || (nestedSize != NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES))
		{
			cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_DECODE;
		}
		else
		{
			NativeCodecReader_Init(&configReader, configBytes, sizeof(configBytes));
			if (!NativeMatchConfigV1_Decode(&configReader, &decoded.config))
			{
				cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_INVALID;
			}
			else
			{
				cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;
			}
		}
	}

	if (cause != NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE)
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = cause;
		}
		return 0;
	}
	*reader = encoded;
	*message = decoded;
	if (faultCauseOut != NULL)
	{
		*faultCauseOut = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;
	}
	return 1;
}

void NativeLockstepHandshake_Init(struct NativeLockstepHandshake *hs)
{
	if (hs == NULL)
	{
		return;
	}
	memset(hs, 0, sizeof(*hs));
	hs->mode = NATIVE_LOCKSTEP_HANDSHAKE_IDLE;
}

int NativeLockstepHandshake_Begin(struct NativeLockstepHandshake *hs, const struct NativeMatchConfigV1 *proposedConfig, uint8_t localRole)
{
	uint8_t slotIndex = 0;

	if ((hs == NULL) || (hs->mode != NATIVE_LOCKSTEP_HANDSHAKE_IDLE) || (proposedConfig == NULL) ||
	    ((localRole != NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) && (localRole != NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN)) ||
	    !NativeMatchConfigV1_Validate(proposedConfig) || !NativeMatchConfigV1_FindRoleSlot(proposedConfig, localRole, &slotIndex))
	{
		return 0;
	}

	hs->mode = NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT;
	hs->localRole = localRole;
	hs->outgoingMessageType = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO;
	hs->outgoingRejectReason = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE;
	hs->localConfig = *proposedConfig;
	memset(&hs->result, 0, sizeof(hs->result));
	return 1;
}

int NativeLockstepHandshake_ComposeMessage(const struct NativeLockstepHandshake *hs, uint8_t *bytes, size_t capacity, size_t *sizeOut)
{
	struct NativeLockstepHandshakeMessageV1 message;
	struct NativeCodecWriter writer;

	if ((hs == NULL) || (bytes == NULL) || (sizeOut == NULL) || (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_IDLE))
	{
		return 0;
	}

	memset(&message, 0, sizeof(message));
	message.messageType = hs->outgoingMessageType;
	message.senderRole = hs->localRole;
	message.rejectReason = hs->outgoingRejectReason;
	message.config = hs->localConfig;

	NativeCodecWriter_Init(&writer, bytes, capacity, NULL);
	if (!NativeLockstepHandshakeMessageV1_Encode(&writer, &message))
	{
		return 0;
	}
	*sizeOut = NativeCodecWriter_Size(&writer);
	return 1;
}

/* Latches REJECTED with the given reason/peerRole, unless already terminal. */
static void NativeLockstepHandshake_LatchRejected(struct NativeLockstepHandshake *hs, uint32_t reason, uint8_t peerRole)
{
	if ((hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE) || (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED))
	{
		return;
	}
	hs->mode = NATIVE_LOCKSTEP_HANDSHAKE_REJECTED;
	hs->result.peerRole = peerRole;
	hs->result.rejectReason = reason;
	memset(&hs->result.agreedConfig, 0, sizeof(hs->result.agreedConfig));
}

/*
 * The one-time full-identity check: configurationVersion, protocolVersion,
 * canonicalSchemaVersion, and replayFormatVersion must all agree, and the
 * full 32-byte SHA-256 config digest must match byte for byte.
 */
static int NativeLockstepHandshake_ConfigsAgree(const struct NativeMatchConfigV1 *local, const struct NativeMatchConfigV1 *peer)
{
	uint8_t localDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t peerDigest[NATIVE_SHA256_DIGEST_BYTES];

	if ((local->configurationVersion != peer->configurationVersion) || (local->protocolVersion != peer->protocolVersion) ||
	    (local->canonicalSchemaVersion != peer->canonicalSchemaVersion) || (local->replayFormatVersion != peer->replayFormatVersion))
	{
		return 0;
	}
	if (!NativeMatchConfigV1_Digest(local, localDigest) || !NativeMatchConfigV1_Digest(peer, peerDigest))
	{
		return 0;
	}
	return memcmp(localDigest, peerDigest, sizeof(localDigest)) == 0;
}

enum NativeLockstepHandshakeAcceptResult NativeLockstepHandshake_AcceptMessage(struct NativeLockstepHandshake *hs, const uint8_t *bytes,
                                                                               size_t size)
{
	struct NativeCodecReader reader;
	struct NativeLockstepHandshakeMessageV1 decoded;
	uint32_t faultCause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;

	if ((hs == NULL) || (bytes == NULL) || (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_IDLE))
	{
		return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE;
	}

	NativeCodecReader_Init(&reader, bytes, size);
	if (!NativeLockstepHandshakeMessageV1_Decode(&reader, &decoded, &faultCause))
	{
		if (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT)
		{
			NativeLockstepHandshake_LatchRejected(hs, (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED, 0u);
			hs->outgoingMessageType = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT;
			hs->outgoingRejectReason = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED;
		}
		return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT;
	}

	if (decoded.messageType == NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO)
	{
		if ((hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE) || (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED))
		{
			return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
		}
		if (decoded.senderRole == hs->localRole)
		{
			NativeLockstepHandshake_LatchRejected(hs, (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_ROLE_CONFLICT, decoded.senderRole);
			hs->outgoingMessageType = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT;
			hs->outgoingRejectReason = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_ROLE_CONFLICT;
			return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
		}
		if (!NativeLockstepHandshake_ConfigsAgree(&hs->localConfig, &decoded.config))
		{
			NativeLockstepHandshake_LatchRejected(hs, (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH, decoded.senderRole);
			hs->outgoingMessageType = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT;
			hs->outgoingRejectReason = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH;
			return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
		}
		hs->mode = NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE;
		hs->result.peerRole = decoded.senderRole;
		hs->result.rejectReason = (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE;
		hs->result.agreedConfig = hs->localConfig;
		hs->outgoingMessageType = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT;
		hs->outgoingRejectReason = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE;
		return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
	}

	if (decoded.messageType == NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT)
	{
		if (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT)
		{
			NativeLockstepHandshake_LatchRejected(hs, decoded.rejectReason, decoded.senderRole);
			/* Documented choice: keep composing the same REJECT-acknowledging
			 * state, carrying the peer's own reported reason. */
			hs->outgoingMessageType = (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT;
			hs->outgoingRejectReason = (uint8_t)decoded.rejectReason;
		}
		return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
	}

	/* ACCEPT: never trust the peer's say-so; only this side's own step-3
	 * comparison above may complete the handshake. */
	return NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
}

enum NativeLockstepHandshakeMode NativeLockstepHandshake_Mode(const struct NativeLockstepHandshake *hs)
{
	return (hs != NULL) ? hs->mode : NATIVE_LOCKSTEP_HANDSHAKE_IDLE;
}

const struct NativeLockstepHandshakeResult *NativeLockstepHandshake_Result(const struct NativeLockstepHandshake *hs)
{
	if ((hs == NULL) || (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_IDLE) || (hs->mode == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT))
	{
		return NULL;
	}
	return &hs->result;
}
