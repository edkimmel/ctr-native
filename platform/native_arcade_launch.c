#include "platform/native_arcade_launch.h"

#include <string.h>

_Static_assert(NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET + sizeof(uint64_t) == NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES,
               "The launch record digest must be the last field of the encoded record.");
_Static_assert(NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET ==
                   4u + 2u + 2u + 1u + 1u + NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED0_BYTES + 4u +
                       NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES + NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED1_BYTES,
               "The digest offset must equal the sum of every field written before it.");
_Static_assert(NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES <= UINT16_MAX, "encodedSize is a u16 on the wire.");
_Static_assert(NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION <= UINT16_MAX, "messageVersion is a u16 on the wire.");

static int NativeArcadeLaunch_IsAllZero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0;

	for (size_t i = 0; i < size; i++)
	{
		combined |= bytes[i];
	}
	return combined == 0;
}

static int NativeArcadeLaunch_IsRole(uint32_t role)
{
	return (role == NATIVE_ARCADE_LAUNCH_ROLE_CAB1) || (role == NATIVE_ARCADE_LAUNCH_ROLE_CAB2);
}

static void NativeArcadeLaunch_Count(uint32_t *count)
{
	if (*count < UINT32_MAX)
	{
		*count += 1u;
	}
}

uint32_t NativeArcadeLaunchRecordV1_ShapeCause(const struct NativeArcadeLaunchRecordV1 *record)
{
	if (record == NULL)
	{
		return NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE;
	}
	if (!NativeArcadeLaunch_IsAllZero(record->reserved0, sizeof(record->reserved0)) ||
	    !NativeArcadeLaunch_IsAllZero(record->reserved1, sizeof(record->reserved1)))
	{
		return NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_RESERVED;
	}
	if (!NativeArcadeLaunch_IsRole(record->senderRole))
	{
		return NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ROLE;
	}
	if ((record->flags & ~NATIVE_ARCADE_LAUNCH_FLAG_HEARD) != 0u)
	{
		return NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_FLAGS;
	}
	if (record->sequence < 1u)
	{
		return NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SEQUENCE;
	}
	return NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE;
}

/* Writes offsets 0..55. The trailer digest is appended by the caller. */
static int NativeArcadeLaunch_WriteBody(struct NativeCodecWriter *writer, const struct NativeArcadeLaunchRecordV1 *record)
{
	if (!NativeCodecWriter_WriteU32(writer, NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC) ||
	    !NativeCodecWriter_WriteU16(writer, (uint16_t)NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION) ||
	    !NativeCodecWriter_WriteU16(writer, (uint16_t)NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES) ||
	    !NativeCodecWriter_WriteU8(writer, record->senderRole) || !NativeCodecWriter_WriteU8(writer, record->flags) ||
	    !NativeCodecWriter_WriteBytes(writer, record->reserved0, sizeof(record->reserved0)) ||
	    !NativeCodecWriter_WriteU32(writer, record->sequence) ||
	    !NativeCodecWriter_WriteBytes(writer, record->configDigest, sizeof(record->configDigest)) ||
	    !NativeCodecWriter_WriteBytes(writer, record->reserved1, sizeof(record->reserved1)))
	{
		return 0;
	}
	return 1;
}

size_t NativeArcadeLaunchRecordV1_EncodedSize(void)
{
	return NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES;
}

int NativeArcadeLaunchRecordV1_Encode(struct NativeCodecWriter *writer, const struct NativeArcadeLaunchRecordV1 *record)
{
	uint8_t bytes[NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES];
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter staged;
	struct NativeCodecWriter encoded;

	if ((writer == NULL) || !NativeCodecWriter_Ok(writer) || (writer->offset > writer->capacity) ||
	    (NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES > writer->capacity - writer->offset) ||
	    (NativeArcadeLaunchRecordV1_ShapeCause(record) != NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE))
	{
		return 0;
	}

	/* The digest covers the body only, so it is taken before it is written. */
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&staged, bytes, sizeof(bytes), &digest);
	if (!NativeArcadeLaunch_WriteBody(&staged, record) || (NativeCodecWriter_Size(&staged) != NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET))
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
static uint64_t NativeArcadeLaunch_BodyDigest(const struct NativeCodecReader *validated)
{
	struct NativeCodecDigest64 digest;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, &validated->data[validated->offset], NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET);
	return digest.value;
}

