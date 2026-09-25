#ifndef PLATFORM_NATIVE_ARCADE_LAUNCH_H
#define PLATFORM_NATIVE_ARCADE_LAUNCH_H

#include "platform/native_canonical_codec.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Race launch agreement (docs/RACE_LAUNCH_MILESTONE.md section 4, RL-2,
 * RL-3, RL-4, RL-7): the 64-byte launch record v1 codec and one cabinet's
 * caller-owned launch agreement state.
 *
 * Pure: a transport-agnostic codec that encodes to and decodes from
 * caller-owned buffers through NativeCodecWriter/NativeCodecReader only, and
 * a fixed-size state the caller owns. There is no socket, OS networking,
 * clock, heap, game, or match-config dependency, and no hidden state; every
 * duration is counted in caller ticks (NativeArcadeLaunch_Tick). The record
 * has its own magic and version (RL-2); the handshake, bundle, match config,
 * and select record formats are unchanged. Its encoded size equals the
 * peer-link aux record size; the netplay adapter static-asserts that in
 * RL-S5, since this module includes no transport header.
 *
 * Roles: NATIVE_ARCADE_LAUNCH_ROLE_CAB1 and _CAB2 mirror
 * NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN and _CAB2_HUMAN (1 and 2). This module
 * includes no match-config header, so the netplay adapter static-asserts the
 * equality in RL-S5.
 *
 * RL-3 commit rule: an agreement commits on the first valid record from the
 * other cabinet role whose configDigest equals its own. There is no third
 * phase. A record with another magic (for example a late select record) is
 * FOREIGN; a malformed record, an own-role echo (SELF), or a different digest
 * (MISMATCH) is ignored and counted, never a failure state. HEARD stays
 * latched once seen. sequence is diagnostic only: no rule reads it, so a
 * reordered or duplicated record changes nothing. When Accept is called (the
 * flow phase) and when the agreement is reset are the caller's rules.
 *
 * RL-4 send and linger: the caller sends one record per tick while
 * NativeArcadeLaunch_ShouldSend is 1. After the commit every record carries
 * HEARD, and sending continues until at least one HEARD record was composed
 * and a HEARD record from the peer was accepted, capped at lingerTicks
 * (NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS, 10 s at the 30 Hz loop) after
 * the commit.
 *
 * RL-7 (two generals, accepted): a commit does not prove the peer will
 * commit. Its records may be lost or late, so the peer can still be pending
 * when the linger ends. This module has no timeout: the flow's launch
 * timeout, not this module, ends a pending agreement.
 */

#define NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC UINT32_C(0x314C414E) /* Little-endian "NAL1". */
#define NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION 1u
#define NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES 64u
#define NATIVE_ARCADE_LAUNCH_RECORD_V1_DIGEST_OFFSET 56u
#define NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES 32u
#define NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED0_BYTES 2u
#define NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED1_BYTES 8u

/* senderRole values (mirror NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN/_CAB2_HUMAN). */
#define NATIVE_ARCADE_LAUNCH_ROLE_CAB1 1u
#define NATIVE_ARCADE_LAUNCH_ROLE_CAB2 2u

/* flags bits. HEARD: the sender has committed. Every other bit is zero. */
#define NATIVE_ARCADE_LAUNCH_FLAG_HEARD 0x01u

/* RL-4: the linger cap after a commit, 10 s at the 30 Hz loop. */
#define NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS 300u

/*
 * Wire byte-offset table (little-endian via NativeCodecWriter/NativeCodecReader):
 *
 *   offset  size  field
 *   0       4     magic (NATIVE_ARCADE_LAUNCH_RECORD_V1_MAGIC)
 *   4       2     messageVersion (NATIVE_ARCADE_LAUNCH_RECORD_V1_VERSION)
 *   6       2     encodedSize, literal NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES
 *   8       1     senderRole, NATIVE_ARCADE_LAUNCH_ROLE_CAB1 or _CAB2
 *   9       1     flags: bit0 NATIVE_ARCADE_LAUNCH_FLAG_HEARD; other bits 0
 *   10      2     reserved0, must be all zero
 *   12      4     sequence, >= 1, per sender, +1 per composed record
 *   16      32    configDigest: the full SHA-256 digest of the sender's
 *                 relink proposal (opaque here)
 *   48      8     reserved1, must be all zero
 *   56      8     digest, FNV-1a 64 (NativeCodecDigest64) over bytes 0..55;
 *                 never digests itself
 *
 * Total: NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES == 64.
 */

/*
 * Codec fault causes. The enum is append-only: new causes may only be
 * appended at the end and an existing numeric value must never change.
 */
