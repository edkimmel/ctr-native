#include "platform/native_lockstep_peer_link.h"

#include "native_lockstep_peer_link_test_fixture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Single-process, real-socket hardening tests for
 * platform/native_lockstep_peer_link.c (Task 5b, a follow-up on Task 5,
 * commit 8fdfb0413), closing GAP 1 from that commit's independent review:
 * the two real integration bugs Task 5 fixed (mode-routing-by-wire-size and
 * the retransmit-before-poll liveness ordering fix) had no deterministic
 * single-process test -- the only coverage was
 * tests/native_lockstep_peer_link_process_test.c, whose exercise of the
 * early-bundle staging/replay path and the ordering fix depends on
 * incidental OS process scheduling, not anything that test controls.
 *
 * Every test below opens two real struct NativeLockstepPeerLink instances,
 * each on its own real loopback UDP socket -- NativeUdpTransport underneath
 * is genuinely real -- but collapses both peer roles into this ONE test
 * process, so this file fully controls call ordering itself instead of
 * depending on OS scheduling of a second process. Several tests
 * deliberately open side B before its peer exists (or vice versa) so that
 * side's own Open()-time HELLO is silently lost -- the same real-socket
 * startup race tests/native_lockstep_peer_link_process_test.c's own
 * DriveParentSide/native_lockstep_peer_link_helper.c comments describe --
 * which is what makes it possible, deterministically and on demand, to
 * control exactly which single HELLO datagram a side ever receives and
 * when, using only the real NativeUdpTransport/NativeLockstepPeerLink
 * public API (occasionally reaching directly into the plain, non-opaque
 * struct NativeLockstepPeerLink fields -- e.g. .transport, .session,
 * .earlyBundleCount -- exactly the way NativeLockstepPeerLink_Session
 * already hands out direct access to another one of those fields).
 *
 * Fixed loopback test ports, in the 48000-48999 band, distinct from
 * NATIVE_UDP_TRANSPORT_PROCESS_TEST_PORT (48037,
 * tests/native_udp_transport_process_test.c) and from PARENT_PORT/
 * HELPER_PORT (48110/48111, tests/native_lockstep_peer_link_process_test.c):
 * this file uses 48200-48256.
 */
#define TEST1_PORT_A 48200u
#define TEST1_PORT_B 48201u
#define TEST2_PORT_A 48202u
#define TEST2_PORT_B 48203u
#define TEST3_GOOD_PORT_A 48204u
#define TEST3_GOOD_PORT_B 48205u
#define TEST3_BAD_PORT_A 48206u
#define TEST3_BAD_PORT_B 48207u
#define TEST4_PORT_A 48208u
#define TEST4_PORT_B 48209u
#define TEST4_PORT_BYSTANDER 48210u
/* Aux-route tests (MS-5, MS-5b) continue the same band at 48211-48232. */
#define AUX_ROUNDTRIP_PORT_A 48211u
#define AUX_ROUNDTRIP_PORT_B 48212u
#define AUX_HANDSHAKING_PORT_A 48213u
#define AUX_HANDSHAKING_PORT_B 48214u
#define AUX_INTERLEAVE_PORT_A 48215u
#define AUX_INTERLEAVE_PORT_B 48216u
#define AUX_OVERFLOW_PORT_A 48217u
#define AUX_OVERFLOW_PORT_B 48218u
#define AUX_REFUSAL_PORT_A 48219u
#define AUX_REFUSAL_PORT_B 48220u
#define AUX_RESET_PORT_A 48221u
#define AUX_RESET_PORT_B 48222u
#define AUX_ODD_SIZE_PORT_A 48223u
#define AUX_ODD_SIZE_PORT_B 48224u
#define AUX_FOREIGN_PORT_A 48225u
#define AUX_FOREIGN_PORT_B 48226u
#define AUX_FOREIGN_PORT_BYSTANDER 48227u
#define AUX_OPEN_RESET_PORT 48228u
#define AUX_OPEN_RESET_PEER_PORT 48229u
#define AUX_OPEN_FAIL_PORT 48230u
#define AUX_TERMINAL_PORT_A 48231u
#define AUX_TERMINAL_PORT_B 48232u
/* Foreign-identity drop tests (docs/LOCKSTEP_RACE_MILESTONE.md LR-14, LR-S6)
 * continue the band at 48233-48248. */
#define FOREIGN_STAGING_PORT_A 48233u
#define FOREIGN_STAGING_PORT_B 48234u
#define FOREIGN_RUNNING_PORT_A 48235u
#define FOREIGN_RUNNING_PORT_B 48236u
#define FOREIGN_CORRUPT_PORT_A 48237u
#define FOREIGN_CORRUPT_PORT_B 48238u
#define FOREIGN_FLIPPED_PORT_A 48239u
#define FOREIGN_FLIPPED_PORT_B 48240u
#define FOREIGN_CORRUPT_STAGED_PORT_A 48241u
#define FOREIGN_CORRUPT_STAGED_PORT_B 48242u
#define CURRENT_BAD_DELAY_PORT_A 48243u
#define CURRENT_BAD_DELAY_PORT_B 48244u
#define CURRENT_BAD_SLOT_PORT_A 48245u
#define CURRENT_BAD_SLOT_PORT_B 48246u
#define FOREIGN_MIXED_STAGED_PORT_A 48247u
#define FOREIGN_MIXED_STAGED_PORT_B 48248u
/* Verbatim bundle send tests (docs/LOCKSTEP_RACE_MILESTONE.md LR-3, LR-S9)
 * continue the band at 48249-48256. */
#define VERBATIM_SEND_PORT_A 48249u
#define VERBATIM_SEND_PORT_B 48250u
#define VERBATIM_REFUSE_PORT_A 48251u
#define VERBATIM_REFUSE_PORT_B 48252u
#define VERBATIM_HANDSHAKE_PORT_A 48253u
#define VERBATIM_HANDSHAKE_PORT_B 48254u
#define VERBATIM_DIVERGED_PORT_A 48255u
#define VERBATIM_DIVERGED_PORT_B 48256u

/* Wire offsets in a 128-byte bundle (platform/native_lockstep_protocol.c,
 * NativeLockstepBundle_WriteBody): the 8-byte match identity follows the
 * four u32 fields magic, bundle version, encoded size, and protocol
 * version; byte 40 lies in the first pad entry (the byte
 * tests/native_arcade_netplay_test.c corrupts too). Both lie inside the
 * digested body (bytes 0..119). */
#define BUNDLE_IDENTITY_OFFSET 16u
#define BUNDLE_IDENTITY_BYTES 8u
#define BUNDLE_PAD_BYTE_OFFSET 40u

/* Real loopback delivery is asynchronous relative to sendto returning
 * (tests/native_udp_transport_test.c's own PollReceive helper documents the
 * same thing): every spin below is bounded by an attempt count, never by
 * wall-clock time. */
#define SPIN_BUDGET 20000u

/* Poll-only spin: never calls Retransmit, so it cannot itself queue any
 * further HELLO for the peer. Used wherever a test needs to control exactly
 * how many HELLO datagrams a side sends. */
static enum NativeLockstepPeerLinkMode PumpPollUntilMode(struct NativeLockstepPeerLink *link, enum NativeLockstepPeerLinkMode expected)
{
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		if (NativeLockstepPeerLink_Mode(link) == expected)
		{
			break;
		}
	}
	return NativeLockstepPeerLink_Mode(link);
}

/* Also retransmits every attempt: the normal caller-driven-cadence usage
 * pattern (NativeLockstepPeerLink_Retransmit doc comment), for scenarios
 * that do not need fine control over exactly how many HELLOs get sent. */
static enum NativeLockstepPeerLinkMode PumpUntilMode(struct NativeLockstepPeerLink *link, enum NativeLockstepPeerLinkMode expected)
{
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Retransmit(link);
		NativeLockstepPeerLink_Poll(link);
		if (NativeLockstepPeerLink_Mode(link) == expected)
		{
			break;
		}
	}
	return NativeLockstepPeerLink_Mode(link);
}

/* Poll-only spin that stops once at least expectedCount early bundles have
 * been staged (struct NativeLockstepPeerLink's earlyBundleCount field is
 * plain and directly readable, not opaque). */
static void PumpPollUntilEarlyCount(struct NativeLockstepPeerLink *link, uint32_t expectedCount)
{
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		if (link->earlyBundleCount >= expectedCount)
		{
			break;
		}
	}
}

/* Bounded spin around NativeUdpTransport_Receive directly (bypassing
 * NativeLockstepPeerLink_Poll entirely), mirroring
 * tests/native_udp_transport_test.c's own PollReceive helper. */
static enum NativeUdpTransportReceiveResult PumpRawReceive(struct NativeUdpTransport *transport, uint8_t *bytesOut, size_t capacity,
	size_t *sizeOut, struct NativeUdpTransportAddress *senderOut)
{
	uint32_t attempt;
	enum NativeUdpTransportReceiveResult result = NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		result = NativeUdpTransport_Receive(transport, bytesOut, capacity, sizeOut, senderOut);
		if (result != NATIVE_UDP_TRANSPORT_RECEIVE_EMPTY)
		{
			return result;
		}
	}
	return result;
}

/*
 * GAP 1, bullet 1 (early-bundle arrival): side A is driven all the way to
 * RUNNING and composes/sends real bundles over its real socket while side B
 * has never once called Poll, so those bundles sit unconsumed in B's real
 * OS receive queue. Genuinely forcing them through the staging path (not
 * merely happening to be processed directly, because B's own completing
 * HELLO datagram would otherwise always be queued ahead of them -- A can
 * only ever send a bundle after A's own handshake has completed, and every
 * HELLO A ever sends necessarily precedes that) requires deterministically
 * controlling which single HELLO datagram is the one that finally lets B's
 * handshake complete, and delivering it only after the bundles: A is opened
 * before B exists (so A's own Open()-time HELLO to B is silently lost, the
 * real-socket startup race described in NativeLockstepPeerLink_Open's
 * process-test comment), then B is opened once A genuinely exists, so B's
 * own HELLO reaches A fine (which is how A itself will reach RUNNING) while
 * A sends exactly one fresh HELLO of its own -- via a single explicit
 * Retransmit call, now that B's socket genuinely exists -- which is the one
 * and only HELLO datagram B will ever receive from A. That HELLO is drained
 * directly off B's real socket (bypassing NativeLockstepPeerLink_Poll via
 * the transport field) and held back, so B's own link state stays untouched
 * at HANDSHAKING while A's bundles arrive and get genuinely staged; the
 * held-back HELLO is then replayed onto the wire so B's own Poll finally
 * completes the handshake and, per the documented contract, replays the
 * staged bundles into the freshly opened session.
 */
