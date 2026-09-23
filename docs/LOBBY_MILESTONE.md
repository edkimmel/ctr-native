# Lobby milestone

Design-and-status record for the real wired-LAN transport, peer
connect/handshake, and lobby (game startup and initial two-cabinet sync)
milestone, on branch arcade. Read AGENTS.md and docs/HANDOFF.md first. This
document follows the same pattern as docs/LOCKSTEP_MILESTONE.md and
docs/FAILURE_HANDLING_MILESTONE.md: a prospective plan with a task list,
updated to record status as tasks land.

This document is an expansion of the milestone statement in the
docs/HANDOFF.md "Next work" section and adds no scope beyond it:

> Owner direction: the next milestone is the real wired-LAN transport, peer
> connect/handshake, and lobby (game startup and initial sync) -- and it
> must be proven in practice, not just against the virtual datagram
> harness.

## 1. What already exists (do not modify)

The native lockstep protocol/window/session stack
(docs/LOCKSTEP_MILESTONE.md) and the failure-handling policy layer
(docs/FAILURE_HANDLING_MILESTONE.md) are both complete, unit-tested, and
fault-tested against native_virtual_datagram, but neither has ever moved a
byte over a real OS socket. This milestone builds strictly on top of, and
does not modify:

- platform/native_lockstep_protocol.{c,h} -- the 128-byte frame-bundle
  codec.
- platform/native_lockstep_input_window.{c,h} -- the delay/reorder ring.
- platform/native_lockstep_session.{c,h} -- the session, first-divergence
  and first-fault reports.
- platform/native_lockstep_match_outcome.{c,h},
  native_lockstep_match_roster.{c,h}, native_lockstep_rematch.{c,h} --
  stall-timeout, peer-drop, and rematch policy.
- platform/native_match_config.{c,h} -- the 256-byte NativeMatchConfigV1
  wire format and its digest.
- platform/native_virtual_datagram.{c,h} -- the in-memory fault-injection
  harness. It stays test-only; production code introduced by this milestone
  never links it, matching the existing rule the lockstep protocol library
  already follows.

Today a mismatched NativeMatchConfigV1 between two lockstep sessions hard
faults on the first bundle (NATIVE_LOCKSTEP_FAULT_MATCH_IDENTITY,
..._PROTOCOL_VERSION, ..._INPUT_DELAY, all detected inside
NativeLockstepSession_AcceptBundle). There is no step before that where two
peers agree on a common config. This milestone adds that step.

## 2. Decided design

### 2.1 Real socket transport: native_udp_transport

A new leaf library, platform/native_udp_transport.c /
include/platform/native_udp_transport.h, wrapping Winsock2 UDP sockets
(socket, bind, sendto, recvfrom, ioctlsocket for non-blocking mode,
WSAStartup/WSACleanup). Windows/MSVC is the only toolchain this repository
builds today (docs/HANDOFF.md: MSVC x86 is the recommended Windows
toolchain; no PSX toolchain is needed), so Winsock2 is the pragmatic
real-socket choice over vendoring SDL_net, which is not currently part of
the tree. The module is a pure byte-mover: caller-owned buffers, no dynamic
allocation, no packet format, no retry policy, no lockstep or match-config
dependency -- it moves opaque bytes over a real OS socket, nothing else,
the same posture native_virtual_datagram has for its in-memory equivalent.
include/platform/native_win32.h WIN32_LEAN_AND_MEAN convention is reused so
winsock2.h can be included without a winsock.h clash from windows.h.

Non-blocking send/receive on caller-owned buffers, one UDP socket per
struct NativeUdpTransport, address as a plain host-byte-order
uint32_t ipv4 plus uint16_t port pair (translated to/from network byte
order internally), and an explicit GlobalInit/GlobalShutdown pair around
WSAStartup/WSACleanup.

### 2.2 Connect/handshake: native_lockstep_handshake

A new leaf library, platform/native_lockstep_handshake.c /
include/platform/native_lockstep_handshake.h, transport-agnostic exactly
like native_lockstep_protocol.c: it encodes to and decodes from
caller-owned byte buffers only, with no socket dependency, so it is
testable standalone, over native_virtual_datagram for fault injection, and
over the real native_udp_transport for the live proof. It may link
ctr_native_match_config (a stable foundation to call into, per this
milestone constraints) to reuse NativeMatchConfigV1_Encode/_Decode/
_Validate/_Digest rather than reinventing config serialization.

