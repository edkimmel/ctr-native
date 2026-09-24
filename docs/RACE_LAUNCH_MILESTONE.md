# Race launch milestone

Design-and-status record for docs/GAME_LOOP_UI_MILESTONE.md Task 7,
"networked race launch" (docs/HANDOFF.md "Next work" item 1), on branch
`arcade`. Read AGENTS.md and docs/HANDOFF.md first. This document follows
the pattern of docs/MATCH_SELECT_MILESTONE.md and docs/ROSTER_MILESTONE.md:
a prospective plan with a task list, updated to record status as slices
land.

## 1. Scope

In scope:

- Networked race launch: on START_RACE, arm and launch the live race setup
  seam game/MAIN/MainArcadeRaceSetup (docs/ROSTER_MILESTONE.md section
  3.2) with the agreed NativeMatchConfigV1.
- Asymmetric relink completion (docs/MATCH_SELECT_MILESTONE.md risk 7).
- The caller allow-list of tests/main_arcade_race_setup_isolation_test.cmake.
- The pause-menu vibration toggle in linked races (ROSTER risk 8).
- Keeping sound IDs (countSounds) out of cross-cabinet identity (the note
  closing GAME_LOOP_UI Task 6b-7).

Already done: the gameMode2 cheat-bit reset. RS-2 pins every non-transient
mode bit, cheat bits included, through the seam.

Out of scope: in-race lockstep (Task 8); live V4 projection; deterministic
VBlank pacing for linked races (ROSTER risk 7, Task 8); a ONE_CAB lobby and
UI flow (a separate follow-up, ROSTER risk 14); physical two-cabinet
validation (HANDOFF steps 6-7).

## 2. Starting point

- START_RACE logs the agreed match and aborts to the title
  (game/MAIN/MainArcadeLink.c:276-294). No race reaches RACING today.
- Each relink handshake completes independently per side
  (include/platform/native_lockstep_peer_link.h:95, :172), so one cabinet
  can reach START_RACE while the other times out to LINK ERROR. The aux
  route carries records only while the link is RUNNING: Poll fills the
  inbox only then (:124-127) and SendAux requires it (:299-306).
- SELECT_RESULT phase 2 moves to RACING with START_RACE on the first
  lobby READY after RELINK (platform/native_arcade_flow.c:232-269), and
  the adapter arms its race on that action
  (platform/native_arcade_netplay.c:612-614).
- lastReadyConfig, the rematch source, is taken at the first handshake
  READY of every lobby (platform/native_arcade_netplay.c:538-547).
- Frame ownership: in LINK mode the layer owns every frame while a link
  screen is active, on any level (game/MAIN/MainArcadeLinkPolicy.c:88-92;
  RACING counts as active, platform/native_arcade_link_host.c:248-259).
  The hook (game/MAIN/MainFrame_RenderFrame.c:80) would therefore already
  tick the host on race frames, and it would also clear every pad tap and
  the collected menu input and hide the box there
  (game/MAIN/MainArcadeLink.c:386-398), while the layout draws nothing on
  RACING (game/MAIN/MainArcadeLinkLayout.c:874).
  MainArcadeLinkPolicy.c:51 is inside MainArcadeLinkPolicy_TitleMenuReady
  (the title-window test), not the ownership rule.
- Level loads: a race-track load is staged over rendered frames
  (game/MAIN/MainMain.c:306-336; LOAD_NextQueuedFile once per pass at
  :136), and the hook ticks the host on them. Link ticks pause only inside
  each synchronous file read (platform/native_cd.c:380-392) and on the few
  loop passes that break before rendering (the VLC wait at
  MainMain.c:234-241, the load finish at :323-330, world init).
- The reference launcher is the roster proof
  (game/MAIN/MainArcadeRosterProof.c:227-311): launch windows, Arm,
  Launch, and leaving the title. Only the adapter and the proof may name
  MainArcadeRaceSetup_Arm, _Launch, and _Disarm
  (tests/main_arcade_race_setup_isolation_test.cmake:257-263), and the
  adapter never calls _Disarm (:268-271). Nothing calls Disarm today.
- A missing pad pauses a 2P race: the pause check calls
  MainFrame_HaveAllPads (game/MAIN/MainFrame.c:425-431).
- The pause options toggle a DualShock vibration bit in gameMode1
  (game/MAIN/MainFreeze.c:518).

## 3. What "Task 7 done" means

1. START_RACE only follows a launch commit (RL-1..RL-5): a cabinet never
   starts a race because its own handshake completed.
2. On START_RACE the cabinet arms MainArcadeRaceSetup with the agreed
   NativeMatchConfigV1 (the exact bytes the relink handshake validated)
   and launches it; both cabinets reach VALIDATED and log equal config,
   race plan, bot setup plan, and bank digests
   (MainArcadeRaceSetup_Digests).
