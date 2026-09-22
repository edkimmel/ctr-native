#ifndef PLATFORM_NATIVE_LOCKSTEP_HANDSHAKE_H
#define PLATFORM_NATIVE_LOCKSTEP_HANDSHAKE_H

#include "platform/native_canonical_codec.h"
#include "platform/native_match_config.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Connect/handshake protocol that lets two peers exchange and validate a
 * NativeMatchConfigV1 proposal before a NativeLockstepSession is opened,
 * replacing the implicit hard-fault-on-first-bundle behavior
 * NativeLockstepSession_AcceptBundle has today with an explicit
 * accept/reject handshake.  This module is transport-agnostic by design,
 * exactly like native_lockstep_protocol.c: it encodes to and decodes from
 * caller-owned byte buffers only, has no socket, OS networking, clock, or
 * game dependency, and never links native_udp_transport,
 * native_lockstep_session, or native_virtual_datagram.  It consumes
 * NativeMatchConfigV1 read-only through NativeMatchConfigV1_Encode/_Decode/
 * _Validate/_Digest/_FindRoleSlot and never writes back to it.
 *
 * Scope decision (docs/LOBBY_MILESTONE.md section 2.2): this is an explicit
 * validate-and-accept/reject handshake, not automatic reconciliation of two
 * differing config proposals.  Each peer proposes a NativeMatchConfigV1 (in
 * practice byte-identical proposals both cabinets built from the same
 * fixture/profile/seed); the handshake's job is to confirm that identity is
 * agreed before a lockstep session opens and to surface a clean, explicit
 * rejection when it is not.
 */

#define NATIVE_LOCKSTEP_HANDSHAKE_V1_MAGIC UINT32_C(0x31484c4e) /* Little-endian "NLH1". */
#define NATIVE_LOCKSTEP_HANDSHAKE_V1_VERSION UINT32_C(1)
#define NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES 284u
#define NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET 276u
#define NATIVE_LOCKSTEP_HANDSHAKE_V1_RESERVED1_BYTES 4u

/*
 * Wire byte-offset table (little-endian via NativeCodecWriter/NativeCodecReader,
 * exactly like the lockstep frame bundle):
 *
 *   offset  size  field
 *   0       4     magic (NATIVE_LOCKSTEP_HANDSHAKE_V1_MAGIC)
 *   4       4     messageVersion (NATIVE_LOCKSTEP_HANDSHAKE_V1_VERSION)
 *   8       4     encodedSize, literal NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES
 *   12      1     messageType (enum NativeLockstepHandshakeMessageType)
 *   13      1     senderRole (CAB1_HUMAN or CAB2_HUMAN)
 *   14      1     rejectReason (enum NativeLockstepHandshakeRejectReason)
 *   15      1     reserved0, must be 0
 *   16      256   config, the raw NativeMatchConfigV1_Encode output; it keeps
 *                 its own self-describing magic/size and decodes independently
 *                 with NativeMatchConfigV1_Decode against a reader scoped to
 *                 exactly those 256 bytes
 *   272     4     reserved1, must be all zero
 *   276     8     bundleDigest, FNV-1a 64 over bytes 0..275; never digests itself
 *
 * Total: NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES == 284.
 */

enum NativeLockstepHandshakeMessageType
{
	NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_HELLO = 1,
	NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_ACCEPT = 2,
	NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT = 3
};

enum NativeLockstepHandshakeRejectReason
{
	NATIVE_LOCKSTEP_HANDSHAKE_REJECT_NONE = 0,
	NATIVE_LOCKSTEP_HANDSHAKE_REJECT_VERSION_MISMATCH = 1,
	NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_INVALID = 2,
	NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH = 3,
	NATIVE_LOCKSTEP_HANDSHAKE_REJECT_ROLE_CONFLICT = 4,
	NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED = 5
};

/*
 * Codec fault causes.  The enum is append-only, mirroring
 * enum NativeLockstepFaultCause: new causes may only be appended at the end
 * and an existing numeric value must never change.  CONFIG_DECODE is a
 * structurally malformed nested 256-byte config record (bad nested magic or
 * encoded size); CONFIG_INVALID is a structurally well-formed nested record
 * that fails NativeMatchConfigV1_Validate.  The two are told apart by peeking
 * the nested magic/encodedSize before calling NativeMatchConfigV1_Decode,
 * because that function validates internally and would otherwise return 0
 * for either case indistinguishably.
 */
enum NativeLockstepHandshakeFaultCause
{
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE = 0,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MAGIC = 1,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_VERSION = 2,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE = 3,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_DIGEST = 4,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_RESERVED = 5,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_MESSAGE_TYPE = 6,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SENDER_ROLE = 7,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_REJECT_REASON = 8,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_DECODE = 9,
	NATIVE_LOCKSTEP_HANDSHAKE_FAULT_CONFIG_INVALID = 10
};