Scope decision, recorded explicitly because the word negotiate in the owner
brief is ambiguous: this milestone implements an explicit
validate-and-accept/reject handshake, not automatic reconciliation of two
differing config proposals. Each peer proposes a NativeMatchConfigV1 (in
practice, byte-identical proposals built by both cabinets from the same
fixture/profile/seed before the handshake starts); the handshake job is to
confirm that identity is agreed before a lockstep session opens and to
surface a clean, explicit rejection when it is not, replacing the current
implicit hard-fault-on-first-bundle behavior. Reconciling two genuinely
different proposals (for example an operator picking a different track on
each cabinet) is out of scope for this milestone; the lobby layer (2.4)
decides what to do with a rejection (retry, or hand control back to the
menu), and UI for that is explicitly not built here.

Wire message, struct NativeLockstepHandshakeMessageV1: a fixed-width,
self-describing, little-endian record (same discipline as the lockstep
bundle and NativeMatchConfigV1: magic, version, then a literal encoded size
field, and a trailing FNV-1a 64 digest over everything before it) carrying
a messageType (HELLO, ACCEPT, REJECT), the sender claimed role
(CAB1_HUMAN or CAB2_HUMAN), a rejectReason when applicable
(VERSION_MISMATCH, CONFIG_INVALID, CONFIG_MISMATCH, ROLE_CONFLICT,
MALFORMED), and the full 256-byte encoded NativeMatchConfigV1 payload. The
implementer finalizes and documents the exact byte offsets in the header,
with a byte-exactness unit test the same way docs/LOCKSTEP_MILESTONE.md
section 2.3 required for the bundle.

State machine: IDLE then HELLO_SENT then, once the peer HELLO, ACCEPT, or
REJECT message is received, COMPLETE or REJECTED. Latch-once, with a
const-or-NULL accessor for the result, in the same idiom as
NativeLockstepSession_FirstDivergence/_FirstFault. On COMPLETE both sides
hold a byte-identical agreed NativeMatchConfigV1, ready to be handed to the
existing, unmodified NativeLockstepSession_Open.

### 2.3 Integration wiring: native_lockstep_peer_link

A new leaf library, platform/native_lockstep_peer_link.c /
include/platform/native_lockstep_peer_link.h, gluing 2.1 and 2.2 to the
existing, unmodified NativeLockstepSession: it owns one
NativeUdpTransport, one NativeLockstepHandshake, and one
NativeLockstepSession; drives the handshake to completion over the real
socket; on completion opens the session with the agreed config; and from
then on composes/sends and receives/accepts lockstep bundles over the same
real socket. It is the first production code in the repository that calls
NativeLockstepSession_Open, NativeLockstepSession_ComposeBundle, and
NativeLockstepSession_AcceptBundle against a real OS transport instead of a
caller-owned byte buffer fed by a test harness. It has no wall-clock
dependency: retransmission cadence is caller-driven (one Poll per tick, a
separate caller-invoked retransmit call on whatever cadence the caller tick
loop decides), consistent with the rest of the tick-driven determinism
stack.

The peer link also routes datagrams of exactly 64 bytes from the peer,
while RUNNING, to a bounded aux inbox that higher layers drain with
NativeLockstepPeerLink_TakeAux (they send with _SendAux); the match-select
phase uses it (docs/MATCH_SELECT_MILESTONE.md section 2.4). Handshake and
bundle routing are unchanged.

### 2.4 Lobby / waiting-flow state layer: native_lobby_state

A new leaf library, platform/native_lobby_state.c /
include/platform/native_lobby_state.h, a policy wrapper over
NativeLockstepPeerLink adding: a small caller-supplied list of candidate
peer addresses, a bounded per-candidate attempt budget (frame-counted, like
NativeLockstepMatchOutcome stall-timeout, not wall-clock), and a state enum
(WAITING_FOR_PEER, HANDSHAKING, READY, REJECTED, PEER_LOST) that the
eventual lobby/waiting UI can read.