static int TestEarlyBundleArrival(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeUdpTransportAddress sender;
	uint8_t savedHello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	size_t savedHelloSize = 0;
	struct NativeLockstepSession *session;
	struct NativeLockstepSessionFrameInputs inputs;

	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(NULL) == 0u);

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST1_PORT_A));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST1_PORT_B));

	/* A first: B does not exist yet, so A's own Open()-time HELLO to B is
	 * silently lost (nothing is listening on B's port yet). A's own socket
	 * is the one a stray "destination unreachable" for that lost send could
	 * ever affect, and this test only ever drives A through the public
	 * NativeLockstepPeerLink_Poll API afterward (never a raw receive on
	 * A's own socket), which already tolerates and drains past a spurious
	 * receive error. */
	CHECK(NativeLockstepPeerLink_Open(&linkA, (uint16_t)TEST1_PORT_A, &addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	/* B second: A already exists, so B's own Open()-time HELLO to A
	 * genuinely reaches A's real socket and sits there, unconsumed. Because
	 * B's own send here lands on a real listener, B's own socket is never
	 * put in that same "stray unreachable" state, which matters below: this
	 * test does do a raw receive directly on B's own socket. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST1_PORT_B, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(&linkB) == 0u);

	/* A's own original HELLO to B was lost above, so A needs one more
	 * resend -- now that B's real socket genuinely exists -- before B can
	 * ever see a HELLO from A at all. This is the one and only HELLO
	 * datagram B will receive from A in this test. */
	NativeLockstepPeerLink_Retransmit(&linkA);

	/* Drain that one HELLO directly off B's real socket, bypassing
	 * NativeLockstepPeerLink_Poll entirely, so B's own link/handshake state
	 * is left completely untouched at HANDSHAKING; the bytes are held back
	 * for later, deliberately delayed redelivery. */
	memset(&sender, 0, sizeof(sender));
	CHECK(PumpRawReceive(&linkB.transport, savedHello, sizeof(savedHello), &savedHelloSize, &sender) == NATIVE_UDP_TRANSPORT_RECEIVE_OK);
	CHECK(savedHelloSize == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES);
	CHECK(sender.ipv4 == addrA.ipv4);
	CHECK(sender.port == addrA.port);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	/* B's own Open()-time HELLO to A (above) already reached A fine, so a
	 * plain Poll (never Retransmit) is all A needs to discover completion;
	 * this cannot itself queue any further HELLO for B. */
	CHECK(PumpPollUntilMode(&linkA, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* A composes and sends two real bundles over its real socket while B
	 * has never once called Poll. */
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u));
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 1u));

	/* Drain B: both bundles arrive while B is still genuinely HANDSHAKING
	 * (it has still never received any handshake datagram at all), so both
	 * must be staged -- not dropped, not causing an error. */
	PumpPollUntilEarlyCount(&linkB, 2u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(linkB.earlyBundleCount == 2u);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(&linkB) == 0u);

	/* Replay the held-back HELLO onto the real wire: this is what finally
	 * lets B's own handshake complete and, per the documented contract,
	 * replay the staged bundles into the freshly opened session before
	 * Poll returns. */
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(linkB.earlyBundleCount == 0u);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(&linkB) == 0u);

	/* B can take both early-arrived frames without A ever resending them. */
	session = NativeLockstepPeerLink_Session(&linkB);
	CHECK(session != NULL);
	CHECK(NativeLockstepSession_TakeFrameInputs(session, 0u, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_TakeFrameInputs(session, 1u, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * GAP 1, bullet 2 (capacity bound): same construction as
 * TestEarlyBundleArrival, but side A sends strictly more bundles than
 * NATIVE_LOCKSTEP_PEER_LINK_EARLY_BUNDLE_CAPACITY
 * (== NATIVE_LOCKSTEP_RING_CAPACITY, defined in
 * platform/native_lockstep_input_window.h and pulled in transitively here
 * through native_lockstep_session.h) can hold, before B ever polls.
 *
 * Uses inputDelay NATIVE_LOCKSTEP_MAX_INPUT_DELAY so frames
 * 0..NATIVE_LOCKSTEP_MAX_INPUT_DELAY (NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1 of
 * them) each compose with verifiedPresent 0: NativeLockstepSession_ComposeBundle
 * only attaches a verified-digest block once a frame's own recorded local
 * digest exists, which B's session -- opened only once replay begins --
 * could never have for a peer-sent frame, and a verifiedPresent-0 record is
 * never digest-compared (platform/native_lockstep_session.c:141), so every
 * early-arrived bundle here is divergence-free by construction. Padded out
 * to, and past, capacity by resending the last distinct frame's own bundle
 * (NativeLockstepSession_ComposeBundle is a read-only, side-effect-free
 * query of session state, so repeat calls for the same frame produce
 * byte-identical output and replay as DUPLICATE, not FAULT).
 */
static int TestEarlyBundleCapacityBound(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeUdpTransportAddress sender;
	uint8_t savedHello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	size_t savedHelloSize = 0;
	struct NativeLockstepSession *session;
	struct NativeLockstepSessionFrameInputs inputs;
	const uint32_t distinctFrameCount = (uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1u;
	const uint32_t duplicateFillCount = (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY - distinctFrameCount;
	const uint32_t overflowCount = 4u;
	uint32_t sent;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST2_PORT_A));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST2_PORT_B));

	/* A first: B does not exist yet, so A's own Open()-time HELLO to B is
	 * silently lost. Only the public Poll API is ever used on A afterward
	 * (never a raw receive on A's own socket), which tolerates and drains
	 * past a spurious receive error from that lost send. */
	CHECK(NativeLockstepPeerLink_Open(&linkA, (uint16_t)TEST2_PORT_A, &addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY));
	/* B second: A already exists, so B's own Open()-time HELLO to A reaches
	 * A fine, and B's own socket is never put in that same "stray
	 * unreachable" state -- this test does a raw receive directly on it. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST2_PORT_B, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MAX_INPUT_DELAY));

	/* A's own original HELLO to B was lost above; this single resend, now
	 * that B's socket genuinely exists, is the one and only HELLO B will
	 * ever receive from A in this test. */
	NativeLockstepPeerLink_Retransmit(&linkA);

	memset(&sender, 0, sizeof(sender));
	CHECK(PumpRawReceive(&linkB.transport, savedHello, sizeof(savedHello), &savedHelloSize, &sender) == NATIVE_UDP_TRANSPORT_RECEIVE_OK);
	CHECK(savedHelloSize == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	/* B's own Open()-time HELLO to A already reached A fine, so a plain
	 * Poll is all A needs to discover completion. */
	CHECK(PumpPollUntilMode(&linkA, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	for (sent = 0; sent < distinctFrameCount; sent++)
	{
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, sent));
	}
	for (sent = 0; sent < (duplicateFillCount + overflowCount); sent++)
	{
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, distinctFrameCount - 1u));
	}

	PumpPollUntilEarlyCount(&linkB, (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	/* Capacity, not overrun: exactly NATIVE_LOCKSTEP_RING_CAPACITY, never more. */
	CHECK(linkB.earlyBundleCount == (uint32_t)NATIVE_LOCKSTEP_RING_CAPACITY);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(&linkB) == overflowCount);

	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(linkB.earlyBundleCount == 0u);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(&linkB) == overflowCount);

	/* Every frame that was actually staged (0..distinctFrameCount-1) still
	 * replays correctly; the dropped overflow copies were all duplicates of
	 * the last frame, so nothing new was ever lost. */
	session = NativeLockstepPeerLink_Session(&linkB);
	CHECK(session != NULL);
	for (sent = 0; sent < distinctFrameCount; sent++)
	{
		CHECK(NativeLockstepSession_TakeFrameInputs(session, sent, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	}

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

#define REGRESSION_MAX_DRIVER_TICKS 3u

/* One real driver tick using the documented, correct call order:
 * Retransmit before Poll, every tick (NativeLockstepPeerLink_Retransmit doc
 * comment). The inner spin absorbs real loopback delivery latency only; it
 * is never counted as a driver tick. */
static enum NativeLockstepPeerLinkMode DriveOneTickCorrectOrder(struct NativeLockstepPeerLink *link)
{
	uint32_t attempt;

	NativeLockstepPeerLink_Retransmit(link);
	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		if (NativeLockstepPeerLink_Mode(link) != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
	}
	return NativeLockstepPeerLink_Mode(link);
}

/*
 * One real driver tick using the historical buggy call order the commit
 * message (8fdfb0413) fixed: Poll first, and -- exactly like a
 * "Poll(); if (mode changed) break;" loop shape -- this tick never reaches
 * a Retransmit call at all once mode leaves HANDSHAKING inside the Poll
 * spin below, reproducing the historical gap on demand: the very tick that
 * discovers local completion skips sending the one more HELLO the peer
 * might still need.
 */
static enum NativeLockstepPeerLinkMode DriveOneTickBuggyOrder(struct NativeLockstepPeerLink *link)
{
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		if (NativeLockstepPeerLink_Mode(link) != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			return NativeLockstepPeerLink_Mode(link);
		}
	}
	NativeLockstepPeerLink_Retransmit(link);
	return NativeLockstepPeerLink_Mode(link);
}

/*
 * GAP 1, bullet 3 (retransmit-before-poll ordering regression): reproduces,
 * on demand, the exact real-socket startup race the commit message
 * describes -- side A's own Open()-time HELLO is lost because side B does
 * not exist yet -- so the only HELLO of A's that ever reaches B is whichever
 * one A's driving loop sends on the tick that discovers A's own completion.
 * Two independent scenarios, on independent port pairs:
 *
 *   - Correct order (this module's documented contract): both sides reach
 *     RUNNING within REGRESSION_MAX_DRIVER_TICKS driver ticks each -- a
 *     small, fixed, deterministic bound, not a wall-clock timeout.
 *   - Buggy order (reproducing the historical gap): side A still reaches
 *     RUNNING (its own completion never depended on sending anything), but
 *     because its one completing tick skipped Retransmit, side B never
 *     receives any HELLO at all and provably stays HANDSHAKING even after
 *     the same driver-tick budget, using its own correct call order.
 */
static int TestRetransmitBeforePollRegression(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint32_t tick;
	enum NativeLockstepPeerLinkMode mode;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);

	/* Correct order. */
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST3_GOOD_PORT_A));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST3_GOOD_PORT_B));
	/* B does not exist yet: A's Open()-time HELLO to B is lost. */
	CHECK(NativeLockstepPeerLink_Open(&linkA, (uint16_t)TEST3_GOOD_PORT_A, &addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	/* A exists: B's Open()-time HELLO to A reaches A fine. */
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST3_GOOD_PORT_B, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	mode = NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING;
	for (tick = 0; tick < REGRESSION_MAX_DRIVER_TICKS; tick++)
	{
		mode = DriveOneTickCorrectOrder(&linkA);
		if (mode != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
	}
	CHECK(tick < REGRESSION_MAX_DRIVER_TICKS);
	CHECK(mode == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	mode = NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING;
	for (tick = 0; tick < REGRESSION_MAX_DRIVER_TICKS; tick++)
	{
		mode = DriveOneTickCorrectOrder(&linkB);
		if (mode != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
	}
	CHECK(tick < REGRESSION_MAX_DRIVER_TICKS);
	CHECK(mode == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);

	/* Buggy order: reproduces the historical liveness gap on demand. */
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST3_BAD_PORT_A));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST3_BAD_PORT_B));
	CHECK(NativeLockstepPeerLink_Open(&linkA, (uint16_t)TEST3_BAD_PORT_A, &addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST3_BAD_PORT_B, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	mode = NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING;
	for (tick = 0; tick < REGRESSION_MAX_DRIVER_TICKS; tick++)
	{
		mode = DriveOneTickBuggyOrder(&linkA);
		if (mode != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
	}
	/* A's own completion never depended on sending anything, so the buggy
	 * order does not stop A itself from reaching RUNNING. */
	CHECK(mode == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	mode = NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING;
	for (tick = 0; tick < REGRESSION_MAX_DRIVER_TICKS; tick++)
	{
		mode = DriveOneTickCorrectOrder(&linkB);
		if (mode != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
	}
	/* The liveness gap: B never received any HELLO at all (A never sent one
	 * back on its completing tick), so B provably stays stuck in
	 * HANDSHAKING even with a full driver-tick budget and B's own correct
	 * call order. */
	CHECK(mode == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * GAP 2 (sender-address filtering): a spurious, well-formed lockstep bundle
 * datagram arriving at B's real socket from a different source address than
 * B's own configured peer must be silently discarded by
 * NativeLockstepPeerLink_Poll -- never staged, never accepted, and never
 * disturbing B's mode -- while the genuinely configured peer's own traffic
 * for the same frame is accepted normally, proving this is specifically
 * about sender address and not an accidental drop of legitimate traffic.
 */
static int TestSenderAddressFiltering(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransport bystander = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t forgedBundle[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t forgedBundleSize = 0;
	struct NativeLockstepSession *session;
	struct NativeLockstepSessionFrameInputs inputs;
	uint32_t attempt;
	enum NativeLockstepSessionResult result;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)TEST4_PORT_A));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)TEST4_PORT_B));

	CHECK(NativeLockstepPeerLink_Open(&linkA, (uint16_t)TEST4_PORT_A, &addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)TEST4_PORT_B, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	CHECK(PumpUntilMode(&linkA, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(PumpUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* A well-formed frame-0 bundle, composed from A's own real session so
	 * every content check (match identity, protocol version, input delay,
	 * sender slot) would otherwise pass cleanly. */
	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 0u, forgedBundle, sizeof(forgedBundle), &forgedBundleSize));

	CHECK(NativeUdpTransport_GlobalInit());
	CHECK(NativeUdpTransport_Open(&bystander, (uint16_t)TEST4_PORT_BYSTANDER));

	/* Sent from the bystander's address, not from A's: must be silently
	 * discarded, not staged/accepted, and must not disturb B's mode. */
	CHECK(NativeUdpTransport_Send(&bystander, &addrB, forgedBundle, forgedBundleSize));
	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(&linkB);
	}
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	session = NativeLockstepPeerLink_Session(&linkB);
	CHECK(session != NULL);
	CHECK(NativeLockstepSession_TakeFrameInputs(session, 0u, &inputs) == NATIVE_LOCKSTEP_SESSION_STALL);

	/* The same content, sent for real by the configured peer, is accepted
	 * normally. */
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u));
	result = NATIVE_LOCKSTEP_SESSION_STALL;
	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(&linkB);
		result = NativeLockstepSession_TakeFrameInputs(session, 0u, &inputs);
		if (result != NATIVE_LOCKSTEP_SESSION_STALL)
		{
			break;
		}
	}
	CHECK(result == NATIVE_LOCKSTEP_SESSION_OK);

	NativeUdpTransport_Close(&bystander);
	NativeUdpTransport_GlobalShutdown();
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * Aux route (MS-5): opaque NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES datagrams for
 * higher layers. Every payload below is a deterministic pattern of a
 * (side, index) tag so payloads are pairwise distinct and a reordering,
 * truncation, or mix-up between entries is caught byte for byte.
 */
static void MakeAuxPayload(uint8_t *out, uint8_t side, uint32_t index)
{
	for (uint32_t i = 0; i < NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES; i++)
	{
		out[i] = (uint8_t)((side * 0x53u) ^ (index * 0x1Du) ^ (i * 0x07u) ^ 0xA5u);
	}
	out[0] = side;
	out[1] = (uint8_t)index;
}

/* Poll-only spin until at least expectedTotal aux datagrams have been either
 * stored or discarded as overflow (AuxCount + DroppedAuxCount). */
static void PumpPollUntilAuxTotal(struct NativeLockstepPeerLink *link, uint32_t expectedTotal)
{
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		if ((NativeLockstepPeerLink_AuxCount(link) + NativeLockstepPeerLink_DroppedAuxCount(link)) >= expectedTotal)
		{
			break;
		}
	}
}

