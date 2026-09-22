#include "platform/native_lockstep_peer_link.h"

#include "platform/native_win32.h"

#include "native_lockstep_peer_link_test_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Standalone helper process spawned by
 * tests/native_lockstep_peer_link_process_test.c: the second real OS
 * process in the master live-evidence test for this milestone
 * (docs/LOBBY_MILESTONE.md section 2.3). argv layout: argv[1] local UDP
 * port to bind, argv[2] peer UDP port (127.0.0.1:argv[2] is the parent test
 * process), argv[3] this process's local role ("1" for CAB1_HUMAN, "2" for
 * CAB2_HUMAN), argv[4] number of lockstep frames to run.
 *
 * Opens a NativeLockstepPeerLink (which itself calls
 * NativeUdpTransport_GlobalInit, so this file never calls it separately),
 * drives the handshake to RUNNING over the real socket, then drives exactly
 * argv[4] lockstep frames using synthetic-but-deterministic
 * NativeCanonicalStateV4 values from the shared test fixture, mirroring
 * tests/native_lockstep_peer_link_process_test.c's own parent-side loop
 * exactly so both real processes exercise the identical protocol shape.
 * Exits 0 with nothing printed if the link ends RUNNING with no divergence
 * or fault latched; otherwise prints a short diagnostic to stderr and exits
 * 1, or exits 2 on the overall wall-clock timeout.
 */

/* Overall wall-clock budget for the whole handshake-plus-frame-loop
 * (test-only glue, not part of the deterministic simulation stack): a real
 * bug must not hang the test suite forever. */
#define OVERALL_TIMEOUT_MS 15000u

static void BuildFixedPad(struct NativeCanonicalInputPadV1 *pad, uint8_t role)
{
	memset(pad, 0, sizeof(*pad));
	pad->status = (uint8_t)(0x40u + role);
	pad->id = role;
	pad->buttons[0] = 0xA5u;
	pad->analog[0] = 0x80u;
	pad->connected = 1u;
}

static void PrintFailureDiagnostics(struct NativeLockstepPeerLink *link)
{
	enum NativeLockstepPeerLinkMode mode = NativeLockstepPeerLink_Mode(link);
	struct NativeLockstepSession *session = NativeLockstepPeerLink_Session(link);
	const struct NativeLockstepDivergenceReport *divergence = (session != NULL) ? NativeLockstepSession_FirstDivergence(session) : NULL;
	const struct NativeLockstepFaultReport *fault = (session != NULL) ? NativeLockstepSession_FirstFault(session) : NULL;
	const struct NativeLockstepHandshakeResult *handshakeResult = NativeLockstepPeerLink_HandshakeResult(link);

	fprintf(stderr, "native_lockstep_peer_link_helper: link mode %d\n", (int)mode);
	if (handshakeResult != NULL)
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: handshake rejectReason %u peerRole %u\n",
			(unsigned)handshakeResult->rejectReason, (unsigned)handshakeResult->peerRole);
	}
	if (divergence != NULL)
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: divergence frame %u mask 0x%x domainMask 0x%x\n",
			(unsigned)divergence->frameIndex, (unsigned)divergence->mask, (unsigned)divergence->canonicalDomainMask);
	}
	if (fault != NULL)
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: fault cause %u frame %u sender %u detail %u\n",
			(unsigned)fault->cause, (unsigned)fault->frameIndex, (unsigned)fault->senderSlot, (unsigned)fault->detail);
	}
}