enum NativeArcadeLaunchRecordFaultCause
{
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE = 0,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SIZE = 1,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_MAGIC = 2,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_VERSION = 3,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ENCODED_SIZE = 4,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_DIGEST = 5,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_RESERVED = 6,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_ROLE = 7,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_FLAGS = 8,
	NATIVE_ARCADE_LAUNCH_RECORD_FAULT_BAD_SEQUENCE = 9
};

/*
 * The magic, message version, encoded size, and trailer digest are wire-only:
 * they are constants or derived values, so they are not members here. The
 * reserved fields are members because they are part of the validated record.
 */
struct NativeArcadeLaunchRecordV1
{
	uint8_t senderRole;
	uint8_t flags;
	uint8_t reserved0[NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED0_BYTES];
	uint32_t sequence;
	uint8_t configDigest[NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES];
	uint8_t reserved1[NATIVE_ARCADE_LAUNCH_RECORD_V1_RESERVED1_BYTES];
};

size_t NativeArcadeLaunchRecordV1_EncodedSize(void);

/*
 * Shape check shared by Encode and Decode. Returns the first failing cause,
 * or NONE. A NULL record is BAD_SIZE. The checks run in this fixed order:
 *   1. reserved0 and reserved1 all zero (BAD_RESERVED);
 *   2. senderRole CAB1 or CAB2 (BAD_ROLE);
 *   3. flags carry no bit other than HEARD (BAD_FLAGS);
 *   4. sequence >= 1 (BAD_SEQUENCE).
 * configDigest is opaque and accepts any value.
 */
uint32_t NativeArcadeLaunchRecordV1_ShapeCause(const struct NativeArcadeLaunchRecordV1 *record);

/*
 * Encode/decode are transactional: on any failure the caller's writer,
 * reader, and output record are left untouched. The record is always
 * exactly NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES bytes and is fully
 * written. Encode fails on a NULL argument, a failed or too-small writer, or
 * any shape fault (NativeArcadeLaunchRecordV1_ShapeCause != NONE).
 */
int NativeArcadeLaunchRecordV1_Encode(struct NativeCodecWriter *writer, const struct NativeArcadeLaunchRecordV1 *record);

/*
 * Accepts exactly one complete record: the reader must hold precisely
 * NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES remaining bytes. Every failure
 * reports its enum NativeArcadeLaunchRecordFaultCause through faultCauseOut
 * when that pointer is non-NULL; success reports
 * NATIVE_ARCADE_LAUNCH_RECORD_FAULT_NONE. NULL reader or record, a failed
 * reader, and any remaining length other than the encoded width report
 * BAD_SIZE. Checks run in a fixed order: size (BAD_SIZE), magic (BAD_MAGIC),
 * version (BAD_VERSION), encoded size (BAD_ENCODED_SIZE), trailer digest
 * (BAD_DIGEST), then the shape checks of
 * NativeArcadeLaunchRecordV1_ShapeCause in its documented order. The
 * reader's bytes are addressed only after the field reads have validated
 * them, so a malformed hand-built reader is a size fault and is never
 * dereferenced.
 */
int NativeArcadeLaunchRecordV1_Decode(struct NativeCodecReader *reader, struct NativeArcadeLaunchRecordV1 *record,
                                      uint32_t *faultCauseOut);

enum NativeArcadeLaunchStatus
{
	NATIVE_ARCADE_LAUNCH_PENDING = 0,
	NATIVE_ARCADE_LAUNCH_COMMITTED = 1
};

/* What NativeArcadeLaunch_Accept did with one received record. */
enum NativeArcadeLaunchAcceptResult
{
	NATIVE_ARCADE_LAUNCH_ACCEPT_INACTIVE = 0,  /* NULL or inactive agreement; nothing counted */
	NATIVE_ARCADE_LAUNCH_ACCEPT_ACCEPTED = 1,  /* valid, other role, equal digest */
	NATIVE_ARCADE_LAUNCH_ACCEPT_FOREIGN = 2,   /* another magic, for example a select record */
	NATIVE_ARCADE_LAUNCH_ACCEPT_MALFORMED = 3, /* any other decode fault, a wrong size, or NULL bytes */
	NATIVE_ARCADE_LAUNCH_ACCEPT_SELF = 4,      /* senderRole equals localRole */
	NATIVE_ARCADE_LAUNCH_ACCEPT_MISMATCH = 5   /* configDigest differs from ours */
};

/*
 * One cabinet's launch agreement. Caller-owned and fixed-size; every field is
 * plain data. Reset (or a zeroed struct) is inactive. The counts saturate at
 * UINT32_MAX.
 */