Scope decision, recorded explicitly: peer discovery in this milestone means
cycling through a caller-supplied candidate address list (in practice, the
two-cabinet fleet known static addresses), not OS-level broadcast/multicast
discovery. Zero-config broadcast discovery is more valuable on a fleet
where addresses are unknown ahead of time, which is not the situation for
this fleet (docs/HANDOFF.md: a fixed two-cabinet, wheel-driven
installation, CAB1 and CAB2), and OS broadcast sockets behave
inconsistently across sandboxed/virtualized network environments, which
would make this milestone automated tests unreliable. Nothing here
forecloses adding broadcast discovery later; the candidate-list seam is
where it would plug in.

This is the data/state layer exists and is tested scope-down the owner
brief explicitly permits: no menu, no wheel-input wiring, no
results/rematch/exit screen. Nothing under game/ references any module this
milestone adds, mirroring how the failure-handling policy layer also has no
game-loop caller yet.

## 3. Constraints

Restated as binding rules for every task below, mirroring
docs/LOCKSTEP_MILESTONE.md section 3.

1. The topology lease is untouched. No acquire, activate, capture, or
   publish wiring; no lease owner in checkpoints, replay, or canonical
   state; no retire hook on LOAD_Hub_ReadFile.
2. No changes to native_lockstep_protocol.{c,h},
   native_lockstep_input_window.{c,h}, native_lockstep_session.{c,h},
   native_lockstep_match_outcome/match_roster/rematch.{c,h}, or
   native_match_config.{c,h}. This milestone calls into them, never edits
   them.
3. No changes to native_virtual_datagram.{c,h} except the isolation test
   consumer-count relaxation, exactly as docs/LOCKSTEP_MILESTONE.md Task 4
   already did once; production code never links it.
4. No new dynamic allocation. Fixed-size, caller-owned buffers throughout,
   matching every existing leaf in this stack.
5. Portable C17, extensions disabled, on every new target.
6. Every new seam gets a unit test; every structural rule gets an isolation
   test.
7. A task is not done until cmake --build build-msvc-x86 --config Debug
   succeeds and the full ctest --test-dir build-msvc-x86 -C Debug suite
   passes.
8. At least one test must open two real, independent OS sockets in two
   separate OS processes, exchange bytes, and tear down cleanly -- not just
   native_virtual_datagram.
9. No push to any remote. Commit on arcade only.

## 4. Task list

Baseline before this milestone: 82 tests, 100% passed (commit c9504eddb). All
seven tasks are done and this milestone is complete: the full suite is 91
tests, 100% passing.

### Task 1 -- this document

Status: done.

### Task 2 -- real socket transport (native_udp_transport)

**Status: done.** Commit `885546513`.

Creates include/platform/native_udp_transport.h,
platform/native_udp_transport.c, a single-process unit test
(tests/native_udp_transport_test.c), a standalone echo-helper executable
(tests/native_udp_transport_echo_helper.c) that binds a real UDP socket on
an argv-supplied port and echoes back the first datagram it receives, and a
two-process test (tests/native_udp_transport_process_test.c) that spawns
the helper as a real child OS process via CreateProcessA, sends it a
payload over a real loopback UDP socket, verifies the echoed reply, and
waits for clean child exit. This is the live evidence required by
constraint 8. Also creates a structural isolation test asserting the module
has no lockstep, match-config, topology-lease, or game dependency, does not
allocate dynamically, and does link ws2_32 and genuinely call real Winsock
symbols (a positive assertion, the mirror image of
native_virtual_datagram_isolation_test.cmake negative one).

No review required: pure socket I/O leaf, touches no simulation identity,
replay, canonical state, or topology lease.

### Task 3 -- connect/handshake protocol (native_lockstep_handshake)

**Status: done.** Commit `fe93f0b78`.

Creates include/platform/native_lockstep_handshake.h,
platform/native_lockstep_handshake.c, and
tests/native_lockstep_handshake_test.c covering: round trip, byte
exactness, truncation, bad magic/version/size, a matching-config
complementary-role pair reaching COMPLETE with byte-identical agreed
configs on both sides, a mismatched-config pair reaching REJECTED with
CONFIG_MISMATCH, a same-role collision reaching REJECTED with
ROLE_CONFLICT, and a malformed message reaching REJECTED with MALFORMED
rather than propagating a decode crash.

Review required: this is connect/handshake logic that decides what
NativeMatchConfigV1 a session will be opened with.

### Task 4 -- handshake fault-injection and isolation tests

**Status: done.** Commit `65b76edfd`.