3. Asymmetric relink completion is handled: a relink that completes on
   one side only launches neither cabinet, both show LINK ERROR, and a
   rematch after it agrees (RL-6); the residual two-generals case is
   fail-safe and documented (RL-7).
4. A rematch launches a second armed race in the same process (Disarm,
   then Arm again), proven live.
5. The pause-menu vibration toggle does nothing in a linked race (RL-13).
6. Sound IDs are provably out of cross-cabinet identity (RL-14, an
   isolation test).
7. The allow-list in tests/main_arcade_race_setup_isolation_test.cmake
   names exactly one new caller file (the race caller, RL-S8), and the
   adapter still never calls Disarm.
8. Default boot (no arcade-link option) is unchanged; the full ctest
   suite passes, including the new live two-process gate
   arcade_link_launch (RL-15).

Driving the race in lockstep is Task 8. Until Task 8 lands, a linked race
is the launch rehearsal of RL-10: it loads and starts, but nobody drives
it.

## 4. Decided design (defaults RL-1..RL-15)

Each default below is a default pending owner review.

RL-1 Launch agreement. After the relink lobby reports READY, START_RACE
additionally needs a launch commit, exchanged as a new 64-byte record on
the existing peer-link aux route (RUNNING only). Handshake completion
alone never starts a race.

RL-2 Launch record v1. A pure codec and state in
platform/native_arcade_launch.c with include/platform/native_arcade_launch.h,
library ctr_native_arcade_launch. It has its own magic and version; the
handshake, bundle, NativeMatchConfigV1, and select record formats are
unchanged. A mixed build never reaches it, because the handshake rejects
a differing build identity. Layout (little-endian through
NativeCodecWriter/NativeCodecReader), mirroring the select record
(include/platform/native_match_select_message.h:21-56):

    offset  size  field
    0       4     magic 0x314C414E (little-endian "NAL1")
    4       2     messageVersion, 1
    6       2     encodedSize, 64
    8       1     senderRole, 1 (CAB1_HUMAN) or 2 (CAB2_HUMAN)
    9       1     flags: bit0 HEARD (the sender has committed); other
                  bits 0
    10      2     reserved0, zero
    12      4     sequence, >= 1, per sender, +1 per composed record
    16      32    configDigest: the full SHA-256
                  NativeMatchConfigV1_Digest of the sender's relink
                  proposal (opaque to the codec)
    48      8     reserved1, zero
    56      8     digest, FNV-1a 64 (NativeCodecDigest64) over bytes 0..55

RL-3 Commit rule. A cabinet commits on the first valid record from the
other cabinet role whose configDigest equals the digest of its own relink
proposal. There is no third phase. Because a peer only sends while its
link is RUNNING, a commit proves both handshakes completed on the same
config. A record with another magic (for example a late select record) is
ignored; a malformed record, an own-role echo, or a different digest is
ignored and counted, never a failure.

RL-4 Send and linger. One record per tick from the tick the relink lobby
is READY. After committing, HEARD is set, and the cabinet keeps sending
until it has sent at least one HEARD record and received a HEARD record
from the peer, capped at launchLingerTicks = 300 (10 s) after the commit,
which covers the whole launch timeout of the peer. Sending continues on
RACING, where the hook already ticks the host every frame. A level load
delays records briefly rather than stopping them: ticks pause only inside
each synchronous file read and on the few loop passes that break before
rendering, and the peer's records wait in the socket buffer (section 7).

RL-5 Timeout and flow gate. No new timer: the flow's launchTimeoutTicks
(300, SEL-9), counted from RELINK, covers the relink handshake plus the
agreement; on expiry the flow shows LINK ERROR with CLOSE_LINK, as today.
The flow observation gains launchStatus (PENDING 0, COMMITTED 1) in a
reserved byte. SELECT_RESULT phase 2 moves to RACING with START_RACE only
on READY with COMMITTED; READY with PENDING waits.

RL-6 Rematch source. For a relink lobby, lastReadyConfig is taken at the
launch commit, not at handshake READY; the first lobby and rematch lobbies
keep the handshake-READY rule. A relink that completed on one side only
therefore leaves both cabinets on the select base, and their rematch
proposals agree.

RL-7 Residual asymmetry (two generals, accepted). If a cabinet commits
and every record in the other direction is lost for the rest of the
peer's launch timeout, one cabinet launches alone. This is fail-safe: the
lone racer ends in PEER TIMEOUT once the Task 8 stall timeout runs (until
then it runs the RL-10 rehearsal), the other shows LINK ERROR, and a
rematch between them is REJECTED (their lastReadyConfig differ), so both
show OPPONENT LEFT and return to the title. No race ever runs on a config
the peer did not hold.

