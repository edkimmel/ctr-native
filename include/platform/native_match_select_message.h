#ifndef PLATFORM_NATIVE_MATCH_SELECT_MESSAGE_H
#define PLATFORM_NATIVE_MATCH_SELECT_MESSAGE_H

#include "platform/native_canonical_codec.h"
#include "platform/native_match_select_rules.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Match-select wire message (docs/MATCH_SELECT_MILESTONE.md section 2.2,
 * MS-3): one cabinet's full select state in a fixed 64-byte record, sent
 * every tick during select. This module is a pure, transport-agnostic codec:
 * it encodes to and decodes from caller-owned buffers through
 * NativeCodecWriter/NativeCodecReader only, has no socket, OS networking,
 * clock, heap, or game dependency, and takes table membership from
 * native_match_select_rules (NativeMatchSelect_CharacterIndex/TrackIndex/
 * LapOptionIndex). It has its own magic and version (section 2.9).
 */

#define NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC UINT32_C(0x31534d4e) /* Little-endian "NMS1". */
#define NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION 1u
#define NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES 64u
#define NATIVE_MATCH_SELECT_MESSAGE_V1_DIGEST_OFFSET 56u
#define NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES 8u
#define NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED0_BYTES 4u
#define NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED1_BYTES 8u

/*
 * Wire byte-offset table (little-endian via NativeCodecWriter/NativeCodecReader):
 *
 *   offset  size  field
 *   0       4     magic (NATIVE_MATCH_SELECT_MESSAGE_V1_MAGIC)
 *   4       2     messageVersion (NATIVE_MATCH_SELECT_MESSAGE_V1_VERSION)
 *   6       2     encodedSize, literal NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES
 *   8       1     senderHuman, 0..humanCount-1
 *   9       1     humanCount, 1..NATIVE_MATCH_SELECT_MAX_HUMANS
 *   10      1     phase (enum NativeMatchSelectPhase)
 *   11      1     lockMask: bit0 character, bit1 track, bit2 laps
 *   12      4     sequence, >= 1
 *   16      8     baseDigest: bytes 0..7 of the base config SHA-256 digest
 *                 (opaque here)
 *   24      8     nonce, u64 (opaque here)
 *   32      1     characterID, a table character
 *   33      1     trackID, a table track
 *   34      1     lapCount, a table lap option
 *   35      1     currentItem (enum NativeMatchSelectItem)
 *   36      4     reserved0, must be all zero
 *   40      8     resolvedDigest: the first NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES
 *                 bytes of NativeMatchSelect_OutcomeDigest; all zero while
 *                 PICKING
 *   48      8     reserved1, must be all zero
 *   56      8     digest, FNV-1a 64 over bytes 0..55; never digests itself
 *
 * Total: NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES == 64.
 */

enum NativeMatchSelectPhase
{
	NATIVE_MATCH_SELECT_PHASE_PICKING = 0,
	NATIVE_MATCH_SELECT_PHASE_RESOLVED = 1
};

/* The item the sender is currently on; DONE once all three are locked. */
enum NativeMatchSelectItem
{
	NATIVE_MATCH_SELECT_ITEM_CHARACTER = 0,
	NATIVE_MATCH_SELECT_ITEM_TRACK = 1,
	NATIVE_MATCH_SELECT_ITEM_LAPS = 2,
	NATIVE_MATCH_SELECT_ITEM_DONE = 3
};

/* lockMask bits. Items lock in order, so lockMask == (1 << currentItem) - 1. */
#define NATIVE_MATCH_SELECT_LOCK_CHARACTER 0x1u
#define NATIVE_MATCH_SELECT_LOCK_TRACK 0x2u
#define NATIVE_MATCH_SELECT_LOCK_LAPS 0x4u

/*
 * Codec fault causes. The enum is append-only: new causes may only be
 * appended at the end and an existing numeric value must never change.
 */
