#include "platform/native_lockstep_handshake.h"
#include "platform/native_virtual_datagram.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define HANDSHAKE_BYTES NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES
#define SLOT_A 0u /* CAB1_HUMAN; also datagram endpoint 0. */
#define SLOT_B 1u /* CAB2_HUMAN; also datagram endpoint 1. */
#define QUEUE_CAPACITY 8u

static void FillConfig(struct NativeMatchConfigV1 *config, uint32_t trackID)
{
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = trackID;
	config->gameMode1 = UINT32_C(0x11223344);
	config->gameMode2 = UINT32_C(0x55667788);
	config->rules = UINT32_C(0x99aabbcc);
	config->lapCount = 3;
	config->tickRateNumerator = 60;
	config->tickRateDenominator = 1;
	config->masterSeed = UINT64_C(0x0123456789abcdef);
	for (uint8_t i = 0; i < NATIVE_SHA256_DIGEST_BYTES; i++)
	{
		config->buildIdentity[i] = (uint8_t)(0xa0u + i);
		config->contentIdentity[i] = (uint8_t)(0xc0u + i);
		config->botRulesDigest[i] = (uint8_t)(0x10u + i);
	}
	for (uint8_t i = 0; i <= 5; i++)
	{
		config->slots[i].characterID = i;
		config->slots[i].difficulty = i < 2 ? 2 : 3;
	}
}

static int EncodeConfig(const struct NativeMatchConfigV1 *config, uint8_t bytes[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES, NULL);
	CHECK(NativeMatchConfigV1_Encode(&writer, config));
	CHECK(NativeCodecWriter_Size(&writer) == NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES);
	return 0;
}

/* Recomputes the FNV-1a 64 body digest so a hand-corrupted record is still
 * a structurally sealed but semantically wrong wire record, exactly the
 * shape a corrupted real transport payload would take. */
static void Reseal(uint8_t *bytes)
{
	struct NativeCodecDigest64 digest;

	NativeCodecDigest64_Init(&digest);
	NativeCodecDigest64_Update(&digest, bytes, NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET);
	for (uint32_t i = 0; i < 8u; i++)
	{
		bytes[NATIVE_LOCKSTEP_HANDSHAKE_V1_DIGEST_OFFSET + i] = (uint8_t)(digest.value >> (8u * i));
	}
}

/* Drains every ready record addressed to destination and hands each straight
 * to receiver's AcceptMessage. Returns the number of records drained; the
 * caller checks that count itself. DROP creates no queued delivery at all, so
 * a message routed that way drains zero records here. */
static int DrainAndAccept(struct NativeVirtualDatagramPair *pair, uint32_t destination, struct NativeLockstepHandshake *receiver,
                          enum NativeLockstepHandshakeAcceptResult *lastResultOut, int *countOut)
{
	uint8_t bytes[HANDSHAKE_BYTES];
	size_t size;
	struct NativeVirtualDatagramMetadata metadata;
	enum NativeVirtualDatagramReceiveResult received;
	int count = 0;

	for (;;)
	{
		size = sizeof(bytes);
		received = NativeVirtualDatagramPair_Receive(pair, destination, bytes, &size, &metadata);
		if (received == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY)
			break;
		CHECK(received == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(size == HANDSHAKE_BYTES);
		count++;
		if (lastResultOut)
			*lastResultOut = NativeLockstepHandshake_AcceptMessage(receiver, bytes, size);
		else
			CHECK(NativeLockstepHandshake_AcceptMessage(receiver, bytes, size) != NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE);
	}
	if (countOut)
		*countOut = count;
	return 0;
}

/*
 * Fault 1 (loss + retransmit): A composes and sends its HELLO with a DROP
 * route, so it never arrives, then composes it again (byte-identical, since
 * ComposeMessage is a pure function of hs state) and sends the second copy
 * with a DELIVER route so it actually reaches B. B's own HELLO is sent
 * straight through, undropped. Both AcceptMessage calls drive both sides to
 * COMPLETE with byte-identical agreedConfig.
 */
static int TestLossAndRetransmit(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][HANDSHAKE_BYTES];
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloA1[HANDSHAKE_BYTES];
	uint8_t helloA2[HANDSHAKE_BYTES];
	uint8_t helloB[HANDSHAKE_BYTES];
	size_t sizeA1 = 0;
	size_t sizeA2 = 0;
	size_t sizeB = 0;
	struct NativeVirtualDatagramRoute drop = { NATIVE_VIRTUAL_DATAGRAM_DROP, 0u, 0u };
	struct NativeVirtualDatagramRoute deliver = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	enum NativeLockstepHandshakeAcceptResult resultB = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE;
	enum NativeLockstepHandshakeAcceptResult resultA = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE;
	int countA = 0;
	int countB = 0;
	const struct NativeLockstepHandshakeResult *finalA;
	const struct NativeLockstepHandshakeResult *finalB;
	uint8_t agreedBytesA[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];
	uint8_t agreedBytesB[NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES];

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);

	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloA1, sizeof(helloA1), &sizeA1) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, helloA1, sizeA1, &drop));

	/* The retransmit: composed again, deterministically the same bytes. */
	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloA2, sizeof(helloA2), &sizeA2) == 1);
	CHECK(sizeA2 == sizeA1);
	CHECK(memcmp(helloA1, helloA2, sizeA1) == 0);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, helloA2, sizeA2, &deliver));

	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloB, sizeof(helloB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, helloB, sizeB, &deliver));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 0u));

	/* Exactly one record reaches B: the dropped copy created no delivery. */
	CHECK(DrainAndAccept(&pair, SLOT_B, &b, &resultB, &countB) == 0);
	CHECK(countB == 1 && resultB == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(DrainAndAccept(&pair, SLOT_A, &a, &resultA, &countA) == 0);
	CHECK(countA == 1 && resultA == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);

	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	finalA = NativeLockstepHandshake_Result(&a);
	finalB = NativeLockstepHandshake_Result(&b);
	CHECK(finalA != NULL && finalB != NULL);
	CHECK(finalA->rejectReason == 0u && finalB->rejectReason == 0u);
	CHECK(EncodeConfig(&finalA->agreedConfig, agreedBytesA) == 0);
	CHECK(EncodeConfig(&finalB->agreedConfig, agreedBytesB) == 0);
	CHECK(memcmp(agreedBytesA, agreedBytesB, sizeof(agreedBytesA)) == 0);
	return 0;
}