RL-8 Launch point. Every networked launch starts from the title launch
window on the idle main-menu level, the same TITLE window the roster
proof uses: MainArcadeLink_TitleMenuReady (game/MAIN/MainArcadeLink.c:
339-348, the wrapper the roster proof calls at
game/MAIN/MainArcadeRosterProof.c:269) around
MainArcadeLinkPolicy_TitleMenuReady. The race caller arms
MainArcadeRaceSetup with the agreed config (a new host accessor copies
the exact NativeArcadeNetplay_AgreedConfig bytes), launches, and leaves
the title as the proof does. After the race the cabinet loads the
main-menu level and shows RESULTS, rematch, and select there, as the
layer does today. Race frames are ticked, not owned. On every frame while
the flow is on RACING off the idle main-menu level, load frames included,
the hook keeps ticking the host (as today) but no longer clears pad taps
or the collected menu input, hides the box, or resets the demo countdown,
so retail race input is untouched (the rehearsal pads now, the Task 8
lockstep pads later). Link ticks pause only inside each synchronous file
read and on the few loop passes that break before rendering (section 7).

RL-9 Setup lifecycle. The race caller calls Disarm exactly once per race:
on the first frame back on the idle main-menu level after the race (so
RS-15 restores the vibration bits there), and on every abort path. Arm
only from IDLE. A rematch is a second armed race in one process (ROSTER
risk 11), covered by the decision-core unit test and the live gate.

RL-10 Until Task 8: the launch rehearsal. A linked race loads and starts
but is not driven. Every frame of the race the caller installs connected,
neutral pads in both human ports (Platform_InputInstallPadSnapshots, as
the roster proof does), so no local input reaches the race, no
unplugged-pad pause fires (MainFrame_HaveAllPads), and neither cabinet
drives a kart. launchRehearsalTicks = 150 (5 s) after race tick 0 the
caller reports the race finished; the flow shows RESULTS (RACE COMPLETE)
with REMATCH and EXIT on the main-menu level. There is no input exchange
and no in-race stall or desync detection beyond the adapter's existing
lobby poll, and the two cabinets are not synchronized. Task 8 replaces
the rehearsal with the lockstep drive and the real finish.

RL-11 Setup failure response (closes RS-10). An Arm or Launch failure, or
a FAILED setup status before the rehearsal ends, ends the linked race
locally as LINK ERROR: the caller reports a local race failure to the
host (a new host input), the flow shows RESULTS LINK ERROR, the cabinet
returns to the main-menu level and Disarms, and the log names the
failure. The peer is not told: until Task 8 it finishes its rehearsal;
with Task 8 it stalls into PEER TIMEOUT.

RL-12 Setup digests. Not sent on the wire in Task 7. Each cabinet logs
one line per validated race

    arcade link: race <n> validated config <hex> plan <hex> bots <hex> bank <hex>

and the two-process gate compares them. In-race cross-cabinet comparison
is the Task 8 V4 digests.

RL-13 Pause-menu vibration toggle. While a race setup is not IDLE (a
linked race), confirming a DualShock vibration row in the pause options
(game/MAIN/MainFreeze.c:518) changes nothing: the gameMode1 write is
skipped, fixed in place with a minimal change and a comment. The
analog-controller row stays retail (ROSTER risk 9). Pausing itself stays
retail until Task 8 decides pause under lockstep; the neutral rehearsal
pads make it unreachable, and until RL-S8 the layer tap clearing on owned
frames would also block a START tap.

RL-14 Sound IDs. countSounds (game/HOWL/HOWL_OtherFX.c:3) and every ID
OtherFX_Play returns stay host-local. None of the identity-bearing
modules names them: match config, select rules, message, and session, the
launch record, netplay, lobby, lockstep, bot rules, canonical codecs, the
race setup plan, facts, core, and adapter, and the MainCanonical*
encoders. An isolation test enforces it.

RL-15 One-machine proof. Two ctr_native processes on loopback (127.0.0.1
ports 7001 and 7002, cab1 and cab2), driven by an internal-only option
--arcade-link-autopilot <report path> (CTR_INTERNAL builds; rejected with
replay options like the other arcade options): START on the attract
screen, CROSS to confirm each select item, REMATCH after race 1, EXIT
after race 2, then exit with a result code.
tools/arcade-link-launch-check.ps1, run by a new ctest arcade_link_launch
(Windows, label "live", skip 77 without assets/ctr-u.bin, a display, the
internal option, or a known build identity), starts both in parallel with
Start-Process as tools/arcade-roster-proof-check.ps1 does and requires:

- both processes exit 0;
- each report shows two races VALIDATED;
- the agreed-match line and the config, plan, bots, and bank digests of
  race k are equal across the two processes;
- the config digest of race 2 differs from that of race 1 (the rematch
  goes through a new select).

The existing in-test two-process harness
(tests/native_lockstep_peer_link_process_test.c) proves the transport;
this gate proves the game-level launch. The asymmetric cases are proven
deterministically by the netplay loopback tests (RL-S5), not live.

