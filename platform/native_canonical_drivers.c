#include "platform/native_canonical_drivers.h"

#include <string.h>

#define NATIVE_CANONICAL_DRIVERS_COMPARE_HEADER UINT32_C(0x01)
#define NATIVE_CANONICAL_DRIVERS_COMPARE_ROSTER UINT32_C(0x02)
#define NATIVE_CANONICAL_DRIVERS_COMPARE_SLOTS UINT32_C(0x04)
#define NATIVE_CANONICAL_DRIVERS_COMPARE_FULL UINT32_C(0x08)

static uint64_t NativeCanonicalDrivers_Digest(const uint8_t *bytes, size_t size)
{
	struct NativeCodecDigest64 digest;
	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, size);
	return digest.value;
}

static void NativeCanonicalDrivers_Absent(struct NativeCanonicalDriversSlotSummary *slot)
{
	static const uint8_t zeroes[NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES];
	slot->slotDigest = NativeCanonicalDrivers_Digest(zeroes, sizeof(zeroes));
	slot->metaRaceDigest = NativeCanonicalDrivers_Digest(zeroes, NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES);
	slot->physicsDynamicsDigest = NativeCanonicalDrivers_Digest(zeroes + NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES,
	                                                             NATIVE_CANONICAL_DRIVERS_PHYSICS_DYNAMICS_BYTES);
	slot->behaviorBotDigest = NativeCanonicalDrivers_Digest(zeroes + NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES +
	                                                        NATIVE_CANONICAL_DRIVERS_PHYSICS_DYNAMICS_BYTES,
	                                                        NATIVE_CANONICAL_DRIVERS_BEHAVIOR_BOT_BYTES);
}

void NativeCanonicalDriversV1_Init(struct NativeCanonicalDriversV1 *drivers)
{
	if (drivers == NULL) return;
	memset(drivers, 0, sizeof(*drivers));
	drivers->version = NATIVE_CANONICAL_DRIVERS_VERSION;
	drivers->slotCount = NATIVE_CANONICAL_DRIVERS_SLOT_COUNT;
	drivers->groupCount = NATIVE_CANONICAL_DRIVERS_GROUP_COUNT;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++) NativeCanonicalDrivers_Absent(&drivers->slots[i]);
}

int NativeCanonicalDriversV1_FromNormativeStream(struct NativeCanonicalDriversV1 *drivers, uint32_t presenceMask,
                                                 const uint8_t stream[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES])
{
	struct NativeCanonicalDriversV1 candidate;
	if ((drivers == NULL) || (stream == NULL) || ((presenceMask & ~UINT32_C(0xff)) != 0)) return 0;
	NativeCanonicalDriversV1_Init(&candidate);
	candidate.presenceMask = presenceMask;
	candidate.rosterMetaDigest = NativeCanonicalDrivers_Digest(stream, NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES);
	candidate.fullStreamDigest = NativeCanonicalDrivers_Digest(stream, NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
	{
		const uint8_t *slot = stream + NATIVE_CANONICAL_DRIVERS_ROSTER_BYTES + i * NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES;
		if ((presenceMask & (UINT32_C(1) << i)) == 0)
		{
			struct NativeCanonicalDriversSlotSummary absent;
			NativeCanonicalDrivers_Absent(&absent);
			if (memcmp(slot, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) != 0 ||
			    memcmp(&candidate.slots[i], &absent, sizeof(absent)) != 0) return 0;
			for (uint32_t j = 16; j < NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES; j++) if (slot[j] != 0) return 0;
			candidate.slots[i] = absent;
		}
		else
		{
			candidate.slots[i].slotDigest = NativeCanonicalDrivers_Digest(slot, NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES);
			candidate.slots[i].metaRaceDigest = NativeCanonicalDrivers_Digest(slot, NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES);
			candidate.slots[i].physicsDynamicsDigest = NativeCanonicalDrivers_Digest(slot + NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES,
			                                                                    NATIVE_CANONICAL_DRIVERS_PHYSICS_DYNAMICS_BYTES);
			candidate.slots[i].behaviorBotDigest = NativeCanonicalDrivers_Digest(slot + NATIVE_CANONICAL_DRIVERS_META_RACE_BYTES +
			                                                                   NATIVE_CANONICAL_DRIVERS_PHYSICS_DYNAMICS_BYTES,
			                                                                   NATIVE_CANONICAL_DRIVERS_BEHAVIOR_BOT_BYTES);
		}
	}
	*drivers = candidate;
	return 1;
}

int NativeCanonicalDriversV1_Validate(const struct NativeCanonicalDriversV1 *drivers)
{
	struct NativeCanonicalDriversSlotSummary absent;
	if ((drivers == NULL) || (drivers->version != NATIVE_CANONICAL_DRIVERS_VERSION) ||
	    (drivers->slotCount != NATIVE_CANONICAL_DRIVERS_SLOT_COUNT) || (drivers->groupCount != NATIVE_CANONICAL_DRIVERS_GROUP_COUNT) ||
	    ((drivers->presenceMask & ~UINT32_C(0xff)) != 0)) return 0;
	NativeCanonicalDrivers_Absent(&absent);
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
		if (((drivers->presenceMask & (UINT32_C(1) << i)) == 0) && (memcmp(&drivers->slots[i], &absent, sizeof(absent)) != 0)) return 0;
	return 1;
}

size_t NativeCanonicalDriversV1_EncodedSize(void) { return NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES; }

int NativeCanonicalDriversV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalDriversV1 *drivers)
{
	struct NativeCodecWriter encoded;
	if ((writer == NULL) || !NativeCanonicalDriversV1_Validate(drivers) || (writer->failed != 0) || (writer->offset > writer->capacity) ||
	    (NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES > writer->capacity - writer->offset)) return 0;
	encoded = *writer;
	if (!NativeCodecWriter_WriteU32(&encoded, drivers->version) || !NativeCodecWriter_WriteU32(&encoded, drivers->slotCount) ||
	    !NativeCodecWriter_WriteU32(&encoded, drivers->presenceMask) || !NativeCodecWriter_WriteU32(&encoded, drivers->groupCount) ||
	    !NativeCodecWriter_WriteU64(&encoded, drivers->rosterMetaDigest)) return 0;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
		if (!NativeCodecWriter_WriteU64(&encoded, drivers->slots[i].slotDigest) || !NativeCodecWriter_WriteU64(&encoded, drivers->slots[i].metaRaceDigest) ||
		    !NativeCodecWriter_WriteU64(&encoded, drivers->slots[i].physicsDynamicsDigest) || !NativeCodecWriter_WriteU64(&encoded, drivers->slots[i].behaviorBotDigest)) return 0;
	if (!NativeCodecWriter_WriteU64(&encoded, drivers->fullStreamDigest)) return 0;
	*writer = encoded;
	return 1;
}

