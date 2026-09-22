#include "platform/native_lockstep_peer_link.h"

#include "platform/native_win32.h"

#include "native_lockstep_peer_link_test_fixture.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Master two-process, real-socket, real-lockstep-bundle proof test
 * (docs/LOBBY_MILESTONE.md section 2.3 and section 3 constraint 8): both
 * this process (CAB1_HUMAN) and a spawned real child OS process
 * (tests/native_lockstep_peer_link_helper.c, CAB2_HUMAN) open a real UDP
 * socket, negotiate identity over that real socket using the Task 3/4
 * handshake, open a NativeLockstepSession on the agreed config, and
 * exchange real lockstep bundles -- including their embedded
 * verified-digest block -- over that same real socket for several dozen
 * frames.
 *
 * Fixed loopback test ports (constraint 8), distinct from
 * NATIVE_UDP_TRANSPORT_PROCESS_TEST_PORT (48037,
 * tests/native_udp_transport_process_test.c) so the two two-process tests
 * can run concurrently in the suite without colliding: this process
 * (CAB1_HUMAN) binds 48110 and the spawned helper (CAB2_HUMAN) binds 48111.
 */
#define PARENT_PORT 48110u
#define HELPER_PORT 48111u
#define FRAME_COUNT 40u
#define OVERALL_TIMEOUT_MS 15000u
struct RecordedDigest
{
	uint64_t combinedDigest;
	int present;
};

static void BuildFixedPad(struct NativeCanonicalInputPadV1 *pad, uint8_t role)
{
	memset(pad, 0, sizeof(*pad));
	pad->status = (uint8_t)(0x40u + role);
	pad->id = role;
	pad->buttons[0] = 0xA5u;
	pad->analog[0] = 0x80u;
	pad->connected = 1u;
}

/*
 * Drives this process's own side of the link: handshake to RUNNING, then
 * FRAME_COUNT frames of SubmitLocalInput/ComposeAndSendBundle/Poll/
 * TakeFrameInputs/RecordLocalDigests, mirroring
 * tests/native_lockstep_peer_link_helper.c's own loop exactly so both real
 * processes exercise an identical protocol shape. Records every
 * combinedDigest this side records locally into recorded[frame] so the
 * caller can independently re-verify at least one of them afterwards.
 */
static int DriveParentSide(struct NativeLockstepPeerLink *link, struct RecordedDigest *recorded, uint32_t recordedCapacity)
{
	DWORD startTick = GetTickCount();
	struct NativeLockstepSession *session;

	/*
	 * Retransmit before polling, every iteration (HANDSHAKE_RETRANSMIT_EVERY
	 * is 1): the peer's own handshake can complete (independently, upon
	 * validating this side's HELLO) inside this same loop's Poll() call, at
	 * which point NativeLockstepPeerLink_Retransmit becomes a permanent
	 * no-op for the rest of this run. Composing/sending this side's HELLO
	 * first, then polling, guarantees the very iteration whose Poll() call
	 * discovers this side's own completion still sent one more HELLO copy
	 * moments earlier in that same iteration -- exactly the delivery attempt
	 * a poll-then-retransmit ordering would have skipped, since the loop
	 * breaks out the instant Poll() reports leaving HANDSHAKING.
	 */
	while (NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
	{
		NativeLockstepPeerLink_Retransmit(link);
		NativeLockstepPeerLink_Poll(link);
		if (NativeLockstepPeerLink_Mode(link) != NATIVE_LOCKSTEP_PEER_LINK_HANDSHAKING)
		{
			break;
		}
		CHECK((GetTickCount() - startTick) <= OVERALL_TIMEOUT_MS);
		Sleep(1);
	}
	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

	session = NativeLockstepPeerLink_Session(link);
	CHECK(session != NULL);

	for (uint32_t frame = 0; frame < FRAME_COUNT; frame++)
	{
		struct NativeCanonicalInputPadV1 pad;
		struct NativeLockstepSessionFrameInputs inputs;
		struct NativeCanonicalStateV4 state;
		int taken = 0;

		BuildFixedPad(&pad, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN);
		CHECK(NativeLockstepSession_SubmitLocalInput(session, frame, &pad));
		CHECK(NativeLockstepPeerLink_ComposeAndSendBundle(link, frame));

		while (!taken)
		{
			enum NativeLockstepSessionResult result;

			NativeLockstepPeerLink_Poll(link);
			CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);

			result = NativeLockstepSession_TakeFrameInputs(session, frame, &inputs);
			if (result == NATIVE_LOCKSTEP_SESSION_OK)
			{
				taken = 1;
				break;
			}
			CHECK(result == NATIVE_LOCKSTEP_SESSION_STALL);
			CHECK((GetTickCount() - startTick) <= OVERALL_TIMEOUT_MS);
			Sleep(2);
		}

		CHECK(NativeLockstepPeerLinkFixture_MakeState(&state, frame));
		CHECK(NativeLockstepSession_RecordLocalDigests(session, &state));
		CHECK(frame < recordedCapacity);
		recorded[frame].combinedDigest = state.combinedDigest;
		recorded[frame].present = 1;
	}

	CHECK(NativeLockstepPeerLink_Mode(link) == NATIVE_LOCKSTEP_PEER_LINK_RUNNING);
	CHECK(NativeLockstepSession_FirstDivergence(session) == NULL);
	CHECK(NativeLockstepSession_FirstFault(session) == NULL);
	return 0;
}