Creates tests/native_lockstep_handshake_fault_test.c driving the loss,
delay, reorder, and duplication faults over native_virtual_datagram
(mirroring docs/LOCKSTEP_MILESTONE.md Task 4), and
tests/native_lockstep_handshake_isolation_test.cmake asserting the
handshake module stays transport-agnostic (no socket/OS-networking token),
has no topology-lease symbol, no dynamic allocation, and does not link
ctr_native_virtual_datagram outside the fault test. Touches
tests/native_virtual_datagram_isolation_test.cmake to relax the consumer
count from exactly three named consumers to exactly four, adding
native_lockstep_handshake_fault_test by name, the same minimal pattern
docs/LOCKSTEP_MILESTONE.md Task 4 already used once. (Correction: by the
time this milestone started, a prior milestone -- the failure-handling
milestone, integration step 5 -- had already relaxed the count from two to
three, adding its own fault test,
tests/native_lockstep_failure_handling_fault_test.c, as a third consumer.
This milestone's Task 4 therefore actually relaxed the count from three to
four, not two to three.)

Review required: same reason as Task 3, plus it relaxes an existing
structural rule.

### Task 5 -- real-transport integration (native_lockstep_peer_link)

**Status: done.** Commit `8fdfb0413`, hardening follow-up `2e46faacf`.

Creates include/platform/native_lockstep_peer_link.h,
platform/native_lockstep_peer_link.c, a standalone second-process
executable (tests/native_lockstep_peer_link_helper.c) and a two-process
integration test (tests/native_lockstep_peer_link_process_test.c) that
spawns the helper as a real child process; both processes open a real UDP
socket, run the handshake to completion over that real socket using
byte-identical proposed NativeMatchConfigV1 values, open a
NativeLockstepSession on the agreed config, and exchange several dozen
frames of lockstep bundles, including their embedded verified-digest block,
over the real socket using synthetic-but-deterministic
NativeCanonicalStateV4 values (not the real CTR simulation, which stays out
of scope, consistent with the rest of this stack never having a game-loop
caller yet); the parent asserts both sides reach session mode RUNNING with
no divergence or fault latched, then waits for clean child exit.

This is the master live-evidence test for the milestone first scope bullet:
a real socket/transport layer actually carrying lockstep bundles between
two processes.

Review required: this is the piece that wires a real transport under the
existing lockstep session. An independent review of the initial commit
found two real integration bugs during composition (not design defects,
genuine bugs caught before merge): incoming datagrams were originally
routed by local link mode instead of actual wire size, which broke once a
peer that finished its handshake first started sending 128-byte lockstep
bundles while the other side still expected 284-byte handshake messages
(fixed by routing by wire size and staging early-arriving bundles, bounded
to NATIVE_LOCKSTEP_RING_CAPACITY, for replay once the session opens); and a
retransmit/poll ordering bug caused real, reproducible flakiness (fixed by
retransmitting before polling in both driver loops, now documented as a
contract on NativeLockstepPeerLink_Retransmit/_Poll). The `2e46faacf`
follow-up closed four reviewer-flagged gaps on top of that: sender-address
filtering, drop telemetry, the ordering contract documentation, and a new
deterministic single-process test (tests/native_lockstep_peer_link_test.c)
that reproduces both original bugs on demand rather than depending on OS
process scheduling timing, closing a real "every new seam needs a unit
test" gap the two-process test alone could not close deterministically.
Verifier PASS, reviewer: no blocking findings on either commit.

### Task 6 -- lobby / waiting-flow state layer (native_lobby_state)

**Status: done.** Commit `8a300c21e`, coverage-gap follow-up `2761d1aa5`.

Creates include/platform/native_lobby_state.h,
platform/native_lobby_state.c, and tests/native_lobby_state_test.c
covering: an empty candidate list staying WAITING_FOR_PEER; a candidate
list where earlier entries have nobody listening, advancing past them and
reaching READY once a real peer answers a later candidate (using real
loopback sockets, two NativeLobbyState/NativeLockstepPeerLink instances
in-process, each on its own real socket); and a deliberately mismatched
config reaching REJECTED rather than hanging.

Review required: this module decides which negotiated config transitions a
peer link into READY, that is, which identity the caller will actually use.
Verifier PASS, reviewer: no blocking findings. The `2761d1aa5` follow-up
closed three reviewer-flagged coverage gaps in native_lobby_state_test (the
PEER_LOST transition, several Begin() rejection paths, and the
retransmitIntervalFrames == 0 defensive branch), with no production code
changed. Verifier PASS.