/*
 * Fault 2 (delay): A's HELLO is routed to arrive several virtual steps late,
 * still eventually delivered. B's own HELLO is delivered immediately.
 * Advancing the pair past the delayed delivery step still lets B receive and
 * validate A's HELLO correctly, reaching COMPLETE exactly as the undelayed
 * case would.
 */
static int TestDelay(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][HANDSHAKE_BYTES];
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloA[HANDSHAKE_BYTES];
	uint8_t helloB[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	const uint64_t sendStep = 0u;
	const uint64_t lateStep = 5u;
	struct NativeVirtualDatagramRoute delayed = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, lateStep, 0u };
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, sendStep, 0u };
	enum NativeLockstepHandshakeAcceptResult resultB = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE;
	int countB = 0;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);

	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloA, sizeof(helloA), &sizeA) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, helloA, sizeA, &delayed));
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloB, sizeof(helloB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, helloB, sizeB, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, sendStep));

	/* B's own HELLO reaches A immediately, but A's HELLO has not reached B
	 * yet, so B is still waiting. */
	CHECK(DrainAndAccept(&pair, SLOT_A, &a, NULL, NULL) == 0);
	CHECK(DrainAndAccept(&pair, SLOT_B, &b, &resultB, &countB) == 0);
	CHECK(countB == 0);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_HELLO_SENT);
	CHECK(NativeLockstepHandshake_Result(&b) == NULL);

	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, lateStep));
	CHECK(DrainAndAccept(&pair, SLOT_B, &b, &resultB, &countB) == 0);
	CHECK(countB == 1 && resultB == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
	CHECK(NativeLockstepHandshake_Result(&b) != NULL);
	CHECK(NativeLockstepHandshake_Result(&b)->rejectReason == 0u);
	return 0;
}

/*
 * Fault 3 (reorder + duplication): A's HELLO is sent twice -- once as the
 * original, once as a simulated retransmission the caller decided to make
 * before knowing whether the first arrived -- routed with DUPLICATE so both
 * deliveries invert relative to real send order (the second copy is routed
 * to arrive first). B's AcceptMessage handles the first delivery by reaching
 * COMPLETE; the later, second delivery of the same HELLO is a safe no-op:
 * mode stays COMPLETE and Result() is unchanged.
 */