struct NativeArcadeLaunchAgreement
{
	uint8_t active;
	uint8_t localRole;
	uint8_t status;    /* enum NativeArcadeLaunchStatus */
	uint8_t peerHeard; /* an accepted record carried HEARD; latched */
	uint8_t heardSent; /* a composed record carried HEARD; latched */
	uint8_t reserved[3];
	uint8_t configDigest[NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES];
	uint32_t sequence; /* of the last composed record; 0 before the first */
	uint32_t lingerTicks;
	uint32_t ticksSinceCommit;
	uint32_t acceptedCount;
	uint32_t foreignCount;
	uint32_t malformedCount;
	uint32_t selfCount;
	uint32_t mismatchCount;
};

/*
 * Returns 1 and resets the agreement to a fresh, active PENDING agreement for
 * localRole on configDigest, with the given linger cap. Returns 0 with the
 * struct untouched on a NULL argument, a role other than CAB1/CAB2, or
 * lingerTicks 0.
 */
int NativeArcadeLaunch_Begin(struct NativeArcadeLaunchAgreement *agreement, uint8_t localRole,
                             const uint8_t configDigest[NATIVE_ARCADE_LAUNCH_CONFIG_DIGEST_BYTES], uint32_t lingerTicks);

/* All zero, inactive. NULL is a no-op. */
void NativeArcadeLaunch_Reset(struct NativeArcadeLaunchAgreement *agreement);

/*
 * Takes one received datagram payload. Returns an enum
 * NativeArcadeLaunchAcceptResult. INACTIVE for a NULL or inactive agreement,
 * with nothing counted. Otherwise, in order: a magic fault is FOREIGN; any
 * other decode fault is MALFORMED; senderRole equal to localRole is SELF; a
 * configDigest different from ours is MISMATCH; else ACCEPTED. Each result
 * except INACTIVE adds one to its count. ACCEPTED moves PENDING to COMMITTED
 * with ticksSinceCommit 0 (RL-3), and a record carrying HEARD sets peerHeard.
 * No result is a failure state.
 */
uint32_t NativeArcadeLaunch_Accept(struct NativeArcadeLaunchAgreement *agreement, const uint8_t *bytes, size_t size);

/* While active and COMMITTED: ticksSinceCommit += 1, saturating. Otherwise a no-op. */
void NativeArcadeLaunch_Tick(struct NativeArcadeLaunchAgreement *agreement);

/*
 * RL-4. 0 for a NULL or inactive agreement. 1 while PENDING. While
 * COMMITTED: 0 once ticksSinceCommit reaches lingerTicks, 0 once both
 * heardSent and peerHeard are set, else 1.
 */
int NativeArcadeLaunch_ShouldSend(const struct NativeArcadeLaunchAgreement *agreement);

/*
 * RL-4 without the lingerTicks cap: ShouldSend, except that ticksSinceCommit
 * never stops it. 0 for a NULL or inactive agreement. 1 while PENDING. While
 * COMMITTED: 0 once both heardSent and peerHeard are set, else 1. The caller
 * bounds how long it follows this rule instead of ShouldSend: the netplay
 * adapter does only through a linked race's start wait (the linked-race
 * plan, LR-69).
 */
int NativeArcadeLaunch_ShouldSendUncapped(const struct NativeArcadeLaunchAgreement *agreement);

/*
 * Composes the next record into out. Requires an active agreement, non-NULL
 * out and sizeOut, capacity >= NATIVE_ARCADE_LAUNCH_RECORD_V1_ENCODED_BYTES,
 * and sequence below UINT32_MAX; otherwise returns 0 and changes nothing (the
 * sequence never wraps). On success: sequence += 1, the record carries
 * senderRole localRole, flags HEARD iff COMMITTED, our configDigest, and zero
 * reserved fields; out holds its 64 encoded bytes, *sizeOut is 64, and
 * heardSent is set when the record carries HEARD. Compose does not consult
 * NativeArcadeLaunch_ShouldSend; the caller does.
 */
int NativeArcadeLaunch_Compose(struct NativeArcadeLaunchAgreement *agreement, uint8_t *out, size_t capacity, size_t *sizeOut);

/* enum NativeArcadeLaunchStatus; PENDING for a NULL or inactive agreement,
 * and for any status byte other than COMMITTED. */
uint32_t NativeArcadeLaunch_Status(const struct NativeArcadeLaunchAgreement *agreement);

/* 1 when the agreement is non-NULL and active, else 0. */
int NativeArcadeLaunch_Active(const struct NativeArcadeLaunchAgreement *agreement);

#endif