/*
 * The magic, message version, encoded size, and bundle digest are wire-only:
 * they are constants or derived values, so they are not members here.  The
 * reserved fields are members because they are part of the validated record,
 * the same convention NativeLockstepBundleV1 uses.
 */
struct NativeLockstepHandshakeMessageV1
{
	uint8_t messageType;
	uint8_t senderRole;
	uint8_t rejectReason;
	uint8_t reserved0;
	struct NativeMatchConfigV1 config;
	uint8_t reserved1[NATIVE_LOCKSTEP_HANDSHAKE_V1_RESERVED1_BYTES];
};

size_t NativeLockstepHandshakeMessageV1_EncodedSize(void);

/*
 * Encode/decode are transactional: on any failure the caller's writer,
 * reader, and output message are left untouched.  The record is always
 * exactly NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES bytes and is fully
 * written.  The bundle digest covers bytes 0..275 only and never digests
 * itself.  Encode fails if the shape is invalid (bad messageType, bad
 * senderRole, a rejectReason inconsistent with messageType, nonzero
 * reserved0/reserved1) or if message->config fails
 * NativeMatchConfigV1_Validate (NativeMatchConfigV1_Encode enforces this).
 */
int NativeLockstepHandshakeMessageV1_Encode(struct NativeCodecWriter *writer, const struct NativeLockstepHandshakeMessageV1 *message);

/*
 * Accepts exactly one complete record: the reader must hold precisely
 * NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES remaining bytes.  Every failure
 * path reports a distinct enum NativeLockstepHandshakeFaultCause through
 * faultCauseOut when that pointer is non-NULL; success reports
 * NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE.  NULL arguments and any remaining
 * length other than the encoded width report
 * NATIVE_LOCKSTEP_HANDSHAKE_FAULT_BAD_SIZE.  Checks run in wire order: magic,
 * message version, encoded size, bundle digest, reserved bytes, message type,
 * sender role, reject reason shape, then the nested 256-byte config (a
 * structurally malformed nested record is CONFIG_DECODE, one that decodes but
 * fails NativeMatchConfigV1_Validate is CONFIG_INVALID).
 */
int NativeLockstepHandshakeMessageV1_Decode(struct NativeCodecReader *reader, struct NativeLockstepHandshakeMessageV1 *message,
                                            uint32_t *faultCauseOut);

/*
 * COMPLETE and REJECTED are both terminal and latch-once: once either is
 * reached a later AcceptMessage call must not change the latched result,
 * mirroring the NativeLockstepSession divergence/fault latch idiom.
 */
enum NativeLockstepHandshakeMode
{
	NATIVE_LOCKSTEP_HANDSHAKE_IDLE = 0,
	NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT = 1,
	NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE = 2,
	NATIVE_LOCKSTEP_HANDSHAKE_REJECTED = 3
};

enum NativeLockstepHandshakeAcceptResult
{
	NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK = 0,
	NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE = 1, /* Caller misuse: NULL, or AcceptMessage before Begin. */
	NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT = 2                /* The incoming record is a protocol fault. */
};

/* peerRole/rejectReason/agreedConfig are only meaningful once latched; agreedConfig is only meaningful when rejectReason is 0. */
struct NativeLockstepHandshakeResult
{
	uint8_t peerRole;
	uint32_t rejectReason;
	struct NativeMatchConfigV1 agreedConfig;
};

/*
 * Caller-owned, no dynamic allocation, fixed-size members only: a localConfig
 * snapshot, small scalar fields for the currently armed outgoing message, and
 * one latch-once result.
 */
struct NativeLockstepHandshake
{
	enum NativeLockstepHandshakeMode mode;
	uint8_t localRole;
	uint8_t outgoingMessageType;
	uint8_t outgoingRejectReason;
	struct NativeMatchConfigV1 localConfig;
	struct NativeLockstepHandshakeResult result;
};

/* Zeroes the whole struct and leaves the handshake IDLE. */
void NativeLockstepHandshake_Init(struct NativeLockstepHandshake *hs);

/*
 * Requires mode IDLE; requires localRole to be CAB1_HUMAN or CAB2_HUMAN;
 * requires NativeMatchConfigV1_Validate(proposedConfig) to succeed and
 * requires NativeMatchConfigV1_FindRoleSlot(proposedConfig, localRole, ...)
 * to succeed, i.e. the caller really does own that role in this config.
 * Snapshots proposedConfig into hs, sets mode HELLO_SENT, and arms the
 * outgoing message as HELLO.  Returns 0 and changes nothing on any rejection.
 */
int NativeLockstepHandshake_Begin(struct NativeLockstepHandshake *hs, const struct NativeMatchConfigV1 *proposedConfig, uint8_t localRole);