static int TestReorderAndDuplication(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][HANDSHAKE_BYTES];
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloA[HANDSHAKE_BYTES];
	uint8_t helloB[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	/* Two deliveries from one send, first at step 4, second (the "earlier"
	 * send-order copy) at step 2 -- inverted relative to real send order. */
	struct NativeVirtualDatagramRoute duplicate = { NATIVE_VIRTUAL_DATAGRAM_DUPLICATE, 4u, 2u };
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	struct NativeLockstepHandshakeResult latched;
	const struct NativeLockstepHandshakeResult *result;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);

	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloA, sizeof(helloA), &sizeA) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, helloA, sizeA, &duplicate));
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloB, sizeof(helloB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, helloB, sizeB, &immediate));

	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 4u));
	CHECK(DrainAndAccept(&pair, SLOT_A, &a, NULL, NULL) == 0);
	{
		uint8_t recv1[HANDSHAKE_BYTES];
		uint8_t recv2[HANDSHAKE_BYTES];
		size_t recvSize1 = sizeof(recv1);
		size_t recvSize2 = sizeof(recv2);
		struct NativeVirtualDatagramMetadata metadata1;
		struct NativeVirtualDatagramMetadata metadata2;

		/* Both duplicates arrive, in delivery-step order (step 2 first, then
		 * step 4), so the first delivery received here is the one whose
		 * delivery step is earlier than the send-order original's. */
		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, recv1, &recvSize1, &metadata1) == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(recvSize1 == HANDSHAKE_BYTES);
		CHECK(NativeLockstepHandshake_AcceptMessage(&b, recv1, recvSize1) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
		CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
		result = NativeLockstepHandshake_Result(&b);
		CHECK(result != NULL);
		CHECK(result->rejectReason == 0u);
		latched = *result;

		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, recv2, &recvSize2, &metadata2) == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
		CHECK(recvSize2 == HANDSHAKE_BYTES);
		/* Byte-identical to the first delivery: the same HELLO, delivered
		 * twice. */
		CHECK(memcmp(recv1, recv2, HANDSHAKE_BYTES) == 0);
		/* The second, later delivery of the same HELLO is a safe no-op: mode
		 * is already terminal (COMPLETE), so it must not re-latch. */
		CHECK(NativeLockstepHandshake_AcceptMessage(&b, recv2, recvSize2) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
		CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_COMPLETE);
		result = NativeLockstepHandshake_Result(&b);
		CHECK(result != NULL);
		CHECK(memcmp(result, &latched, sizeof(latched)) == 0);

		CHECK(NativeVirtualDatagramPair_Receive(&pair, SLOT_B, recv1, &recvSize1, &metadata1) == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY);
	}
	return 0;
}

/*
 * Fault 4 (malformed delivery): A composes a valid HELLO, then one byte of
 * the payload is hand-corrupted after composing but before sending it through
 * the virtual pair, so the corruption survives real transport-shaped
 * delivery rather than just a direct in-memory call. B (still HELLO_SENT)
 * receives the corrupted record and must reach REJECTED with MALFORMED,
 * cleanly, with no crash. B's own outgoing message becomes a REJECT the
 * other side can eventually receive; delivering that REJECT to A (still
 * HELLO_SENT, since it never got a valid HELLO from B either) latches A
 * REJECTED with the same reason.
 */
static int TestMalformedDelivery(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][HANDSHAKE_BYTES];
	struct NativeMatchConfigV1 config;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloA[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	enum NativeLockstepHandshakeAcceptResult resultB = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK;
	int countB = 0;
	const struct NativeLockstepHandshakeResult *resultOut;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	FillConfig(&config, UINT32_C(0x01020304));
	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);

	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloA, sizeof(helloA), &sizeA) == 1);
	CHECK(sizeA == HANDSHAKE_BYTES);
	/* Hand-corrupt one payload byte (inside the nested config region) after
	 * composing but before sending, and do NOT reseal the digest: a real
	 * corrupted-in-flight datagram would not carry a digest recomputed over
	 * the corruption either. */
	helloA[20] ^= 0xFFu;
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, helloA, sizeA, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 0u));

	CHECK(DrainAndAccept(&pair, SLOT_B, &b, &resultB, &countB) == 0);
	CHECK(countB == 1);
	CHECK(resultB == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	resultOut = NativeLockstepHandshake_Result(&b);
	CHECK(resultOut != NULL);
	CHECK(resultOut->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED);

	/* Idempotent on an already-terminal handshake: delivering the same
	 * corrupted record again must not crash or re-latch. */
	{
		struct NativeLockstepHandshakeResult before = *resultOut;
		CHECK(NativeLockstepHandshake_AcceptMessage(&b, helloA, sizeA) == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT);
		CHECK(memcmp(NativeLockstepHandshake_Result(&b), &before, sizeof(before)) == 0);
	}

	/* B's own outgoing message is now a REJECT(MALFORMED) the other side can
	 * eventually receive. */
	{
		uint8_t rejectBytes[HANDSHAKE_BYTES];
		size_t rejectSize = 0;
		struct NativeCodecReader reader;
		struct NativeLockstepHandshakeMessageV1 decoded;
		uint32_t cause = NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE;

		CHECK(NativeLockstepHandshake_ComposeMessage(&b, rejectBytes, sizeof(rejectBytes), &rejectSize) == 1);
		NativeCodecReader_Init(&reader, rejectBytes, rejectSize);
		CHECK(NativeLockstepHandshakeMessageV1_Decode(&reader, &decoded, &cause));
		CHECK(cause == NATIVE_LOCKSTEP_HANDSHAKE_FAULT_NONE);
		CHECK(decoded.messageType == (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_MESSAGE_REJECT);
		CHECK(decoded.rejectReason == (uint8_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED);

		CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, rejectBytes, rejectSize, &immediate));
		CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 1u));
		{
			int countA = 0;
			enum NativeLockstepHandshakeAcceptResult resultA = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_FAULT;

			CHECK(DrainAndAccept(&pair, SLOT_A, &a, &resultA, &countA) == 0);
			CHECK(countA == 1 && resultA == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
			CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
			CHECK(NativeLockstepHandshake_Result(&a) != NULL);
			CHECK(NativeLockstepHandshake_Result(&a)->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_MALFORMED);
		}
	}
	return 0;
}