/* Pops one aux entry and checks it is byte-identical to the (side, index)
 * payload. */
static int ExpectTakeAux(struct NativeLockstepPeerLink *link, uint8_t side, uint32_t index)
{
	uint8_t expected[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t actual[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0;

	MakeAuxPayload(expected, side, index);
	memset(actual, 0, sizeof(actual));
	CHECK(NativeLockstepPeerLink_TakeAux(link, actual, sizeof(actual), &size) == 1);
	CHECK(size == NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES);
	CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
	return 0;
}

/* Opens A then B on the given ports and pumps both to RUNNING with the
 * normal Retransmit-before-Poll cadence. */
static int OpenRunningPair(struct NativeLockstepPeerLink *linkA, struct NativeLockstepPeerLink *linkB, uint16_t portA, uint16_t portB,
	struct NativeUdpTransportAddress *addrA, struct NativeUdpTransportAddress *addrB)
{
	struct NativeMatchConfigV1 config;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(addrA, "127.0.0.1", portA));
	CHECK(NativeUdpTransport_MakeAddress(addrB, "127.0.0.1", portB));
	CHECK(NativeLockstepPeerLink_Open(linkA, portA, addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Open(linkB, portB, addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(PumpUntilMode(linkA, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(PumpUntilMode(linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_AuxCount(linkA) == 0u);
	CHECK(NativeLockstepPeerLink_AuxCount(linkB) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(linkA) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(linkB) == 0u);
	return 0;
}

/* Two RUNNING links exchange three distinct aux payloads each way; each
 * receiver takes them back byte-identical and in send order. */
static int TestAuxRoundTrip(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t out[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0;
	uint32_t i;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_ROUNDTRIP_PORT_A, (uint16_t)AUX_ROUNDTRIP_PORT_B, &addrA, &addrB) == 0);

	for (i = 0; i < 3u; i++)
	{
		MakeAuxPayload(payload, 0xAu, i);
		CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	}
	PumpPollUntilAuxTotal(&linkB, 3u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 3u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);
	for (i = 0; i < 3u; i++)
	{
		CHECK(ExpectTakeAux(&linkB, 0xAu, i) == 0);
	}
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), &size) == 0);

	for (i = 0; i < 3u; i++)
	{
		MakeAuxPayload(payload, 0xBu, i);
		CHECK(NativeLockstepPeerLink_SendAux(&linkB, payload, sizeof(payload)) == 1);
	}
	PumpPollUntilAuxTotal(&linkA, 3u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkA) == 3u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkA) == 0u);
	for (i = 0; i < 3u; i++)
	{
		CHECK(ExpectTakeAux(&linkA, 0xBu, i) == 0);
	}
	CHECK(NativeLockstepPeerLink_TakeAux(&linkA, out, sizeof(out), &size) == 0);

	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * An aux datagram reaching a side that is still HANDSHAKING is dropped (and
 * not counted). Uses TestEarlyBundleArrival's held-back-HELLO construction
 * to keep B genuinely HANDSHAKING while A is RUNNING; a bundle sent right
 * after the aux datagram, on the same socket pair, is the marker that B's
 * Poll has already consumed the aux datagram (it is staged as an early
 * bundle, which is observable).
 */
static int TestAuxDroppedWhileHandshaking(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeUdpTransportAddress sender;
	uint8_t savedHello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	size_t savedHelloSize = 0;
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(&addrA, "127.0.0.1", (uint16_t)AUX_HANDSHAKING_PORT_A));
	CHECK(NativeUdpTransport_MakeAddress(&addrB, "127.0.0.1", (uint16_t)AUX_HANDSHAKING_PORT_B));

	/* A first (its Open()-time HELLO to B is lost), then B. */
	CHECK(NativeLockstepPeerLink_Open(&linkA, (uint16_t)AUX_HANDSHAKING_PORT_A, &addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)AUX_HANDSHAKING_PORT_B, &addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));

	/* A HANDSHAKING link refuses to send aux. */
	MakeAuxPayload(payload, 0xBu, 0u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkB, payload, sizeof(payload)) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);

	/* The one HELLO B will ever get from A, held back off B's socket. */
	NativeLockstepPeerLink_Retransmit(&linkA);
	memset(&sender, 0, sizeof(sender));
	CHECK(PumpRawReceive(&linkB.transport, savedHello, sizeof(savedHello), &savedHelloSize, &sender) == NATIVE_UDP_TRANSPORT_RECEIVE_OK);
	CHECK(savedHelloSize == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES);

	CHECK(PumpPollUntilMode(&linkA, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	MakeAuxPayload(payload, 0xAu, 0u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u));

	PumpPollUntilEarlyCount(&linkB, 1u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(linkB.earlyBundleCount == 1u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);

	/* Let B complete: the dropped aux datagram does not reappear. */
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);

	/* Once RUNNING, B's aux route is live. */
	MakeAuxPayload(payload, 0xAu, 1u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	PumpPollUntilAuxTotal(&linkB, 1u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 1u);
	CHECK(ExpectTakeAux(&linkB, 0xAu, 1u) == 0);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

#define AUX_INTERLEAVE_FRAME_COUNT 6u

static void BuildRolePad(struct NativeCanonicalInputPadV1 *pad, uint8_t role, uint32_t frame)
{
	memset(pad, 0, sizeof(*pad));
	pad->status = (uint8_t)(0x40u + role);
	pad->id = role;
	pad->buttons[0] = (uint8_t)(0xA5u ^ frame);
	pad->analog[0] = 0x80u;
	pad->connected = 1u;
}

/*
 * Aux datagrams interleaved with real lockstep traffic: both sides run
 * AUX_INTERLEAVE_FRAME_COUNT frames of SubmitLocalInput/ComposeAndSendBundle/
 * Poll/TakeFrameInputs/RecordLocalDigests (the process test's loop, both
 * sides driven from this one process), sending one aux datagram just before
 * and one just after each frame's bundle. Both sides must stay RUNNING with
 * no fault or divergence, and every aux payload must come out intact and in
 * order (2 * AUX_INTERLEAVE_FRAME_COUNT per side, under the inbox capacity).
 */
static int TestAuxInterleavedWithBundles(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeLockstepPeerLink *links[2] = {&linkA, &linkB};
	const uint8_t roles[2] = {(uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN};
	const uint8_t sides[2] = {0xAu, 0xBu};
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint32_t frame;
	uint32_t side;
	uint32_t i;

	_Static_assert(2u * AUX_INTERLEAVE_FRAME_COUNT <= NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY,
		"interleave test must not overflow the aux inbox");

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_INTERLEAVE_PORT_A, (uint16_t)AUX_INTERLEAVE_PORT_B, &addrA, &addrB) == 0);

	for (frame = 0; frame < AUX_INTERLEAVE_FRAME_COUNT; frame++)
	{
		int taken[2] = {0, 0};
		uint32_t attempt;

		for (side = 0; side < 2u; side++)
		{
			struct NativeCanonicalInputPadV1 pad;
			struct NativeLockstepSession *session = NativeLockstepPeerLink_Session(links[side]);

			CHECK(session != NULL);
			BuildRolePad(&pad, roles[side], frame);
			CHECK(NativeLockstepSession_SubmitLocalInput(session, frame, &pad));
			MakeAuxPayload(payload, sides[side], 2u * frame);
			CHECK(NativeLockstepPeerLink_SendAux(links[side], payload, sizeof(payload)) == 1);
			CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(links[side], frame));
			MakeAuxPayload(payload, sides[side], (2u * frame) + 1u);
			CHECK(NativeLockstepPeerLink_SendAux(links[side], payload, sizeof(payload)) == 1);
		}

		for (attempt = 0; (attempt < SPIN_BUDGET) && !(taken[0] && taken[1]); attempt++)
		{
			for (side = 0; side < 2u; side++)
			{
				struct NativeLockstepSessionFrameInputs inputs;
				enum NativeLockstepSessionResult result;

				NativeLockstepPeerLink_Poll(links[side]);
				CHECK(NativeLockstepPeerLink_Mode(links[side]) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
				if (taken[side])
				{
					continue;
				}
				result = NativeLockstepSession_TakeFrameInputs(NativeLockstepPeerLink_Session(links[side]), frame, &inputs);
				if (result == NATIVE_LOCKSTEP_SESSION_OK)
				{
					taken[side] = 1;
				}
				else
				{
					CHECK(result == NATIVE_LOCKSTEP_SESSION_STALL);
				}
			}
		}
		CHECK(taken[0] && taken[1]);

		for (side = 0; side < 2u; side++)
		{
			struct NativeCanonicalStateV4 state;

			CHECK(NativeLockstepPeerLinkFixture_MakeState(&state, frame));
			CHECK(NativeLockstepSession_RecordLocalDigests(NativeLockstepPeerLink_Session(links[side]), &state));
		}
	}

	for (side = 0; side < 2u; side++)
	{
		struct NativeLockstepPeerLink *receiver = links[1u - side];
		struct NativeLockstepSession *session = NativeLockstepPeerLink_Session(receiver);

		PumpPollUntilAuxTotal(receiver, 2u * AUX_INTERLEAVE_FRAME_COUNT);
		CHECK(NativeLockstepPeerLink_Mode(receiver) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
		CHECK(NativeLockstepSession_FirstFault(session) == NULL);
		CHECK(NativeLockstepSession_FirstDivergence(session) == NULL);
		CHECK(NativeLockstepPeerLink_AuxCount(receiver) == 2u * AUX_INTERLEAVE_FRAME_COUNT);
		CHECK(NativeLockstepPeerLink_DroppedAuxCount(receiver) == 0u);
		for (i = 0; i < 2u * AUX_INTERLEAVE_FRAME_COUNT; i++)
		{
			CHECK(ExpectTakeAux(receiver, sides[side], i) == 0);
		}
		CHECK(NativeLockstepPeerLink_AuxCount(receiver) == 0u);
	}

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

#define AUX_OVERFLOW_SEND_COUNT 20u

/*
 * Overflow: A sends AUX_OVERFLOW_SEND_COUNT aux datagrams before B polls at
 * all; B drains them over several Poll calls (the per-call budget is
 * NATIVE_LOCKSTEP_PEER_LINK_POLL_BUDGET). The inbox keeps the newest
 * NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY in order and counts the rest.
 */
static int TestAuxOverflowKeepsNewest(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t out[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0;
	const uint32_t dropped = AUX_OVERFLOW_SEND_COUNT - NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY;
	uint32_t i;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_OVERFLOW_PORT_A, (uint16_t)AUX_OVERFLOW_PORT_B, &addrA, &addrB) == 0);

	for (i = 0; i < AUX_OVERFLOW_SEND_COUNT; i++)
	{
		MakeAuxPayload(payload, 0xAu, i);
		CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	}

	PumpPollUntilAuxTotal(&linkB, AUX_OVERFLOW_SEND_COUNT);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == dropped);
	CHECK(dropped == 4u);

	for (i = dropped; i < AUX_OVERFLOW_SEND_COUNT; i++)
	{
		CHECK(ExpectTakeAux(&linkB, 0xAu, i) == 0);
	}
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), &size) == 0);
	/* Taking does not reset the drop counter. */
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == dropped);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/* SendAux and TakeAux argument, size, and mode refusals. */
static int TestAuxRefusals(void)
{
	struct NativeLockstepPeerLink idle = {0};
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t big[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES + 1u];
	uint8_t out[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0;

	/* NULL and never-opened links. */
	CHECK(NativeLockstepPeerLink_AuxCount(NULL) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(NULL) == 0u);
	MakeAuxPayload(big, 0xAu, 0u);
	CHECK(NativeLockstepPeerLink_SendAux(NULL, big, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES) == 0);
	CHECK(NativeLockstepPeerLink_SendAux(&idle, big, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES) == 0);
	CHECK(NativeLockstepPeerLink_TakeAux(NULL, out, sizeof(out), &size) == 0);
	CHECK(NativeLockstepPeerLink_TakeAux(&idle, out, sizeof(out), &size) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&idle) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_REFUSAL_PORT_A, (uint16_t)AUX_REFUSAL_PORT_B, &addrA, &addrB) == 0);

	/* Wrong sizes and NULL bytes are refused and change nothing. */
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, big, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES - 1u) == 0);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, big, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES + 1u) == 0);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, big, 0u) == 0);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, NULL, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* One good send, so TakeAux refusals can be shown to pop nothing. */
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, big, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES) == 1);
	PumpPollUntilAuxTotal(&linkB, 1u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 1u);

	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, NULL, sizeof(out), &size) == 0);
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), NULL) == 0);
	size = 12345u;
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES - 1u, &size) == 0);
	CHECK(size == 12345u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 1u);
	CHECK(ExpectTakeAux(&linkB, 0xAu, 0u) == 0);
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), &size) == 0);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	/* A closed (IDLE again) link refuses to send. */
	NativeLockstepPeerLink_Close(&linkA);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, big, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);

	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * Close empties the inbox and zeroes the drop counter (idempotently, and
 * safely on a zero-initialized struct), and a fresh Open on the same struct
 * starts empty; the reopened pair's aux route works normally.
 */