/*
 * Composes whatever the current outgoing message is: HELLO while no peer
 * HELLO has been validated yet, or the ACCEPT/REJECT reply once one has (see
 * NativeLockstepHandshake_AcceptMessage for exactly when that reply is
 * armed).  Requires mode is not IDLE (Begin must have run first).  This can
 * be called repeatedly; it is the caller's job to decide the resend cadence,
 * this module has no clock.  Returns 0 and writes nothing if capacity is too
 * small, sizeOut is NULL, or hs is still IDLE.
 */
int NativeLockstepHandshake_ComposeMessage(const struct NativeLockstepHandshake *hs, uint8_t *bytes, size_t capacity, size_t *sizeOut);

/*
 * Decodes one incoming wire message and updates hs.  Exact behavior:
 *
 * 1. hs NULL or bytes NULL, or hs mode is IDLE (Begin was never called): this
 *    is caller misuse, not wire data; returns REJECTED_LOCAL_STATE and
 *    latches nothing.
 * 2. Decode fails (any NativeLockstepHandshakeMessageV1_Decode fault): if hs
 *    mode is HELLO_SENT (not yet terminal), latches REJECTED with reason
 *    MALFORMED and arms an outgoing REJECT(MALFORMED) reply (senderRole is
 *    always this side's localRole, via ComposeMessage); returns FAULT.  If hs
 *    mode is already COMPLETE or REJECTED, does not re-latch (idempotent
 *    no-op on an already-terminal handshake); returns FAULT.
 * 3. Decode succeeds, messageType is HELLO: this is the peer proposal.
 *    - If hs mode is already COMPLETE or REJECTED, this is a no-op (does not
 *      re-latch); returns OK.
 *    - Else if the decoded senderRole equals hs's own localRole (both sides
 *      claiming the same human role): latches REJECTED with reason
 *      ROLE_CONFLICT, arms an outgoing REJECT(ROLE_CONFLICT) reply; returns
 *      OK.
 *    - Else compares the decoded peer config against hs's own localConfig:
 *      requires configurationVersion, protocolVersion, canonicalSchemaVersion,
 *      replayFormatVersion all equal, and requires
 *      NativeMatchConfigV1_Digest(&peerConfig) byte-equal to
 *      NativeMatchConfigV1_Digest(&localConfig) (the full 32-byte SHA-256
 *      compare, not the lockstep bundle's truncated identity -- this is the
 *      one-time full-identity check the truncated per-frame identity relies
 *      on having already happened).  Mismatch: latches REJECTED with reason
 *      CONFIG_MISMATCH, arms an outgoing REJECT(CONFIG_MISMATCH) reply;
 *      returns OK.  Match: latches COMPLETE with result.agreedConfig set to
 *      hs's own localConfig (byte-identical to the peer proposal by
 *      construction of the check just performed), result.peerRole the
 *      decoded senderRole, result.rejectReason 0; arms an outgoing ACCEPT
 *      reply; returns OK.
 * 4. Decode succeeds, messageType is REJECT: if hs mode is still HELLO_SENT
 *    (this side has not yet independently validated a HELLO from the peer),
 *    latches REJECTED locally too, using the peer-reported rejectReason
 *    verbatim (never inventing a new reason), and arms an outgoing REJECT
 *    reply carrying that same reason (this module's documented choice:
 *    "keep composing the same REJECT-acknowledging state" rather than
 *    arming nothing).  If hs mode is already COMPLETE or REJECTED, no-op.
 *    Returns OK.
 * 5. Decode succeeds, messageType is ACCEPT: this only confirms the peer also
 *    completed on its own side; a bare ACCEPT never moves this side from
 *    HELLO_SENT to COMPLETE without this side having independently decoded
 *    and validated the peer's own HELLO first in step 3 -- completion is
 *    always earned by this side's own comparison, never trusted from the
 *    peer's say-so.  If hs mode is already COMPLETE or REJECTED, no-op.  If
 *    hs mode is still HELLO_SENT, no-op (keeps waiting for the peer's HELLO;
 *    the caller keeps calling ComposeMessage, which keeps resending this
 *    side's own HELLO, and keeps polling for the peer's HELLO to arrive).
 *    Returns OK.
 */
enum NativeLockstepHandshakeAcceptResult NativeLockstepHandshake_AcceptMessage(struct NativeLockstepHandshake *hs, const uint8_t *bytes,
                                                                               size_t size);

enum NativeLockstepHandshakeMode NativeLockstepHandshake_Mode(const struct NativeLockstepHandshake *hs);

/* NULL while mode is IDLE or HELLO_SENT; non-NULL once COMPLETE or REJECTED. */
const struct NativeLockstepHandshakeResult *NativeLockstepHandshake_Result(const struct NativeLockstepHandshake *hs);

#endif