## 5. Constraints

- The topology lease is untouched. No lease owner in checkpoints, replay,
  or canonical state; no retire hook on LOAD_Hub_ReadFile.
- No canonical-state schema or replay-format change.
- NativeMatchConfigV1, the handshake, the bundle, and the select record
  are unchanged; the launch record is new and versioned.
- The lockstep and failure-handling isolation rules on game/ stay as
  strict.
- No heap; portable C17 with compiler extensions off.
- Every seam is unit-tested, and every structural rule has an isolation
  test.
- Default boot is unchanged.
- Done means the MSVC build plus the full ctest suite.
- Commit on `arcade` only, no push. Anything touching identity, the wire,
  replay, canonical state, or the setup seam is reviewed.

## 6. Task list

### RL-S1 -- this document

Status: done (this document). docs/RACE_LAUNCH_MILESTONE.md and the Task 7
pointer in docs/GAME_LOOP_UI_MILESTONE.md.

### RL-S2 -- launch record codec and agreement state

Status: planned. Review required (wire format). The pure RL-2 codec and
the RL-3 commit state. Files: include/platform/native_arcade_launch.h,
platform/native_arcade_launch.c, tests/native_arcade_launch_test.c,
tests/native_arcade_launch_isolation_test.cmake.

### RL-S3 -- sound-ID identity isolation test

Status: planned. RL-14. File:
tests/arcade_sound_identity_isolation_test.cmake.

### RL-S4 -- flow launch gate

Status: planned. Review required. RL-5: observation.launchStatus;
SELECT_RESULT phase 2 needs READY and COMMITTED. Until RL-S5 the netplay
adapter reports COMMITTED whenever the lobby is READY, so behaviour is
unchanged.

### RL-S5 -- netplay launch agreement

Status: planned. Review required. RL-1, RL-3, RL-4, RL-6: begin on relink
READY, drain aux, send with linger, report launchStatus to the flow, take
lastReadyConfig at the commit. Loopback tests: the symmetric launch; a
relink that completes on one side only (neither launches, and the
rematch agrees); a lost last record (one launches, the rematch is
REJECTED to OPPONENT LEFT); stale select records ignored.

### RL-S6 -- host API

Status: planned. The agreed-config accessor and the local race-failure
input (RL-8, RL-11), with host tests and isolation.

### RL-S7 -- race launch decision core

Status: planned. Pure, in game/MAIN, a standalone library: launch, wait
for VALIDATED, the rehearsal, return to the main menu, the Disarm point,
failure mapping, and two races in a row (RL-8..RL-11).

### RL-S8 -- live race caller and hook

Status: planned. Review required. The caller in the unity chain; race
frames ticked but not owned (the policy change of RL-8); the rehearsal
pads; the digest log; the setup allow-list extended with exactly this
caller file; hook isolation updates.

### RL-S9 -- pause-menu vibration guard

Status: planned. Review required. RL-13, with an isolation pin.

### RL-S10 -- two-process live gate

Status: planned. Review required. RL-15: the autopilot option, the
checker script, the ctest.

### RL-S11 -- docs close-out

Status: planned. This document, GAME_LOOP_UI Task 7 and risks,
MATCH_SELECT risks 7 and 12, ROSTER RS-10 and risks 8, 11, and 14, and
docs/HANDOFF.md sections other than "Next work".

## 7. Risks and open questions

1. Two generals (RL-7). A commit on one side with every record the other
   way lost launches one cabinet alone. Fail-safe, not prevented.
2. Level loads delay link ticks. A race-track load is staged over
   rendered frames and the hook ticks the host on them, but ticks pause
   inside each synchronous file read (platform/native_cd.c:380-392) and
   on the few loop passes that break before rendering (the VLC wait, the
   load finish, world init). This delays launch records briefly rather
   than stopping them (the launch linger now, and a Task 8 concern).
   LOAD_Hub_SwapNow (game/LOAD/LOAD_Hub.c:38-43) is the adventure-hub
   swap and not on the race launch path.
3. The tap clearing on owned frames must not reach race frames (RL-8,
   RL-S8; also a Task 8 input concern).
4. Host timing still feeds the simulation (ROSTER risk 7, Task 8).
5. Pause under lockstep is undecided (Task 8).
6. The rehearsal RESULTS says RACE COMPLETE for an undriven race. This is
   interim and development only; HANDOFF steps 6-7 need Task 8.
7. The live gate needs a build made from a clean tree (GAME_LOOP_UI risk
   5), so it skips on a dirty-tree build.
8. Stale bundles after a rematch (GAME_LOOP_UI risk 2) stay a Task 8 test
   item.
9. The ONE_CAB lobby and UI flow is a follow-up (ROSTER risk 14).