static int TestAuxResetOnCloseAndReopen(void)
{
	struct NativeLockstepPeerLink zeroed = {0};
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t out[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0;
	uint32_t i;

	NativeLockstepPeerLink_Close(&zeroed);
	NativeLockstepPeerLink_Close(&zeroed);
	CHECK(NativeLockstepPeerLink_AuxCount(&zeroed) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&zeroed) == 0u);
	CHECK(NativeLockstepPeerLink_Mode(&zeroed) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_RESET_PORT_A, (uint16_t)AUX_RESET_PORT_B, &addrA, &addrB) == 0);

	/* Fill B's inbox past capacity so both the ring and the counter are
	 * non-zero before Close. */
	for (i = 0; i < NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY + 1u; i++)
	{
		MakeAuxPayload(payload, 0xAu, i);
		CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	}
	PumpPollUntilAuxTotal(&linkB, NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY + 1u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 1u);

	NativeLockstepPeerLink_Close(&linkB);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), &size) == 0);
	NativeLockstepPeerLink_Close(&linkB);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	NativeLockstepPeerLink_Close(&linkA);

	/* Reopen both on the same structs and ports: empty from the start, and
	 * still empty once RUNNING again. */
	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_RESET_PORT_A, (uint16_t)AUX_RESET_PORT_B, &addrA, &addrB) == 0);
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), &size) == 0);

	MakeAuxPayload(payload, 0xAu, 99u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	PumpPollUntilAuxTotal(&linkB, 1u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 1u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);
	CHECK(ExpectTakeAux(&linkB, 0xAu, 99u) == 0);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * 63- and 65-byte datagrams from the peer are neither aux datagrams nor
 * wire records: dropped, inbox untouched, modes unchanged. A trailing
 * genuine aux datagram on the same socket pair proves the odd ones were
 * already consumed (and not stored) by the time it arrives.
 */
static int TestAuxOddSizesDropped(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t odd[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES + 1u];
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_ODD_SIZE_PORT_A, (uint16_t)AUX_ODD_SIZE_PORT_B, &addrA, &addrB) == 0);

	memset(odd, 0x5A, sizeof(odd));
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, odd, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES - 1u));
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, odd, NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES + 1u));
	MakeAuxPayload(payload, 0xAu, 7u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);

	PumpPollUntilAuxTotal(&linkB, 1u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 1u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);
	CHECK(ExpectTakeAux(&linkB, 0xAu, 7u) == 0);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * An aux-sized datagram from a sender other than the configured peer is
 * discarded by the existing sender filter (TestSenderAddressFiltering's
 * bystander construction), while the configured peer's own aux datagram is
 * accepted normally.
 */