int main(int argc, char **argv)
{
	struct NativeLockstepPeerLink link;
	struct NativeMatchConfigV1 config;
	struct NativeUdpTransportAddress peerAddress;
	long localPortArg;
	long peerPortArg;
	long frameCountArg;
	uint8_t role;
	uint32_t frameCount;
	DWORD startTick;
	struct NativeLockstepSession *session;

	if (argc < 5)
	{
		fprintf(stderr, "usage: %s <local-port> <peer-port> <role 1|2> <frame-count>\n", argv[0]);
		return 1;
	}

	localPortArg = strtol(argv[1], NULL, 10);
	peerPortArg = strtol(argv[2], NULL, 10);
	frameCountArg = strtol(argv[4], NULL, 10);
	if ((localPortArg <= 0) || (localPortArg > 65535) || (peerPortArg <= 0) || (peerPortArg > 65535) || (frameCountArg <= 0))
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: bad argument\n");
		return 1;
	}
	if (strcmp(argv[3], "1") == 0)
	{
		role = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN;
	}
	else if (strcmp(argv[3], "2") == 0)
	{
		role = (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB2_HUMAN;
	}
	else
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: bad role\n");
		return 1;
	}
	frameCount = (uint32_t)frameCountArg;

	NativeLockstepPeerLinkFixture_BuildConfig(&config);

	memset(&link, 0, sizeof(link));
	if (!NativeUdpTransport_MakeAddress(&peerAddress, "127.0.0.1", (uint16_t)peerPortArg))
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: MakeAddress failed\n");
		return 1;
	}

	if (!NativeLockstepPeerLink_Open(&link, (uint16_t)localPortArg, &peerAddress, &config, role,
		    (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY))
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: Open failed\n");
		return 1;
	}

	startTick = GetTickCount();

	/*
	 * Retransmit before polling, every iteration: the peer's own handshake
	 * can complete (independently, upon validating this side's HELLO)
	 * inside this same loop's Poll() call, at which point
	 * NativeLockstepPeerLink_Retransmit becomes a permanent no-op for the
	 * rest of this run. Composing/sending this side's HELLO first, then
	 * polling, guarantees the very iteration whose Poll() call discovers
	 * this side's own completion still sent one more HELLO copy moments
	 * earlier in that same iteration -- exactly the delivery attempt a
	 * poll-then-retransmit ordering would have skipped, since the loop
	 * breaks out the instant Poll() reports leaving HANDSHAKING. This
	 * matters because each side's own Open() sends its first HELLO
	 * immediately, which routinely races the *other* real OS process's
	 * startup (CreateProcessA returns long before the child has loaded,
	 * bound its socket, and started polling) and is lost; without this
	 * ordering, the peer can be left waiting for a HELLO that never
	 * arrives.
	 */
	while (NativeLockstepPeerLink_Mode(&link) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
	{
		NativeLockstepPeerLink_Retransmit(&link);
		NativeLockstepPeerLink_Poll(&link);
		if (NativeLockstepPeerLink_Mode(&link) != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
		if ((GetTickCount() - startTick) > OVERALL_TIMEOUT_MS)
		{
			fprintf(stderr, "native_lockstep_peer_link_helper: handshake timed out\n");
			NativeLockstepPeerLink_Close(&link);
			return 2;
		}
		Sleep(1);
	}

	if (NativeLockstepPeerLink_Mode(&link) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: handshake did not reach RUNNING\n");
		PrintFailureDiagnostics(&link);
		NativeLockstepPeerLink_Close(&link);
		return 1;
	}

	session = NativeLockstepPeerLink_Session(&link);
	if (session == NULL)
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: no session\n");
		NativeLockstepPeerLink_Close(&link);
		return 1;
	}

	for (uint32_t frame = 0; frame < frameCount; frame++)
	{
		struct NativeCanonicalInputPadV1 pad;
		struct NativeLockstepSessionFrameInputs inputs;
		struct NativeCanonicalStateV4 state;
		int taken = 0;

		BuildFixedPad(&pad, role);
		if (!NativeLockstepSession_SubmitLocalInput(session, frame, &pad))
		{
			fprintf(stderr, "native_lockstep_peer_link_helper: SubmitLocalInput failed at frame %u\n", (unsigned)frame);
			PrintFailureDiagnostics(&link);
			NativeLockstepPeerLink_Close(&link);
			return 1;
		}

		if (!NativeLockstepPeerLink_ComposeAndSendBundle(&link, frame))
		{
			fprintf(stderr, "native_lockstep_peer_link_helper: ComposeAndSendBundle failed at frame %u\n", (unsigned)frame);
			PrintFailureDiagnostics(&link);
			NativeLockstepPeerLink_Close(&link);
			return 1;
		}

		while (!taken)
		{
			enum NativeLockstepSessionResult result;

			NativeLockstepPeerLink_Poll(&link);
			if (NativeLockstepPeerLink_Mode(&link) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING)
			{
				fprintf(stderr, "native_lockstep_peer_link_helper: link left RUNNING at frame %u\n", (unsigned)frame);
				PrintFailureDiagnostics(&link);
				NativeLockstepPeerLink_Close(&link);
				return 1;
			}

			result = NativeLockstepSession_TakeFrameInputs(session, frame, &inputs);
			if (result == NATIVE_LOCKSTEP_SESSION_OK)
			{
				taken = 1;
				break;
			}
			if (result != NATIVE_LOCKSTEP_SESSION_STALL)
			{
				fprintf(stderr, "native_lockstep_peer_link_helper: TakeFrameInputs failed at frame %u result %d\n", (unsigned)frame,
					(int)result);
				PrintFailureDiagnostics(&link);
				NativeLockstepPeerLink_Close(&link);
				return 1;
			}

			if ((GetTickCount() - startTick) > OVERALL_TIMEOUT_MS)
			{
				fprintf(stderr, "native_lockstep_peer_link_helper: frame loop timed out at frame %u\n", (unsigned)frame);
				NativeLockstepPeerLink_Close(&link);
				return 2;
			}
			Sleep(2);
		}

		if (!NativeLockstepPeerLinkFixture_MakeState(&state, frame))
		{
			fprintf(stderr, "native_lockstep_peer_link_helper: MakeState failed at frame %u\n", (unsigned)frame);
			NativeLockstepPeerLink_Close(&link);
			return 1;
		}
		if (!NativeLockstepSession_RecordLocalDigests(session, &state))
		{
			fprintf(stderr, "native_lockstep_peer_link_helper: RecordLocalDigests failed at frame %u\n", (unsigned)frame);
			NativeLockstepPeerLink_Close(&link);
			return 1;
		}
	}

	if ((NativeLockstepPeerLink_Mode(&link) != NATIVE_LOCKSTEP_PEER_LINK_RUNNING) ||
	    (NativeLockstepSession_FirstDivergence(session) != NULL) || (NativeLockstepSession_FirstFault(session) != NULL))
	{
		fprintf(stderr, "native_lockstep_peer_link_helper: final check failed\n");
		PrintFailureDiagnostics(&link);
		NativeLockstepPeerLink_Close(&link);
		return 1;
	}

	NativeLockstepPeerLink_Close(&link);
	return 0;
}