enum NativeMatchSelectMessageFaultCause
{
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE = 0,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SIZE = 1,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_MAGIC = 2,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_VERSION = 3,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ENCODED_SIZE = 4,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_DIGEST = 5,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESERVED = 6,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_HUMAN_COUNT = 7,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SENDER = 8,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_PHASE = 9,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LOCK_MASK = 10,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_SEQUENCE = 11,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_CHARACTER = 12,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_TRACK = 13,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_LAPS = 14,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_ITEM = 15,
	NATIVE_MATCH_SELECT_MESSAGE_FAULT_BAD_RESOLVED_DIGEST = 16
};

/*
 * The magic, message version, encoded size, and trailer digest are wire-only:
 * they are constants or derived values, so they are not members here. The
 * reserved fields are members because they are part of the validated record.
 */
struct NativeMatchSelectMessageV1
{
	uint8_t senderHuman;
	uint8_t humanCount;
	uint8_t phase;
	uint8_t lockMask;
	uint32_t sequence;
	uint8_t baseDigest[NATIVE_MATCH_SELECT_MESSAGE_V1_BASE_DIGEST_BYTES];
	uint64_t nonce;
	uint8_t characterID;
	uint8_t trackID;
	uint8_t lapCount;
	uint8_t currentItem;
	uint8_t reserved0[NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED0_BYTES];
	uint8_t resolvedDigest[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES];
	uint8_t reserved1[NATIVE_MATCH_SELECT_MESSAGE_V1_RESERVED1_BYTES];
};

size_t NativeMatchSelectMessageV1_EncodedSize(void);

/*
 * Shape check shared by Encode and Decode (and usable by a caller composing a
 * message). Returns the first failing cause, or NONE. A NULL message is
 * BAD_SIZE. The checks run in this order, which is also the fault-cause
 * order:
 *   1. reserved0 and reserved1 all zero (BAD_RESERVED);
 *   2. humanCount 1..NATIVE_MATCH_SELECT_MAX_HUMANS (BAD_HUMAN_COUNT);
 *   3. senderHuman < humanCount (BAD_SENDER);
 *   4. phase PICKING or RESOLVED (BAD_PHASE);
 *   5. lockMask <= 7, i.e. only the three item bits (BAD_LOCK_MASK);
 *   6. sequence >= 1 (BAD_SEQUENCE);
 *   7. characterID, trackID, lapCount in the rules tables (BAD_CHARACTER,
 *      BAD_TRACK, BAD_LAPS, in that order);
 *   8. currentItem <= DONE and lockMask == (1 << currentItem) - 1 (BAD_ITEM);
 *   9. PICKING requires resolvedDigest all zero, and RESOLVED requires
 *      currentItem DONE (BAD_RESOLVED_DIGEST for either).
 * baseDigest and nonce are opaque and accept any value.
 */
uint32_t NativeMatchSelectMessageV1_ShapeCause(const struct NativeMatchSelectMessageV1 *message);

/*
 * Encode/decode are transactional: on any failure the caller's writer,
 * reader, and output message are left untouched. The record is always
 * exactly NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES bytes and is fully
 * written. Encode fails on a NULL argument, a failed or too-small writer, or
 * any shape fault (NativeMatchSelectMessageV1_ShapeCause != NONE).
 */
int NativeMatchSelectMessageV1_Encode(struct NativeCodecWriter *writer, const struct NativeMatchSelectMessageV1 *message);

/*
 * Accepts exactly one complete record: the reader must hold precisely
 * NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES remaining bytes. Every failure
 * reports its enum NativeMatchSelectMessageFaultCause through faultCauseOut
 * when that pointer is non-NULL; success reports
 * NATIVE_MATCH_SELECT_MESSAGE_FAULT_NONE. NULL reader or message, a failed
 * reader, and any remaining length other than the encoded width report
 * BAD_SIZE. Checks run in wire order: magic (BAD_MAGIC), message version
 * (BAD_VERSION), encoded size (BAD_ENCODED_SIZE), trailer digest
 * (BAD_DIGEST), then the shape checks of
 * NativeMatchSelectMessageV1_ShapeCause in its documented order.
 */
int NativeMatchSelectMessageV1_Decode(struct NativeCodecReader *reader, struct NativeMatchSelectMessageV1 *message,
                                      uint32_t *faultCauseOut);

#endif