static int TestAuxForeignSenderDiscarded(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransport bystander = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint32_t attempt;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_FOREIGN_PORT_A, (uint16_t)AUX_FOREIGN_PORT_B, &addrA, &addrB) == 0);

	CHECK(NativeUdpTransport_GlobalInit());
	CHECK(NativeUdpTransport_Open(&bystander, (uint16_t)AUX_FOREIGN_PORT_BYSTANDER));

	MakeAuxPayload(payload, 0xCu, 0u);
	CHECK(NativeUdpTransport_Send(&bystander, &addrB, payload, sizeof(payload)));
	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(&linkB);
	}
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);

	MakeAuxPayload(payload, 0xAu, 0u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	PumpPollUntilAuxTotal(&linkB, 1u);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 1u);
	CHECK(ExpectTakeAux(&linkB, 0xAu, 0u) == 0);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);

	NativeUdpTransport_Close(&bystander);
	NativeUdpTransport_GlobalShutdown();
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/* Fills a link's aux inbox fields with a recognizable junk pattern. */
static void FillAuxJunk(struct NativeLockstepPeerLink *link)
{
	memset(link->auxBytes, 0xEE, sizeof(link->auxBytes));
	link->auxHead = 5u;
	link->auxCount = 7u;
	link->droppedAuxCount = 9u;
}

/* The aux inbox fields still hold exactly FillAuxJunk's pattern. */
static int ExpectAuxJunk(const struct NativeLockstepPeerLink *link)
{
	uint8_t junk[NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY][NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];

	memset(junk, 0xEE, sizeof(junk));
	CHECK(memcmp(link->auxBytes, junk, sizeof(junk)) == 0);
	CHECK(link->auxHead == 5u);
	CHECK(link->auxCount == 7u);
	CHECK(link->droppedAuxCount == 9u);
	return 0;
}

/*
 * A successful Open resets the aux inbox (whatever the struct held before,
 * even on a closed struct), and a failed Open leaves the aux fields exactly
 * as they were: NULL peer (before anything opens), a local port already in
 * use (the transport open fails), and a bad role (the handshake Begin fails
 * after the transport opened).
 */
static int TestAuxOpenResetsInbox(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink link = {0};
	struct NativeLockstepPeerLink holder = {0};
	struct NativeUdpTransportAddress peer;
	uint8_t out[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t size = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	/* Nothing listens on the peer port; the Open()-time HELLO is simply lost. */
	CHECK(NativeUdpTransport_MakeAddress(&peer, "127.0.0.1", (uint16_t)AUX_OPEN_RESET_PEER_PORT));

	/* (a) Junk on a closed struct, then a successful Open: empty inbox, 0 dropped. */
	NativeLockstepPeerLink_Close(&link);
	FillAuxJunk(&link);
	CHECK(NativeLockstepPeerLink_AuxCount(&link) == 7u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&link) == 9u);
	CHECK(NativeLockstepPeerLink_Open(&link, (uint16_t)AUX_OPEN_RESET_PORT, &peer, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&link) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_AuxCount(&link) == 0u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&link) == 0u);
	CHECK(NativeLockstepPeerLink_TakeAux(&link, out, sizeof(out), &size) == 0);
	NativeLockstepPeerLink_Close(&link);

	/* (b) Failed Opens leave the junk untouched. */
	memset(&link, 0, sizeof(link));
	FillAuxJunk(&link);
	CHECK(!NativeLockstepPeerLink_Open(&link, (uint16_t)AUX_OPEN_FAIL_PORT, NULL, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&link) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);
	CHECK(ExpectAuxJunk(&link) == 0);

	CHECK(NativeLockstepPeerLink_Open(&holder, (uint16_t)AUX_OPEN_FAIL_PORT, &peer, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(!NativeLockstepPeerLink_Open(&link, (uint16_t)AUX_OPEN_FAIL_PORT, &peer, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&link) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);
	CHECK(ExpectAuxJunk(&link) == 0);
	NativeLockstepPeerLink_Close(&holder);

	CHECK(!NativeLockstepPeerLink_Open(&link, (uint16_t)AUX_OPEN_FAIL_PORT, &peer, &config, 0xFFu,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&link) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);
	CHECK(ExpectAuxJunk(&link) == 0);
	return 0;
}

/*
 * Aux entries stored while RUNNING survive a terminal transition: B stores
 * three aux datagrams, then a corrupted bundle from A (A's real frame-0
 * bundle with its first byte flipped, which fails decode) latches B FAULTED.
 * A fourth aux datagram queued behind that bundle is never stored (a
 * terminal link no longer receives). TakeAux still returns the three stored
 * entries in order, and SendAux on the terminal link returns 0.
 */
static int TestAuxKeptAfterTerminal(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t payload[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	uint8_t bundle[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t out[NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES];
	size_t bundleSize = 0;
	size_t size = 0;
	uint32_t attempt;
	uint32_t i;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)AUX_TERMINAL_PORT_A, (uint16_t)AUX_TERMINAL_PORT_B, &addrA, &addrB) == 0);

	for (i = 0; i < 3u; i++)
	{
		MakeAuxPayload(payload, 0xAu, i);
		CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);
	}
	PumpPollUntilAuxTotal(&linkB, 3u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 3u);

	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 0u, bundle, sizeof(bundle), &bundleSize));
	CHECK(bundleSize == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	bundle[0] ^= 0xFFu;
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, bundleSize));
	MakeAuxPayload(payload, 0xAu, 3u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkA, payload, sizeof(payload)) == 1);

	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_FAULTED) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	CHECK(NativeLockstepSession_FirstFault(NativeLockstepPeerLink_Session(&linkB)) != NULL);
	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(&linkB);
	}
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 3u);
	CHECK(NativeLockstepPeerLink_DroppedAuxCount(&linkB) == 0u);

	MakeAuxPayload(payload, 0xBu, 0u);
	CHECK(NativeLockstepPeerLink_SendAux(&linkB, payload, sizeof(payload)) == 0);

	for (i = 0; i < 3u; i++)
	{
		CHECK(ExpectTakeAux(&linkB, 0xAu, i) == 0);
	}
	CHECK(NativeLockstepPeerLink_TakeAux(&linkB, out, sizeof(out), &size) == 0);
	CHECK(NativeLockstepPeerLink_AuxCount(&linkB) == 0u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/* ---- LR-14 (docs/LOCKSTEP_RACE_MILESTONE.md, LR-S6): foreign-identity
 * records ---- */

/* The fixture config of another match: the same config with another master
 * seed, so its digest, and with it the match identity every one of its
 * bundles carries, differs from the fixture's. */
static void BuildForeignConfig(struct NativeMatchConfigV1 *config)
{
	NativeLockstepPeerLinkFixture_BuildConfig(config);
	config->masterSeed ^= UINT64_C(0x5A5A5A5A5A5A5A5A);
}

/* A cleanly encoded bundle for frame (at most inputDelay, so it carries no
 * digest) from a scratch session opened on config with inputDelay and the
 * given role's slot: its own digest is valid, whatever its identity. */
static int ComposeScratchBundle(const struct NativeMatchConfigV1 *config, uint32_t inputDelay, uint8_t role, uint32_t frame,
	uint8_t *bytes)
{
	static struct NativeLockstepSession scratch;
	uint8_t slot = 0u;
	size_t size = 0;

	CHECK(frame <= inputDelay);
	CHECK(NativeMatchConfigV1_FindRoleSlot(config, role, &slot));
	NativeLockstepSession_Init(&scratch);
	CHECK(NativeLockstepSession_Open(&scratch, config, inputDelay, slot));
	CHECK(NativeLockstepSession_ComposeBundle(&scratch, frame, bytes, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES, &size));
	CHECK(size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	return 0;
}

/* A foreign-identity CAB1 bundle for frame, D = NATIVE_LOCKSTEP_MIN_INPUT_DELAY. */
static int ComposeForeignBundle(uint32_t frame, uint8_t *bytes)
{
	struct NativeMatchConfigV1 foreign;

	BuildForeignConfig(&foreign);
	return ComposeScratchBundle(&foreign, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		frame, bytes);
}

/* Poll-only spin that stops once link has dropped at least expected
 * foreign-identity records or left RUNNING. */
static void PumpPollUntilForeignCount(struct NativeLockstepPeerLink *link, uint32_t expected)
{
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		if ((NativeLockstepPeerLink_DroppedForeignBundleCount(link) >= expected) ||
			(NativeLockstepPeerLink_Mode(link) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING))
		{
			break;
		}
	}
}

/* Poll-only spin until link's session takes frame (or a bounded give-up). */
static enum NativeLockstepSessionResult PumpPollUntilTaken(struct NativeLockstepPeerLink *link, uint32_t frame)
{
	struct NativeLockstepSessionFrameInputs inputs;
	enum NativeLockstepSessionResult result = NATIVE_LOCKSTEP_SESSION_STALL;
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		NativeLockstepPeerLink_Poll(link);
		result = NativeLockstepSession_TakeFrameInputs(NativeLockstepPeerLink_Session(link), frame, &inputs);
		if (result != NATIVE_LOCKSTEP_SESSION_STALL)
		{
			break;
		}
	}
	return result;
}