int NativeCanonicalDriversV1_Decode(struct NativeCodecReader *reader, struct NativeCanonicalDriversV1 *drivers)
{
	struct NativeCodecReader encoded;
	struct NativeCanonicalDriversV1 candidate;
	if ((reader == NULL) || (drivers == NULL) || (reader->failed != 0) || (reader->offset > reader->size) ||
	    (NATIVE_CANONICAL_DRIVERS_SUMMARY_BYTES > reader->size - reader->offset)) return 0;
	encoded = *reader; memset(&candidate, 0, sizeof(candidate));
	if (!NativeCodecReader_ReadU32(&encoded, &candidate.version) || !NativeCodecReader_ReadU32(&encoded, &candidate.slotCount) ||
	    !NativeCodecReader_ReadU32(&encoded, &candidate.presenceMask) || !NativeCodecReader_ReadU32(&encoded, &candidate.groupCount) ||
	    !NativeCodecReader_ReadU64(&encoded, &candidate.rosterMetaDigest)) return 0;
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
		if (!NativeCodecReader_ReadU64(&encoded, &candidate.slots[i].slotDigest) || !NativeCodecReader_ReadU64(&encoded, &candidate.slots[i].metaRaceDigest) ||
		    !NativeCodecReader_ReadU64(&encoded, &candidate.slots[i].physicsDynamicsDigest) || !NativeCodecReader_ReadU64(&encoded, &candidate.slots[i].behaviorBotDigest)) return 0;
	if (!NativeCodecReader_ReadU64(&encoded, &candidate.fullStreamDigest) || !NativeCanonicalDriversV1_Validate(&candidate)) return 0;
	*reader = encoded; *drivers = candidate;
	return 1;
}

uint32_t NativeCanonicalDriversV1_Compare(const struct NativeCanonicalDriversV1 *expected, const struct NativeCanonicalDriversV1 *actual,
                                          struct NativeCanonicalDriversCompareMask *masks)
{
	struct NativeCanonicalDriversCompareMask candidate = {0};
	uint32_t result = 0;
	if ((expected == NULL) || (actual == NULL) || !NativeCanonicalDriversV1_Validate(expected) || !NativeCanonicalDriversV1_Validate(actual))
	{
		candidate.headerMask = UINT32_MAX; result = NATIVE_CANONICAL_DRIVERS_COMPARE_HEADER;
		if (masks != NULL) *masks = candidate;
		return result;
	}
	if ((expected->version != actual->version) || (expected->slotCount != actual->slotCount) ||
	    (expected->presenceMask != actual->presenceMask) || (expected->groupCount != actual->groupCount)) { candidate.headerMask = UINT32_MAX; result |= NATIVE_CANONICAL_DRIVERS_COMPARE_HEADER; }
	if (expected->rosterMetaDigest != actual->rosterMetaDigest) { candidate.rosterMask = 1; result |= NATIVE_CANONICAL_DRIVERS_COMPARE_ROSTER; }
	for (uint32_t i = 0; i < NATIVE_CANONICAL_DRIVERS_SLOT_COUNT; i++)
	{
		const uint32_t bit = UINT32_C(1) << i;
		if (expected->slots[i].slotDigest != actual->slots[i].slotDigest) candidate.slotMask |= bit;
		if (expected->slots[i].metaRaceDigest != actual->slots[i].metaRaceDigest) candidate.metaRaceMask |= bit;
		if (expected->slots[i].physicsDynamicsDigest != actual->slots[i].physicsDynamicsDigest) candidate.physicsDynamicsMask |= bit;
		if (expected->slots[i].behaviorBotDigest != actual->slots[i].behaviorBotDigest) candidate.behaviorBotMask |= bit;
	}
	if ((candidate.slotMask | candidate.metaRaceMask | candidate.physicsDynamicsMask | candidate.behaviorBotMask) != 0) result |= NATIVE_CANONICAL_DRIVERS_COMPARE_SLOTS;
	if (expected->fullStreamDigest != actual->fullStreamDigest) { candidate.fullStreamMask = 1; result |= NATIVE_CANONICAL_DRIVERS_COMPARE_FULL; }
	if (masks != NULL) *masks = candidate;
	return result;
}