int NativeArcadeLaunchRecordV1_Decode(struct NativeCodecReader *reader, struct NativeArcadeLaunchRecordV1 *record,
                                      uint32_t *faultCauseOut)
{
	struct NativeCodecReader encoded;
	struct NativeArcadeLaunchRecordV1 decoded;
	uint32_t magic = 0;
	uint16_t messageVersion = 0;
	uint16_t encodedSize = 0;
	uint32_t cause;
	uint64_t trailerDigest = 0;

	if ((reader == NULL) || (record == NULL) || !NativeCodecReader_Ok(reader) ||
	    (NativeCodecReader_Remaining(reader) != NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES))
	{
		if (faultCauseOut != NULL)
		{
			*faultCauseOut = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE;
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
	    !NativeCodecReader_ReadU16(&encoded, &encodedSize) || !NativeCodecReader_ReadU8(&encoded, &decoded.senderRole) ||
	    !NativeCodecReader_ReadU8(&encoded, &decoded.flags) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.reserved0, sizeof(decoded.reserved0)) ||
	    !NativeCodecReader_ReadU32(&encoded, &decoded.sequence) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.configDigest, sizeof(decoded.configDigest)) ||
	    !NativeCodecReader_ReadBytes(&encoded, decoded.reserved1, sizeof(decoded.reserved1)) ||
	    !NativeCodecReader_ReadU64(&encoded, &trailerDigest) || (NativeCodecReader_Remaining(&encoded) != 0))
	{
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE;
	}
	else if (magic != NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC)
	{
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC;
	}
	else if (messageVersion != NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION)
	{
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_VERSION;
	}
	else if (encodedSize != NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES)
	{
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ENCODED_SIZE;
	}
	else if (trailerDigest != NativeArcadeLaunch_BodyDigest(reader))
	{
		cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST;
	}
	else
	{
		cause = NativeArcadeLaunchRecordV1_ShapeCause(&decoded);
	}

	if (faultCauseOut != NULL)
	{
		*faultCauseOut = cause;
	}
	if (cause != NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE)
	{
		return 0;
	}
	*record = decoded;
	*reader = encoded;
	return 1;
}

int NativeArcadeLaunch_Begin(struct NativeArcadeLaunchAgreement *agreement, uint8_t localRole,
                             const uint8_t configDigest[NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES], uint32_t lingerTicks)
{
	if ((agreement == NULL) || (configDigest == NULL) || !NativeArcadeLaunch_IsRole(localRole) || (lingerTicks == 0u))
	{
		return 0;
	}
	memset(agreement, 0, sizeof(*agreement));
	agreement->active = 1;
	agreement->localRole = localRole;
	agreement->status = NATIVE_ARCADE_LAUNCH_PENDING;
	memcpy(agreement->configDigest, configDigest, sizeof(agreement->configDigest));
	agreement->lingerTicks = lingerTicks;
	return 1;
}

void NativeArcadeLaunch_Reset(struct NativeArcadeLaunchAgreement *agreement)
{
	if (agreement != NULL)
	{
		memset(agreement, 0, sizeof(*agreement));
	}
}