/* TestEarlyBundleArrival's construction as a helper: A RUNNING, B still
 * HANDSHAKING, and the one HELLO B will ever get from A held back in
 * savedHello until the caller sends it. */
static int OpenHeldPair(struct NativeLockstepPeerLink *linkA, struct NativeLockstepPeerLink *linkB, uint16_t portA, uint16_t portB,
	struct NativeUdpTransportAddress *addrA, struct NativeUdpTransportAddress *addrB, uint8_t *savedHello, size_t *savedHelloSize)
{
	struct NativeMatchConfigV1 config;
	struct NativeUdpTransportAddress sender;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeUdpTransport_MakeAddress(addrA, "127.0.0.1", portA));
	CHECK(NativeUdpTransport_MakeAddress(addrB, "127.0.0.1", portB));
	CHECK(NativeLockstepPeerLink_Open(linkA, portA, addrB, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Open(linkB, portB, addrA, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN,
		(uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	NativeLockstepPeerLink_Retransmit(linkA);
	memset(&sender, 0, sizeof(sender));
	CHECK(PumpRawReceive(&linkB->transport, savedHello, NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES, savedHelloSize, &sender) ==
		NATIVE_UDP_TRANSPORT_RECEIVE_OK);
	CHECK(*savedHelloSize == NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES);
	CHECK(PumpPollUntilMode(linkA, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(linkB) == 0u);
	return 0;
}

/* B's session latched exactly one fault, with this cause, and dropped no
 * foreign record. */
static int ExpectFaultNotDropped(struct NativeLockstepPeerLink *link, uint32_t cause)
{
	const struct NativeLockstepFaultReport *fault;

	CHECK(PumpPollUntilMode(link, NATIVE_LOCKSTEP_PEER_LINK_FAULTED) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	fault = NativeLockstepSession_FirstFault(NativeLockstepPeerLink_Session(link));
	CHECK(fault != NULL);
	CHECK(fault->cause == cause);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(link) == 0u);
	return 0;
}

/*
 * Foreign records staged while HANDSHAKING are dropped on replay. B is held
 * HANDSHAKING while A's link sends, in this order, a foreign bundle, A's own
 * frame 0, another foreign bundle, and A's own frame 1. All four are staged
 * unscreened (the count stays 0: before the session opens there is no
 * identity to screen against). When the held HELLO completes B's handshake,
 * the replay drops and counts the two foreign records and hands A's two
 * frames to the session, which takes both: a drop does not stop the replay.
 */
static int TestForeignIdentityDroppedWhileStaging(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t savedHello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t foreign[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t savedHelloSize = 0;
	struct NativeLockstepSession *session;
	struct NativeLockstepSessionFrameInputs inputs;

	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(NULL) == 0u);
	CHECK(OpenHeldPair(&linkA, &linkB, (uint16_t)FOREIGN_STAGING_PORT_A, (uint16_t)FOREIGN_STAGING_PORT_B, &addrA, &addrB,
		savedHello, &savedHelloSize) == 0);

	CHECK(ComposeForeignBundle(0u, foreign) == 0);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, foreign, sizeof(foreign)));
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u));
	CHECK(ComposeForeignBundle(1u, foreign) == 0);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, foreign, sizeof(foreign)));
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 1u));

	PumpPollUntilEarlyCount(&linkB, 4u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(linkB.earlyBundleCount == 4u);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 0u);

	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(linkB.earlyBundleCount == 0u);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 2u);
	CHECK(NativeLockstepPeerLink_DroppedEarlyBundleCount(&linkB) == 0u);

	session = NativeLockstepPeerLink_Session(&linkB);
	CHECK(NativeLockstepSession_FirstFault(session) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(session) == NULL);
	CHECK(NativeLockstepSession_TakeFrameInputs(session, 0u, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepSession_TakeFrameInputs(session, 1u, &inputs) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * Foreign records arriving while RUNNING are dropped and counted, never
 * reach the session, and leave the link RUNNING: B then takes A's own frame
 * 0, sent after them. The counter survives Close and is zeroed by the next
 * successful Open, like the early-bundle drop counter.
 */
static int TestForeignIdentityDroppedWhileRunning(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t foreign[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint32_t frame;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)FOREIGN_RUNNING_PORT_A, (uint16_t)FOREIGN_RUNNING_PORT_B, &addrA, &addrB) == 0);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 0u);

	for (frame = 0; frame <= (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY; frame++)
	{
		CHECK(ComposeForeignBundle(frame, foreign) == 0);
		CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, foreign, sizeof(foreign)));
	}
	/* The same foreign record again: a resent stale bundle counts again. */
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, foreign, sizeof(foreign)));
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u));

	CHECK(PumpPollUntilTaken(&linkB, 0u) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY + 2u);
	CHECK(NativeLockstepSession_FirstFault(NativeLockstepPeerLink_Session(&linkB)) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(NativeLockstepPeerLink_Session(&linkB)) == NULL);
	/* The sender's own counter is untouched. */
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkA) == 0u);

	NativeLockstepPeerLink_Close(&linkB);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY + 2u);
	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)FOREIGN_RUNNING_PORT_B, &addrA, &config,
		(uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 0u);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * The drop path's edge: the decoder checks a record's own digest before its
 * identity, so a corrupt record is never dropped as foreign, it faults
 * BAD_DIGEST. Three records, each against a fresh pair: a foreign-identity
 * record with one corrupted pad byte, sent while RUNNING; a current-identity
 * record (A's own frame 0) whose identity bytes are flipped without
 * recomputing its digest, sent while RUNNING; and the corrupt foreign
 * record again, staged while HANDSHAKING, which faults on replay. A last
 * pair stages [a foreign record, the corrupt foreign record, A's own frame
 * 0] (LR-33: a drop does not stop a replay; a fault still does): the replay
 * drops and counts the first, faults BAD_DIGEST on the second, and stops
 * there, so frame 0 never reaches the session.
 */
static int TestForeignIdentityCorruptStillFaults(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t savedHello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t bundle[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t foreign[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	const struct NativeLockstepFaultReport *fault;
	struct NativeLockstepSession *session;
	size_t savedHelloSize = 0;
	size_t size = 0;
	uint32_t i;

	/* A corrupt foreign record while RUNNING. */
	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)FOREIGN_CORRUPT_PORT_A, (uint16_t)FOREIGN_CORRUPT_PORT_B, &addrA, &addrB) == 0);
	CHECK(ComposeForeignBundle(0u, bundle) == 0);
	bundle[BUNDLE_PAD_BYTE_OFFSET] ^= 0x01u;
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, sizeof(bundle)));
	CHECK(ExpectFaultNotDropped(&linkB, (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);

	/* A current-identity record with its identity flipped, digest stale. */
	memset(&linkA, 0, sizeof(linkA));
	memset(&linkB, 0, sizeof(linkB));
	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)FOREIGN_FLIPPED_PORT_A, (uint16_t)FOREIGN_FLIPPED_PORT_B, &addrA, &addrB) == 0);
	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 0u, bundle, sizeof(bundle), &size));
	CHECK(size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
	CHECK(memcmp(&bundle[BUNDLE_IDENTITY_OFFSET], linkB.session.matchIdentity, BUNDLE_IDENTITY_BYTES) == 0);
	for (i = 0; i < BUNDLE_IDENTITY_BYTES; i++)
	{
		bundle[BUNDLE_IDENTITY_OFFSET + i] ^= 0xFFu;
	}
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, sizeof(bundle)));
	CHECK(ExpectFaultNotDropped(&linkB, (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);

	/* The corrupt foreign record staged while HANDSHAKING: the replay
	 * faults. */
	memset(&linkA, 0, sizeof(linkA));
	memset(&linkB, 0, sizeof(linkB));
	CHECK(OpenHeldPair(&linkA, &linkB, (uint16_t)FOREIGN_CORRUPT_STAGED_PORT_A, (uint16_t)FOREIGN_CORRUPT_STAGED_PORT_B, &addrA,
		&addrB, savedHello, &savedHelloSize) == 0);
	CHECK(ComposeForeignBundle(0u, bundle) == 0);
	bundle[BUNDLE_PAD_BYTE_OFFSET] ^= 0x01u;
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, sizeof(bundle)));
	PumpPollUntilEarlyCount(&linkB, 1u);
	CHECK(linkB.earlyBundleCount == 1u);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(ExpectFaultNotDropped(&linkB, (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST) == 0);
	CHECK(linkB.earlyBundleCount == 0u);
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);

	/* Staged [foreign, corrupt foreign, A's own frame 0]: the replay drops
	 * the first, faults on the second, and stops before the third. */
	memset(&linkA, 0, sizeof(linkA));
	memset(&linkB, 0, sizeof(linkB));
	CHECK(OpenHeldPair(&linkA, &linkB, (uint16_t)FOREIGN_MIXED_STAGED_PORT_A, (uint16_t)FOREIGN_MIXED_STAGED_PORT_B, &addrA,
		&addrB, savedHello, &savedHelloSize) == 0);
	CHECK(ComposeForeignBundle(0u, foreign) == 0);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, foreign, sizeof(foreign)));
	CHECK(ComposeForeignBundle(1u, bundle) == 0);
	bundle[BUNDLE_PAD_BYTE_OFFSET] ^= 0x01u;
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, sizeof(bundle)));
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u));
	PumpPollUntilEarlyCount(&linkB, 3u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(linkB.earlyBundleCount == 3u);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 0u);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_FAULTED) == NATIVE_LOCKSTEP_PEER_LINK_FAULTED);
	CHECK(linkB.earlyBundleCount == 0u);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 1u);
	session = NativeLockstepPeerLink_Session(&linkB);
	fault = NativeLockstepSession_FirstFault(session);
	CHECK(fault != NULL);
	CHECK(fault->cause == (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_DIGEST);
	/* No peer window holds any frame: A's frame 0 never reached the
	 * session. */
	for (i = 0; i < NATIVE_LOCKSTEP_SESSION_PEER_CAPACITY; i++)
	{
		CHECK(session->peers[i].occupancyMask == 0u);
	}
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * Every non-identity fault still latches: a current-identity record (the
 * fixture's identity, validly encoded) with a wrong input delay faults
 * INPUT_DELAY, and one whose sender slot is B's own slot faults BAD_SLOT.
 * Neither is dropped.
 */
static int TestCurrentIdentityBadDelayOrSlotStillFaults(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t bundle[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];

	NativeLockstepPeerLinkFixture_BuildConfig(&config);

	/* A wrong D: identity and digest are right, the delay is D + 1. */
	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)CURRENT_BAD_DELAY_PORT_A, (uint16_t)CURRENT_BAD_DELAY_PORT_B, &addrA, &addrB) ==
		0);
	CHECK(ComposeScratchBundle(&config, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY + 1u, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		0u, bundle) == 0);
	CHECK(memcmp(&bundle[BUNDLE_IDENTITY_OFFSET], linkB.session.matchIdentity, BUNDLE_IDENTITY_BYTES) == 0);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, sizeof(bundle)));
	CHECK(ExpectFaultNotDropped(&linkB, (uint32_t)NATIVE_LOCKSTEP_FAULT_INPUT_DELAY) == 0);
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);

	/* A wrong sender slot: B's own (CAB2) slot, which is not a peer of B. */
	memset(&linkA, 0, sizeof(linkA));
	memset(&linkB, 0, sizeof(linkB));
	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)CURRENT_BAD_SLOT_PORT_A, (uint16_t)CURRENT_BAD_SLOT_PORT_B, &addrA, &addrB) == 0);
	CHECK(ComposeScratchBundle(&config, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, 0u,
		bundle) == 0);
	CHECK(memcmp(&bundle[BUNDLE_IDENTITY_OFFSET], linkB.session.matchIdentity, BUNDLE_IDENTITY_BYTES) == 0);
	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, bundle, sizeof(bundle)));
	CHECK(ExpectFaultNotDropped(&linkB, (uint32_t)NATIVE_LOCKSTEP_FAULT_BAD_SLOT) == 0);
	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/* ---- LR-3 / LR-S9 (docs/LOCKSTEP_RACE_MILESTONE.md): the verbatim bundle
 * send ---- */