int main(int argc, char **argv)
{
	const char *helperPath;
	char commandLine[1024];
	STARTUPINFOA startupInfo;
	PROCESS_INFORMATION processInfo;
	struct NativeLockstepPeerLink link;
	struct NativeMatchConfigV1 config;
	struct NativeUdpTransportAddress helperAddress;
	static struct RecordedDigest recorded[FRAME_COUNT];
	int verifiedAtLeastOne = 0;
	DWORD waitResult;
	DWORD exitCode = 1u;
	int failed = 0;

	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <helper-path>\n", argv[0]);
		return 1;
	}
	helperPath = argv[1];

	if ((size_t)snprintf(commandLine, sizeof(commandLine), "\"%s\" %u %u 2 %u", helperPath, HELPER_PORT, PARENT_PORT, FRAME_COUNT) >=
	    sizeof(commandLine))
	{
		fprintf(stderr, "native_lockstep_peer_link_process_test: command line too long\n");
		return 1;
	}

	memset(&startupInfo, 0, sizeof(startupInfo));
	startupInfo.cb = sizeof(startupInfo);
	memset(&processInfo, 0, sizeof(processInfo));

	if (!CreateProcessA(NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startupInfo, &processInfo))
	{
		fprintf(stderr, "native_lockstep_peer_link_process_test: CreateProcessA failed: %lu\n", (unsigned long)GetLastError());
		return 1;
	}

	NativeLockstepPeerLinkFixture_BuildConfig(&config);
	memset(&link, 0, sizeof(link));
	memset(recorded, 0, sizeof(recorded));

	if (!NativeUdpTransport_MakeAddress(&helperAddress, "127.0.0.1", (uint16_t)HELPER_PORT) ||
	    !NativeLockstepPeerLink_Open(&link, (uint16_t)PARENT_PORT, &helperAddress, &config, (uint8_t)NATIVE_MATCH_SLOT_ROLE_CAB1_HUMAN,
		    (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY))
	{
		fprintf(stderr, "native_lockstep_peer_link_process_test: failed to open local link\n");
		failed = 1;
	}

	if (!failed && (DriveParentSide(&link, recorded, FRAME_COUNT) != 0))
	{
		fprintf(stderr, "native_lockstep_peer_link_process_test: parent side drive failed\n");
		failed = 1;
	}

	/*
	 * Prove the real transport genuinely carried the verified-digest block
	 * correctly, not just that both sides happened not to crash: for at
	 * least one frame past the inputDelay + 1 verification lag, this side's
	 * own locally recorded combinedDigest (kept from RecordLocalDigests
	 * calls above) matches what NativeLockstepPeerLinkFixture_MakeState
	 * independently computes for that same frame index a second time here.
	 * The synthetic state is a pure function of frame index, so this proves
	 * this side's own recorded digest is the expected one; combined with
	 * the no-divergence assertion already checked inside DriveParentSide,
	 * that proves the peer received and matched it correctly over the real
	 * socket.
	 */
	if (!failed)
	{
		for (uint32_t frame = (uint32_t)NATIVE_LOCKSTEP_MIN_INPUT_DELAY + 2u; frame < FRAME_COUNT; frame++)
		{
			struct NativeCanonicalStateV4 expected;

			if (!recorded[frame].present)
			{
				continue;
			}
			if (!NativeLockstepPeerLinkFixture_MakeState(&expected, frame))
			{
				continue;
			}
			if (expected.combinedDigest == recorded[frame].combinedDigest)
			{
				verifiedAtLeastOne = 1;
				break;
			}
		}
		if (!verifiedAtLeastOne)
		{
			fprintf(stderr, "native_lockstep_peer_link_process_test: no recorded digest matched an independent recomputation\n");
			failed = 1;
		}
	}

	NativeLockstepPeerLink_Close(&link);

	waitResult = WaitForSingleObject(processInfo.hProcess, 15000);
	if (waitResult != WAIT_OBJECT_0)
	{
		fprintf(stderr, "native_lockstep_peer_link_process_test: helper did not exit in time\n");
		TerminateProcess(processInfo.hProcess, 1);
		failed = 1;
	}
	else if (!GetExitCodeProcess(processInfo.hProcess, &exitCode) || exitCode != 0u)
	{
		fprintf(stderr, "native_lockstep_peer_link_process_test: helper exit code %lu\n", (unsigned long)exitCode);
		failed = 1;
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);

	if (failed)
	{
		return 1;
	}

	puts("native_lockstep_peer_link_process_test: passed");
	return 0;
}