uint32_t NativeArcadeLaunch_Accept(struct NativeArcadeLaunchAgreement *agreement, const uint8_t *bytes, size_t size)
{
	struct NativeCodecReader reader;
	struct NativeArcadeLaunchRecordV1 record;
	uint32_t cause = NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE;

	if ((agreement == NULL) || !agreement->active)
	{
		return NATIVE_ARCADE_LAUNCH_ACCEPT_INACTIVE;
	}
	if (bytes == NULL)
	{
		NativeArcadeLaunch_Count(&agreement->malformedCount);
		return NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED;
	}

	NativeCodecReader_Init(&reader, bytes, size);
	if (!NativeArcadeLaunchRecordV1_Decode(&reader, &record, &cause))
	{
		if (cause == NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC)
		{
			NativeArcadeLaunch_Count(&agreement->foreignCount);
			return NATIVE_ARCADE_LAUNCH_ACCEPT_FOREIGN;
		}
		NativeArcadeLaunch_Count(&agreement->malformedCount);
		return NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED;
	}
	if (record.senderRole == agreement->localRole)
	{
		NativeArcadeLaunch_Count(&agreement->selfCount);
		return NATIVE_ARCADE_LAUNCH_ACCEPT_SELF;
	}
	if (memcmp(record.configDigest, agreement->configDigest, sizeof(record.configDigest)) != 0)
	{
		NativeArcadeLaunch_Count(&agreement->mismatchCount);
		return NATIVE_ARCADE_LAUNCH_ACCEPT_MISMATCH;
	}

	NativeArcadeLaunch_Count(&agreement->acceptedCount);
	if (agreement->status == NATIVE_ARCADE_LAUNCH_PENDING)
	{
		agreement->status = NATIVE_ARCADE_LAUNCH_COMMITTED;
		agreement->ticksSinceCommit = 0;
	}
	if ((record.flags & NATIVE_ARCADE_LAUNCH_FLAG_HEARD) != 0u)
	{
		agreement->peerHeard = 1;
	}
	return NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED;
}

void NativeArcadeLaunch_Tick(struct NativeArcadeLaunchAgreement *agreement)
{
	if ((agreement != NULL) && agreement->active && (agreement->status == NATIVE_ARCADE_LAUNCH_COMMITTED) &&
	    (agreement->ticksSinceCommit < UINT32_MAX))
	{
		agreement->ticksSinceCommit += 1u;
	}
}

int NativeArcadeLaunch_ShouldSend(const struct NativeArcadeLaunchAgreement *agreement)
{
	if ((agreement == NULL) || !agreement->active)
	{
		return 0;
	}
	if (agreement->status != NATIVE_ARCADE_LAUNCH_COMMITTED)
	{
		return 1;
	}
	if (agreement->ticksSinceCommit >= agreement->lingerTicks)
	{
		return 0;
	}
	return !(agreement->heardSent && agreement->peerHeard);
}

int NativeArcadeLaunch_Compose(struct NativeArcadeLaunchAgreement *agreement, uint8_t *out, size_t capacity, size_t *sizeOut)
{
	struct NativeArcadeLaunchRecordV1 record;
	struct NativeCodecWriter writer;
	uint8_t bytes[NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES];

	if ((agreement == NULL) || !agreement->active || (out == NULL) || (sizeOut == NULL) ||
	    (capacity < NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES) || (agreement->sequence >= UINT32_MAX))
	{
		return 0;
	}

	memset(&record, 0, sizeof(record));
	record.senderRole = agreement->localRole;
	record.flags = (agreement->status == NATIVE_ARCADE_LAUNCH_COMMITTED) ? (uint8_t)NATIVE_ARCADE_LAUNCH_FLAG_HEARD : (uint8_t)0u;
	record.sequence = agreement->sequence + 1u;
	memcpy(record.configDigest, agreement->configDigest, sizeof(record.configDigest));

	/* Staged so a refused record changes neither out nor the agreement. */
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	if (!NativeArcadeLaunchRecordV1_Encode(&writer, &record) || (NativeCodecWriter_Size(&writer) != sizeof(bytes)))
	{
		return 0;
	}

	memcpy(out, bytes, sizeof(bytes));
	*sizeOut = sizeof(bytes);
	agreement->sequence = record.sequence;
	if ((record.flags & NATIVE_ARCADE_LAUNCH_FLAG_HEARD) != 0u)
	{
		agreement->heardSent = 1;
	}
	return 1;
}

uint32_t NativeArcadeLaunch_Status(const struct NativeArcadeLaunchAgreement *agreement)
{
	if ((agreement == NULL) || !agreement->active)
	{
		return NATIVE_ARCADE_LAUNCH_PENDING;
	}
	/* Only an exact COMMITTED byte commits; any other value reads PENDING. */
	return (agreement->status == NATIVE_ARCADE_LAUNCH_COMMITTED) ? (uint32_t)NATIVE_ARCADE_LAUNCH_COMMITTED
	                                                             : (uint32_t)NATIVE_ARCADE_LAUNCH_PENDING;
}

int NativeArcadeLaunch_Active(const struct NativeArcadeLaunchAgreement *agreement)
{
	return (agreement != NULL) && (agreement->active != 0u);
}