/* A width none of the link's routes uses, so a receiving link would drop it
 * and a raw receiver can tell it apart. */
#define MARKER_BYTES 7u

/* Sends a marker datagram from sender to receiverAddress, then takes every
 * datagram waiting on receiver's socket up to and including the marker.
 * Returns the number of bundle-width datagrams taken before it (handshake
 * leftovers are skipped), or UINT32_MAX when the marker never arrives. The
 * marker is sent after anything the test is checking, on the same socket
 * pair, so everything sent before it has been taken once it is. */
static uint32_t CountBundlesBeforeMarker(struct NativeUdpTransport *receiver, struct NativeUdpTransport *sender,
	const struct NativeUdpTransportAddress *receiverAddress)
{
	static const uint8_t marker[MARKER_BYTES] = {0x4Du, 0x41u, 0x52u, 0x4Bu, 0x45u, 0x52u, 0x21u};
	uint8_t bytes[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint32_t bundles = 0u;
	uint32_t attempt;

	if (!NativeUdpTransport_Send(sender, receiverAddress, marker, sizeof(marker)))
	{
		return UINT32_MAX;
	}
	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		struct NativeUdpTransportAddress from;
		size_t size = 0;

		if (PumpRawReceive(receiver, bytes, sizeof(bytes), &size, &from) != NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			continue;
		}
		if ((size == MARKER_BYTES) && (memcmp(bytes, marker, sizeof(marker)) == 0))
		{
			return bundles;
		}
		if (size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
		{
			bundles++;
		}
	}
	return UINT32_MAX;
}

/* Takes datagrams off receiver's socket until a bundle-width one arrives and
 * copies it to out (handshake leftovers are skipped); checks its sender. */
static int ReceiveBundleFrom(struct NativeUdpTransport *receiver, const struct NativeUdpTransportAddress *expectedSender, uint8_t *out)
{
	uint8_t bytes[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint32_t attempt;

	for (attempt = 0; attempt < SPIN_BUDGET; attempt++)
	{
		struct NativeUdpTransportAddress from;
		size_t size = 0;

		memset(&from, 0, sizeof(from));
		if (PumpRawReceive(receiver, bytes, sizeof(bytes), &size, &from) != NATIVE_UDP_TRANSPORT_RECEIVE_OK)
		{
			continue;
		}
		if (size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES)
		{
			CHECK((from.ipv4 == expectedSender->ipv4) && (from.port == expectedSender->port));
			memcpy(out, bytes, NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);
			return 0;
		}
	}
	return 1;
}

/* The fixture's synthetic state for frame, with one world counter perturbed
 * when drift is nonzero, so its digests differ from the fixture's. */
static int MakeDriftState(struct NativeCanonicalStateV4 *state, uint32_t frame, uint32_t drift)
{
	NativeCanonicalStateV4_Init(state);
	state->frameNumber = frame;
	state->control.frameCounter = (int32_t)frame;
	if (drift != 0u)
	{
		state->worldCounters.flags = NATIVE_CANONICAL_WORLD_COUNTERS_V1_FLAG_AVAILABLE;
		state->worldCounters.activeBombMissileCount = drift;
	}
	return NativeCanonicalStateV4_ComputeDigests(state) == 1;
}

/*
 * A RUNNING pair: the bytes A's session composed for frame 0, sent through
 * the verbatim send, reach B unchanged, from A's address, and are exactly
 * what ComposeAndSendBundle puts on the wire for that frame. B's session
 * takes them (OK); the same bytes resent are a DUPLICATE, both handed to the
 * session directly and drained by B's own Poll, with no fault: a resend of an
 * already accepted bundle is harmless.
 */
static int TestVerbatimSendDelivers(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t kept[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t received[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	struct NativeLockstepSession *sessionB;
	const struct NativeLockstepInputWindow *windowA;
	size_t size = 0;
	uint32_t attempt;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)VERBATIM_SEND_PORT_A, (uint16_t)VERBATIM_SEND_PORT_B, &addrA, &addrB) == 0);
	sessionB = NativeLockstepPeerLink_Session(&linkB);
	windowA = &sessionB->peers[linkA.session.localSlot];
	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 0u, kept, sizeof(kept), &size));
	CHECK(size == NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES);

	/* Verbatim: the exact bytes, from A. */
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, sizeof(kept)) == 1);
	memset(received, 0, sizeof(received));
	CHECK(ReceiveBundleFrom(&linkB.transport, &addrA, received) == 0);
	CHECK(memcmp(received, kept, sizeof(kept)) == 0);

	/* The compose path puts the same bytes on the wire. */
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 0u) == 1);
	memset(received, 0, sizeof(received));
	CHECK(ReceiveBundleFrom(&linkB.transport, &addrA, received) == 0);
	CHECK(memcmp(received, kept, sizeof(kept)) == 0);

	/* B's session takes them; the resend is a duplicate, not a fault. */
	CHECK(NativeLockstepSession_AcceptBundle(sessionB, received, sizeof(received)) == NATIVE_LOCKSTEP_SESSION_OK);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, sizeof(kept)) == 1);
	memset(received, 0, sizeof(received));
	CHECK(ReceiveBundleFrom(&linkB.transport, &addrA, received) == 0);
	CHECK(memcmp(received, kept, sizeof(kept)) == 0);
	CHECK(NativeLockstepSession_AcceptBundle(sessionB, received, sizeof(received)) == NATIVE_LOCKSTEP_SESSION_DUPLICATE);
	CHECK(windowA->duplicateAcceptCount == 1u);

	/* Once more, drained by B's own Poll. */
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, sizeof(kept)) == 1);
	for (attempt = 0; (attempt < SPIN_BUDGET) && (windowA->duplicateAcceptCount < 2u); attempt++)
	{
		NativeLockstepPeerLink_Poll(&linkB);
	}
	CHECK(windowA->duplicateAcceptCount == 2u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepSession_FirstFault(sessionB) == NULL);
	CHECK(NativeLockstepSession_FirstDivergence(sessionB) == NULL);
	CHECK(NativeLockstepPeerLink_DroppedForeignBundleCount(&linkB) == 0u);
	CHECK(PumpPollUntilTaken(&linkB, 0u) == NATIVE_LOCKSTEP_SESSION_OK);
	/* The sender's own link and session are unchanged by sending. */
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepSession_Mode(&linkA.session) == NATIVE_LOCKSTEP_RUNNING);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * Every refusal returns 0 and sends nothing: a NULL or never-opened link,
 * NULL bytes, a wrong size, a record of another match (the foreign config's
 * CAB1 bundle, which is A's own role), a record whose sender is not A's slot
 * (B's own bundle, from B's session and from a scratch session), a
 * current-identity record with the wrong input delay, and a corrupt record.
 * The marker then shows nothing reached B; the positive control after it
 * shows the same path does deliver. A closed link refuses too. Both links
 * stay RUNNING and neither session latches anything.
 */
static int TestVerbatimSendRefusals(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink idle = {0};
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t kept[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t other[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t big[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES + 1u];
	size_t size = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(ComposeScratchBundle(&config, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN, 0u,
		kept) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(NULL, kept, sizeof(kept)) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&idle, kept, sizeof(kept)) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&idle) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)VERBATIM_REFUSE_PORT_A, (uint16_t)VERBATIM_REFUSE_PORT_B, &addrA, &addrB) == 0);
	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 0u, kept, sizeof(kept), &size));
	memcpy(big, kept, sizeof(kept));
	big[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES] = 0u;

	/* Arguments and sizes. */
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, NULL, sizeof(kept)) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, 0u) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, sizeof(kept) - 1u) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, big, sizeof(big)) == 0);

	/* Another match's record, for A's own role and slot. */
	CHECK(ComposeForeignBundle(0u, other) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, other, sizeof(other)) == 0);

	/* The peer's record: B's own bundle, and a scratch CAB2 bundle. */
	CHECK(NativeLockstepSession_ComposeBundle(&linkB.session, 0u, other, sizeof(other), &size));
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, other, sizeof(other)) == 0);
	CHECK(ComposeScratchBundle(&config, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, 0u,
		other) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, other, sizeof(other)) == 0);

	/* The current identity with the wrong input delay. */
	CHECK(ComposeScratchBundle(&config, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY + 1u, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		0u, other) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, other, sizeof(other)) == 0);

	/* A corrupt copy of A's own bundle. */
	memcpy(other, kept, sizeof(kept));
	other[BUNDLE_PAD_BYTE_OFFSET] ^= 0x01u;
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, other, sizeof(other)) == 0);

	/* Nothing reached B; the positive control does. */
	CHECK(CountBundlesBeforeMarker(&linkB.transport, &linkA.transport, &addrB) == 0u);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, sizeof(kept)) == 1);
	CHECK(CountBundlesBeforeMarker(&linkB.transport, &linkA.transport, &addrB) == 1u);

	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepSession_FirstFault(&linkA.session) == NULL);
	CHECK(NativeLockstepSession_FirstFault(&linkB.session) == NULL);

	/* A closed link refuses. */
	NativeLockstepPeerLink_Close(&linkA);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept, sizeof(kept)) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_IDLE);

	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * A HANDSHAKING link refuses: B is held HANDSHAKING (OpenHeldPair) and its
 * own frame-0 bundle, byte for byte what its session will compose, is
 * refused, and nothing reaches A. Once the held HELLO completes B's
 * handshake, the same bytes go out and reach A unchanged. B closed and
 * reopened on the same struct is HANDSHAKING again over its old, still
 * RUNNING session, and link mode alone refuses the bytes.
 */