/*
 * Fault 5 (real mismatch end-to-end): two NativeMatchConfigV1 proposals
 * differ only in trackID. Both peers run a full HELLO exchange over the
 * virtual pair; both sides independently reach REJECTED with
 * CONFIG_MISMATCH, since each compares the peer proposal against its own
 * local config rather than trusting the other side's verdict.
 */
static int TestRealMismatchEndToEnd(void)
{
	struct NativeVirtualDatagramPair pair;
	struct NativeVirtualDatagramSlot slots[QUEUE_CAPACITY];
	uint8_t storage[QUEUE_CAPACITY][HANDSHAKE_BYTES];
	struct NativeMatchConfigV1 configA;
	struct NativeMatchConfigV1 configB;
	struct NativeLockstepHandshake a;
	struct NativeLockstepHandshake b;
	uint8_t helloA[HANDSHAKE_BYTES];
	uint8_t helloB[HANDSHAKE_BYTES];
	size_t sizeA = 0;
	size_t sizeB = 0;
	struct NativeVirtualDatagramRoute immediate = { NATIVE_VIRTUAL_DATAGRAM_DELIVER, 0u, 0u };
	enum NativeLockstepHandshakeAcceptResult resultA = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE;
	enum NativeLockstepHandshakeAcceptResult resultB = NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_REJECTED_LOCAL_STATE;
	int countA = 0;
	int countB = 0;
	const struct NativeLockstepHandshakeResult *finalA;
	const struct NativeLockstepHandshakeResult *finalB;

	CHECK(NativeVirtualDatagramPair_Init(&pair, slots, QUEUE_CAPACITY, &storage[0][0], sizeof(storage[0])));
	FillConfig(&configA, UINT32_C(0x01020304));
	FillConfig(&configB, UINT32_C(0x0A0B0C0D));
	CHECK(configA.trackID != configB.trackID);

	NativeLockstepHandshake_Init(&a);
	NativeLockstepHandshake_Init(&b);
	CHECK(NativeLockstepHandshake_Begin(&a, &configA, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN) == 1);
	CHECK(NativeLockstepHandshake_Begin(&b, &configB, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN) == 1);

	CHECK(NativeLockstepHandshake_ComposeMessage(&a, helloA, sizeof(helloA), &sizeA) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_A, helloA, sizeA, &immediate));
	CHECK(NativeLockstepHandshake_ComposeMessage(&b, helloB, sizeof(helloB), &sizeB) == 1);
	CHECK(NativeVirtualDatagramPair_Send(&pair, SLOT_B, helloB, sizeB, &immediate));
	CHECK(NativeVirtualDatagramPair_AdvanceTo(&pair, 0u));

	CHECK(DrainAndAccept(&pair, SLOT_B, &b, &resultB, &countB) == 0);
	CHECK(countB == 1 && resultB == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);
	CHECK(DrainAndAccept(&pair, SLOT_A, &a, &resultA, &countA) == 0);
	CHECK(countA == 1 && resultA == NATIVE_LOCKSTEP_HANDSHAKE_ACCEPT_OK);

	/* Both sides independently detect the mismatch: neither trusted the
	 * other's say-so. */
	CHECK(NativeLockstepHandshake_Mode(&a) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	CHECK(NativeLockstepHandshake_Mode(&b) == NATIVE_LOCKSTEP_HANDSHAKE_REJECTED);
	finalA = NativeLockstepHandshake_Result(&a);
	finalB = NativeLockstepHandshake_Result(&b);
	CHECK(finalA != NULL && finalB != NULL);
	CHECK(finalA->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH);
	CHECK(finalB->rejectReason == (uint32_t)NATIVE_LOCKSTEP_HANDSHAKE_REJECT_CONFIG_MISMATCH);
	return 0;
}

int main(void)
{
	CHECK(HANDSHAKE_BYTES == 284u);
	CHECK(TestLossAndRetransmit() == 0);
	CHECK(TestDelay() == 0);
	CHECK(TestReorderAndDuplication() == 0);
	CHECK(TestMalformedDelivery() == 0);
	CHECK(TestRealMismatchEndToEnd() == 0);
	puts("native_lockstep_handshake_fault_test: passed");
	return 0;
}