### Task 7 -- docs finalize

**Status: done.** This commit.

Updates this document task statuses and the "Risks and open questions"
section, and updates docs/HANDOFF.md "Networking" and "Next work" sections,
following the same close-out pattern docs/LOCKSTEP_MILESTONE.md Task 6 and
docs/FAILURE_HANDLING_MILESTONE.md used.

## 5. Risks and open questions

1. This milestone's live-socket evidence is two real OS processes on the
   same machine over real loopback sockets:
   tests/native_udp_transport_process_test.c (Task 2) and
   tests/native_lockstep_peer_link_process_test.c (Task 5) both spawn a
   real second OS process via CreateProcessA and exchange real bytes over
   real loopback UDP sockets. Actual two-cabinet LAN conditions -- real
   wire, a real switch, a real NIC, real latency and packet loss under
   real network load -- were **not** exercised and were never claimed to
   be. That remains gated behind step 6 (CAB1 G29/kiosk gate) and step 7
   (two-cabinet fleet acceptance) with physical hardware this milestone
   did not have access to.
2. Peer discovery is a caller-supplied candidate list, not OS
   broadcast/multicast (section 2.4). If a future fleet configuration needs
   zero-config discovery, that is new scope, not a defect in this
   milestone.
3. native_lockstep_peer_link retransmission cadence is caller-driven and
   frame-counted, not wall-clock; a real deployment tick loop must decide
   the cadence, which this milestone does not do because it has no
   game-loop caller yet, mirroring the same gap
   docs/FAILURE_HANDLING_MILESTONE.md left open.
4. No game-loop or UI wiring: no menu, no wheel input, no waiting/results
   screen reads any module this milestone adds, exactly as
   docs/FAILURE_HANDLING_MILESTONE.md left the failure-handling policy
   layer unwired. Both remain open before step 6.
5. The handshake negotiation is validate-and-reject, not reconciliation of
   differing proposals (section 2.2). A future milestone may need true
   negotiation (for example an operator picking different tracks per
   cabinet); this one deliberately does not build it.
6. The sender-address filtering added in Task 5b
   (native_lockstep_peer_link.c's Poll discards any datagram whose sender
   does not exactly match the configured peer address) depends on both
   peers using fixed, non-ephemeral ports, which matches this milestone's
   own candidate-list lobby design (section 2.4) but would need revisiting
   if a future milestone ever introduced ephemeral source ports or NAT
   traversal.
7. platform/native_lockstep_peer_link.{c,h} has no dedicated structural
   isolation test, unlike the transport and handshake modules. This was a
   deliberate scope decision: only Tasks 2 and 4 in this document's
   original plan called for one, since peer_link's key invariants (no
   session/handshake/transport modification, no
   topology-lease/canonical-state/game dependency) were instead confirmed
   twice by direct reviewer inspection (the Task 5 and Task 5b review
   passes) rather than by an automated structural scan. This is a
   reasonable follow-up for a future pass if peer_link grows further, not
   a defect in this milestone.
8. Windows-specific implementation detail: include/platform/native_win32.h
   #undefs the far/near macros (kept for PS1-SDK-compatibility reasons
   elsewhere in the codebase), which otherwise breaks <winsock2.h>'s
   FAR-decorated prototypes. platform/native_udp_transport.c works around
   this with a locally scoped far/near redefinition bracketing only the
   winsock2.h/ws2tcpip.h includes, immediately undefined again afterward,
   verified by an independent reviewer/verifier pass to not leak past
   those two includes or weaken native_win32.h's invariant elsewhere.

## 6. What this milestone must not do

- Not modify native_lockstep_protocol.{c,h}, native_lockstep_input_window.{c,h},
  native_lockstep_session.{c,h}, native_lockstep_match_outcome/match_roster/rematch.{c,h},
  or native_match_config.{c,h}.
- Not modify native_virtual_datagram.{c,h} beyond its isolation test
  consumer list.
- Not touch the topology lease in any way.
- Not add rollback, prediction, or any re-simulation of a past frame.
- Not wire the real CTR game loop or a menu/UI to any module this milestone
  adds.
- Not add anything to game/game_unity.h.
- Not claim live two-cabinet hardware evidence that was not actually
  produced.