static int TestVerbatimSendRefusedWhileHandshaking(void)
{
	struct NativeMatchConfigV1 config;
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	uint8_t savedHello[NATIVE_LOCKSTEP_HANDSHAKE_V1_ENCODED_BYTES];
	uint8_t bundle[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t composed[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t received[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	size_t savedHelloSize = 0;
	size_t size = 0;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	CHECK(ComposeScratchBundle(&config, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, 0u,
		bundle) == 0);
	CHECK(OpenHeldPair(&linkA, &linkB, (uint16_t)VERBATIM_HANDSHAKE_PORT_A, (uint16_t)VERBATIM_HANDSHAKE_PORT_B, &addrA, &addrB,
		savedHello, &savedHelloSize) == 0);

	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkB, bundle, sizeof(bundle)) == 0);
	CHECK(CountBundlesBeforeMarker(&linkA.transport, &linkB.transport, &addrA) == 0u);

	CHECK(NativeUdpTransport_Send(&linkA.transport, &addrB, savedHello, savedHelloSize));
	CHECK(PumpPollUntilMode(&linkB, NATIVE_LOCKSTEP_PEER_LINK_RUNNING) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepSession_ComposeBundle(&linkB.session, 0u, composed, sizeof(composed), &size));
	CHECK(memcmp(composed, bundle, sizeof(bundle)) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkB, bundle, sizeof(bundle)) == 1);
	memset(received, 0, sizeof(received));
	CHECK(ReceiveBundleFrom(&linkA.transport, &addrB, received) == 0);
	CHECK(memcmp(received, bundle, sizeof(bundle)) == 0);

	/* Reopened on the same struct, B is HANDSHAKING again while its old
	 * session, which only a completed handshake re-initializes, still reads
	 * RUNNING: link mode alone refuses the old match's bytes. (A's handshake
	 * is complete, so B's new HELLO gets no answer and B stays
	 * HANDSHAKING.) */
	NativeLockstepPeerLink_Close(&linkB);
	CHECK(NativeLockstepPeerLink_Open(&linkB, (uint16_t)VERBATIM_HANDSHAKE_PORT_B, &addrA, &config,
		(uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN, (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY));
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING);
	CHECK(NativeLockstepSession_Mode(&linkB.session) == NATIVE_LOCKSTEP_RUNNING);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkB, bundle, sizeof(bundle)) == 0);
	CHECK(CountBundlesBeforeMarker(&linkA.transport, &linkB.transport, &addrA) == 0u);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

/*
 * LR-3's case: the session is DIVERGED while link mode is still RUNNING.
 * With D = 1, A records frame 0; B records frames 0 and 1 and sends its
 * frame-3 bundle, whose verified digest is frame 1's, and A's Poll parks it
 * (r = 0, 1 <= r + D). A's kept frame-0 bundle still goes out. A then records
 * a drifted frame 1: the parked digest mismatches inside RecordLocalDigests,
 * the session latches DIVERGED, and with no Poll since, link mode is still
 * RUNNING. Now every verbatim send is refused (ComposeAndSendBundle refuses
 * too), and nothing reaches B. A Poll that reads nothing leaves link mode
 * RUNNING (the link mirrors the session mode only when it hands a received
 * bundle to the session); the next bundle from B mirrors DIVERGED into link
 * mode, and the send stays refused.
 */
static int TestVerbatimSendRefusedWhenSessionDiverged(void)
{
	struct NativeLockstepPeerLink linkA = {0};
	struct NativeLockstepPeerLink linkB = {0};
	struct NativeUdpTransportAddress addrA;
	struct NativeUdpTransportAddress addrB;
	struct NativeCanonicalStateV4 state;
	uint8_t kept0[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	uint8_t kept1[NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES];
	const struct NativeLockstepSessionParkedDigest *parked;
	const struct NativeLockstepDivergenceReport *divergence;
	uint8_t slotB;
	size_t size = 0;
	uint32_t attempt;

	CHECK(OpenRunningPair(&linkA, &linkB, (uint16_t)VERBATIM_DIVERGED_PORT_A, (uint16_t)VERBATIM_DIVERGED_PORT_B, &addrA, &addrB) ==
		0);
	CHECK(linkA.session.inputDelay == 1u);
	slotB = linkB.session.localSlot;
	parked = &linkA.session.parked[slotB][1u % NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY];

	CHECK(MakeDriftState(&state, 0u, 0u));
	CHECK(NativeLockstepSession_RecordLocalDigests(&linkA.session, &state) == 1);
	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 0u, kept0, sizeof(kept0), &size));
	CHECK(NativeLockstepSession_ComposeBundle(&linkA.session, 1u, kept1, sizeof(kept1), &size));

	/* B leads: its frame-3 bundle carries frame 1's digest, parked at A. */
	CHECK(MakeDriftState(&state, 0u, 0u));
	CHECK(NativeLockstepSession_RecordLocalDigests(&linkB.session, &state) == 1);
	CHECK(MakeDriftState(&state, 1u, 0u));
	CHECK(NativeLockstepSession_RecordLocalDigests(&linkB.session, &state) == 1);
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkB, 3u) == 1);
	for (attempt = 0; (attempt < SPIN_BUDGET) && (parked->present == 0u); attempt++)
	{
		NativeLockstepPeerLink_Poll(&linkA);
	}
	CHECK(parked->present == 1u);
	CHECK(parked->frameIndex == 1u);
	CHECK(NativeLockstepSession_Mode(&linkA.session) == NATIVE_LOCKSTEP_RUNNING);

	/* The positive control, while both modes are RUNNING. */
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept0, sizeof(kept0)) == 1);
	CHECK(CountBundlesBeforeMarker(&linkB.transport, &linkA.transport, &addrB) == 1u);

	/* A's drifted frame 1: the divergence latches inside the record. */
	CHECK(MakeDriftState(&state, 1u, 7u));
	CHECK(NativeLockstepSession_RecordLocalDigests(&linkA.session, &state) == 1);
	CHECK(NativeLockstepSession_Mode(&linkA.session) == NATIVE_LOCKSTEP_DIVERGED);
	divergence = NativeLockstepSession_FirstDivergence(&linkA.session);
	CHECK(divergence != NULL);
	CHECK(divergence->frameIndex == 1u);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept0, sizeof(kept0)) == 0);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept1, sizeof(kept1)) == 0);
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkA, 2u) == 0);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(CountBundlesBeforeMarker(&linkB.transport, &linkA.transport, &addrB) == 0u);

	/* A Poll that reads no bundle leaves link mode RUNNING: the link copies
	 * the session mode only after handing a received bundle to the session.
	 * So the gate cannot rely on the next Poll either. */
	NativeLockstepPeerLink_Poll(&linkA);
	CHECK(NativeLockstepPeerLink_Mode(&linkA) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept0, sizeof(kept0)) == 0);

	/* The next bundle from B mirrors the latch into link mode; still refused. */
	CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(&linkB, 0u) == 1);
	CHECK(PumpPollUntilMode(&linkA, NATIVE_LOCKSTEP_PEER_LINK_DIVERGED) == NATIVE_LOCKSTEP_PEER_LINK_DIVERGED);
	CHECK(NativeLockstepPeerLink_SendBundleVerbatim(&linkA, kept0, sizeof(kept0)) == 0);
	CHECK(CountBundlesBeforeMarker(&linkB.transport, &linkA.transport, &addrB) == 0u);
	CHECK(NativeLockstepPeerLink_Mode(&linkB) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	NativeLockstepPeerLink_Close(&linkA);
	NativeLockstepPeerLink_Close(&linkB);
	return 0;
}

int main(void)
{
	CHECK(TestEarlyBundleArrival() == 0);
	CHECK(TestEarlyBundleCapacityBound() == 0);
	CHECK(TestRetransmitBeforePollRegression() == 0);
	CHECK(TestSenderAddressFiltering() == 0);
	CHECK(TestAuxRoundTrip() == 0);
	CHECK(TestAuxDroppedWhileHandshaking() == 0);
	CHECK(TestAuxInterleavedWithBundles() == 0);
	CHECK(TestAuxOverflowKeepsNewest() == 0);
	CHECK(TestAuxRefusals() == 0);
	CHECK(TestAuxResetOnCloseAndReopen() == 0);
	CHECK(TestAuxOddSizesDropped() == 0);
	CHECK(TestAuxForeignSenderDiscarded() == 0);
	CHECK(TestAuxOpenResetsInbox() == 0);
	CHECK(TestAuxKeptAfterTerminal() == 0);
	CHECK(TestForeignIdentityDroppedWhileStaging() == 0);
	CHECK(TestForeignIdentityDroppedWhileRunning() == 0);
	CHECK(TestForeignIdentityCorruptStillFaults() == 0);
	CHECK(TestCurrentIdentityBadDelayOrSlotStillFaults() == 0);
	CHECK(TestVerbatimSendDelivers() == 0);
	CHECK(TestVerbatimSendRefusals() == 0);
	CHECK(TestVerbatimSendRefusedWhileHandshaking() == 0);
	CHECK(TestVerbatimSendRefusedWhenSessionDiverged() == 0);
	puts("native_lockstep_peer_link_test: passed");
	return 0;
}
