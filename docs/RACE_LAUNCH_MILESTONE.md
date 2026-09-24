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
  inbox only then (the routing rule at :253-263; the inbox field comment
  is :123-127) and SendAux requires it (:299-306). Close empties the inbox
  (:345-353).
- SELECT_RESULT phase 2 moves to RACING with START_RACE on the first
  lobby READY after RELINK (platform/native_arcade_flow.c:236-276), and
  the netplay adapter arms its race on that action
  (platform/native_arcade_netplay.c:617-619).
- lastReadyConfig, the rematch source, is taken at the first handshake
  READY of every lobby (platform/native_arcade_netplay.c:538-547).
- The flow leaves RACING only on a link failure, LOST, or raceFinished
  (platform/native_arcade_flow.c:278-301). Before Task 8 nothing else
  bounds RACING.
- Frame ownership: in LINK mode the layer owns every frame while a link
  screen is active, on any level (game/MAIN/MainArcadeLinkPolicy.c:88-92;
  RACING counts as active, platform/native_arcade_link_host.c:248-259).
  The hook (game/MAIN/MainFrame_RenderFrame.c:80) would therefore already
  tick the host on race frames: MainArcadeLink_LinkTick runs only on owned
  frames (game/MAIN/MainArcadeLink.c:378-398). On those frames it would
  also clear every pad tap and hide the box (:386-393), reset the demo
  countdown (:405-410), and return 1 (:413), so the render frame clears
  the collected menu input with RECTMENU_ClearInput
  (MainFrame_RenderFrame.c:96-101). The layout draws nothing on RACING
  (game/MAIN/MainArcadeLinkLayout.c:874).
  MainArcadeLinkPolicy.c:51 is inside MainArcadeLinkPolicy_TitleMenuReady
  (the title-window test), not the ownership rule.
- Level loads: a race-track load is staged over rendered frames
  (game/MAIN/MainMain.c:306-336; LOAD_NextQueuedFile once per pass at
  :136), and the hook ticks the host on them. Link ticks pause only inside
  each synchronous file read (platform/native_cd.c:380-392) and on the few
  loop passes that break before rendering (the VLC wait at
  MainMain.c:234-241, which only a main-menu or scrapbook load enters,
  :314-320, so only on the return load; the load finish at :323-330;
  world init).
- The reference launcher is the roster proof
  (game/MAIN/MainArcadeRosterProof.c:227-311): launch windows, Arm,
  Launch, and leaving the title. Only the setup adapter
  (game/MAIN/MainArcadeRaceSetup.c) and the proof may name
  MainArcadeRaceSetup_Arm, _Launch, and _Disarm
  (tests/main_arcade_race_setup_isolation_test.cmake:257-263), and the
  setup adapter never calls _Disarm (:268-271). Nothing calls Disarm
  today.
- Loading any level runs MainInit_FinalizeInit, which calls
  MainArcadeRaceSetup_OnFinalizeInitBegin unconditionally
  (game/MAIN/MainInit.c:425). The setup core latches FAILED/STATE when
  that hook runs while SEEDED or VALIDATED, whatever the level
  (game/MAIN/MainArcadeRaceSetupCore.c:250-255; header rule
  game/MAIN/MainArcadeRaceSetup.h:63-69). Disarm restores the vibration
  bits only on the idle main-menu level (MainArcadeRaceSetup.h:131-140).
- Installed pads persist. Platform_InputInstallPadSnapshots sets a flag
  that stays on (platform/native_input.c:1182); while it is on,
  Platform_InputUpdate replays the installed pads and skips SDL and the
  keyboard (:1040-1046) until Platform_InputClearInstalledPadSnapshots
  (:1187-1190). The roster proof never clears, because it exits, and it
  installs from a CTR_INTERNAL-only BeginFrame hook
  (game/MAIN/MainMain.c:396-399, inside the internal block at :362-405)
  that a shipping caller cannot use.
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
   names exactly one new caller file (the race caller, RL-S8b), and the
   setup adapter still never calls Disarm.
8. Default boot (no arcade-link option) is unchanged; the full ctest
   suite passes; and the new live two-process gate arcade_link_launch
   (RL-15) has a recorded, non-skipped PASS from a build made from a clean
   tree. A skip (77) does not count.

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

The encoded size equals NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES. The codec
module includes no lockstep header, so the netplay adapter static-asserts
it in RL-S5, as it does for the select record
(platform/native_arcade_netplay.c:20-21).

RL-3 Commit rule. A cabinet commits on the first valid record from the
other cabinet role whose configDigest equals the digest of its own relink
proposal. There is no third phase. Because a peer only sends while its
link is RUNNING, a commit proves both handshakes completed on the same
config. A record with another magic (for example a late select record) is
ignored; a malformed record, an own-role echo, or a different digest is
ignored and counted, never a failure.

Agreement lifecycle. The netplay adapter holds one agreement state and
resets it on RELINK, RESTART_LOBBY, CLOSE_LINK, BEGIN_SELECT,
BEGIN_REMATCH, RETURN_TO_TITLE, and Enter. In Tick it drains the aux
inbox before the flow runs, where DriveSelect reads it today
(platform/native_arcade_netplay.c:591-594). It commits only while the
flow is in SELECT_RESULT phase 2; after the commit it only watches for
HEARD. HEARD stays latched once seen. sequence is diagnostic only: no
rule reads it, so a reordered or duplicated record changes nothing. A
commit can never follow a LINK ERROR: every pre-race LINK ERROR returns
CLOSE_LINK, which closes the link, and Close empties the aux inbox
(include/platform/native_lockstep_peer_link.h:345-353).

RL-4 Send and linger. One record per tick from the tick the relink lobby
is READY. After committing, HEARD is set, and the cabinet keeps sending
until it has sent at least one HEARD record and received a HEARD record
from the peer, or until the link closes. The linger is capped at
launchLingerTicks = 300 (10 s) local ticks after the commit. A commit
follows the peer's relink READY, so the cap covers the peer's remaining
launch window when the peer's ticks run no slower than ours. Native ticks
only ever run slow (catch-up VBlanks, ROSTER risk 7), so a slower peer
may still be pending when the linger ends; that is RL-7. Sending
continues on RACING, where the hook ticks the host every frame (RL-8).
A level load delays records briefly rather than stopping them: ticks
pause only inside each synchronous file read and on the few loop passes
that break before rendering (the VLC wait on the return load, the load
finish, world init), and the peer's records wait in the socket buffer
(section 7).

RL-5 Timeout and flow gate. No new timer: the flow's launchTimeoutTicks
(300, SEL-9), counted from RELINK, covers the relink handshake plus the
agreement; on expiry the flow shows LINK ERROR with CLOSE_LINK, as today.
The flow observation gains launchStatus (PENDING 0, COMMITTED 1) in the
first reserved byte (offset 10). The observation validation rejects a
launchStatus above COMMITTED (it checks no reserved byte today,
platform/native_arcade_flow.c:111-137), and an invalid observation is
ignored, as today (:403-406). SELECT_RESULT phase 2 moves to RACING with
START_RACE only on READY with COMMITTED, and that check comes before the
timeout check on the same tick. READY with PENDING waits, still under
launchTimeoutTicks. Three comments become false or incomplete: the
phase-2 order in include/platform/native_arcade_flow.h:33-42 (fixed by
RL-S4); "START_RACE only follows READY of the relink" in
include/platform/native_arcade_netplay.h:389-397; and "the flow only
reaches RACING on READY of the relink" above
NativeArcadeNetplay_AgreedConfig in platform/native_arcade_netplay.c:741-750
(both fixed by RL-S5).

RL-6 Rematch source. For a relink lobby, lastReadyConfig is taken in
ArmRace, on the START_RACE the commit gates, and step 2a of Tick
(platform/native_arcade_netplay.c:538-547) is skipped for relink lobbies.
The first lobby and rematch lobbies keep the handshake-READY rule of step
2a. A relink that completed on one side only therefore leaves both
cabinets on the select base, and their rematch proposals agree.

RL-7 Residual asymmetry (two generals, accepted). One cabinet launches
alone when it commits but no record from it reaches the peer before the
peer's own launch timeout: the records are lost, delivered late, or the
commit lands at the timeout edge (skewed RELINK ticks or slower peer
ticks, RL-4). This is fail-safe. Until Task 8 the lone racer finishes the
rehearsal and shows RACE COMPLETE; with Task 8 it ends in PEER TIMEOUT
once the stall timeout runs. The other cabinet shows LINK ERROR. A
rematch between them fails, traced as follows: each proposes from its
lastReadyConfig (platform/native_arcade_netplay.c:229-250), which is the
relink config on the lone racer (RL-6) and the select base on the other;
the handshake rejects the differing proposal as CONFIG_MISMATCH
(platform/native_lockstep_handshake.c:328-343, :382-387); the lobby
reports REJECTED (platform/native_lobby_state.c:128-132); and REMATCH_WAIT
moves to EXIT with OPPONENT LEFT on REJECTED or on its own wait timeout
(platform/native_arcade_flow.c:363-370), then holds and returns to the
title (flow.c:380-392). So both show OPPONENT LEFT. No race ever runs on
a config the peer did not hold.

RL-8 Launch point, waits, and race frames. Every networked launch starts
from the title launch window on the idle main-menu level, the same TITLE
window the roster proof uses: MainArcadeLink_TitleMenuReady
(game/MAIN/MainArcadeLink.c:339-349, the wrapper the roster proof calls
at game/MAIN/MainArcadeRosterProof.c:269) around
MainArcadeLinkPolicy_TitleMenuReady. The window needs mainMenuState
MAIN_MENU_TITLE (MainArcadeLink.c:332) and is closed during a load and
during the title intro before frame 230 (TITLE_INTRO_MENU_READY_FRAME,
include/ovr_230.h:48), so after race 1 it opens only once the return load
and the intro are done.

Bounded waits. RACING has no bound before Task 8 (section 2), so the race
caller bounds its own waits, as the proof does
(MainArcadeRosterProof.c:373-385 for the window, :421 for VALIDATED,
:435 for race tick 0):

- On START_RACE the caller holds the launch until the TITLE window opens,
  at most launchWindowTimeoutTicks = 900 (30 s). For race 2 the window
  must cover the rest of the return load plus the title intro up to frame
  230 (TITLE_INTRO_MENU_READY_FRAME, include/ovr_230.h:48), and the flow
  can reach the next START_RACE soon after RESULTS: its holds are 30 to
  90 ticks (the flow defaults, include/platform/native_arcade_flow.h:64-72).
  At 300 the intro alone would take 230 ticks, leaving 70 for the load;
  900 leaves 670. The roster proof waits up to 3600 for its window
  (NATIVE_ARCADE_ROSTER_PROOF_LAUNCH_WAIT_TIMEOUT_TICKS,
  include/platform/native_arcade_roster_proof.h:216).
- From Launch it waits at most launchValidateTimeoutTicks = 1800 for
  VALIDATED, and the same bound again from VALIDATED for race tick 0.
  1800 is the proof's own bound
  (NATIVE_ARCADE_ROSTER_PROOF_VALIDATE_TIMEOUT_TICKS and
  _RACE_TICK_TIMEOUT_TICKS,
  include/platform/native_arcade_roster_proof.h:217-218): a race-track
  load runs over many frames and pauses inside each file read, so 300
  would be too tight. The second bound is loose by design: VALIDATED is
  reached inside MainInit_FinalizeInit (game/MAIN/MainInit.c:486), a few
  frames before race tick 0, so it only catches a stall.
- Any expiry is an RL-11 local failure.

Race tick 0 is the first frame after VALIDATED on the plan's level with no
load in progress (Loading.stage LOAD_IDLE) and the LOADING bit clear.
VALIDATED alone is not enough: after the RL-9 change it is also visible on
the idle main-menu level after the race, until Disarm. The proof's
definition (the first frame the DRIVERS extraction succeeds,
MainArcadeRosterProof.c:579-591) needs CTR_INTERNAL canonical
extraction, which the shipping caller does not have.

Launch. On the window the caller arms MainArcadeRaceSetup with the agreed
config (a new host accessor copies the exact
NativeArcadeNetplay_AgreedConfig bytes), launches, and leaves the title.
LeaveTitle is duplicated, not shared: the caller carries its own copy of
the steps of MainArcadeRosterProof_LeaveTitle
(MainArcadeRosterProof.c:242-255), so the CTR_INTERNAL proof and its live
gate stay untouched, and the RL-S8b hook-isolation test pins that the two
bodies make the same calls in the same order.

Return. After the race, finished or failed, the caller loads the
main-menu level the way the link's return to title does
(MainArcadeLink.c:300-306): boolDemoMode 0, numPlyrNextGame 1,
mainMenuState MAIN_MENU_TITLE, then MainRaceTrack_RequestLoad of the
main-menu level. The cabinet shows RESULTS, rematch, and select there, as
the layer does today. RL-10 fixes the frame of the return and of the pad
clear.

A link failure or LOST can take the flow out of RACING during the staged
race-track load (platform/native_arcade_flow.c:282-293).
MainRaceTrack_RequestLoad overwrites Loading.stage without checking it
(game/MAIN/MainRaceTrack.c:27), and the link's return checks only levelID
(MainArcadeLink.c:300), which LOAD_LevelFile has already set to the race
level (game/LOAD/LOAD_Level.c:43). So the caller runs the return step
(numPlyrNextGame 1 and the request) only when Loading.stage is LOAD_IDLE
or LOAD_REQUESTED; otherwise it defers the step to the first LOAD_IDLE
frame. The race level then initializes and the setup ends VALIDATED or
FAILED before the return load runs. Deferring numPlyrNextGame too keeps
the race load's own read of it (game/LOAD/LOAD_TenStages.c:102) intact.
RL-S7 tests a failure during the race load.

Race frames are ticked, not owned. MainArcadeLink_LinkTick runs only on
owned frames (MainArcadeLink.c:378-398), so the policy gains one input
(the host flow is on RACING) and one output, tickOnly, each in a reserved
byte of its struct. On a LINK frame with the flow on RACING and not on
the idle main-menu level (a load in progress, or another level),
MainArcadeLinkPolicy_Decide sets tickOnly and heldButtons only; owns,
enterPressed, hideBox, restoreBox, resetDemoCountdown, and clearTaps stay
0. MainArcadeLink_Frame then runs MainArcadeLink_LinkTick and nothing
else, and returns 0. So no pad tap is cleared (:386-389), the box is not
hidden (:390-393), the demo countdown is not reset (:405-410), and the
render frame does not call RECTMENU_ClearInput
(game/MAIN/MainFrame_RenderFrame.c:96-101). Retail race input is
untouched (the rehearsal pads now, the Task 8 lockstep pads later). An
RL-8 policy unit test pins tickOnly next to the ownership cases
(game/MAIN/MainArcadeLinkPolicy.c:88-106). Link ticks pause only inside
each synchronous file read and on the few loop passes that break before
rendering (section 7).

RL-9 Setup lifecycle. Arm only from IDLE. The race caller calls Disarm
exactly once per race, always deferred to the first idle main-menu frame
after the race (the main-menu level, no load in progress, LOADING clear),
aborts included, so RS-15 restores the vibration bits there
(game/MAIN/MainArcadeRaceSetup.h:131-140). The one exception is an Arm or
Launch failure at the title: the caller Disarms at once. That is safe
because a failed Launch wrote no field: fieldsWritten is set only on
success (game/MAIN/MainArcadeRaceSetupCore.c:214), and Disarm writes only
when it is set (:423), so this Disarm writes nothing. It never Disarms
off the idle main-menu level, except just before a process exit.

The deferred Disarm needs one reviewed seam change, made in RL-S8b, the
slice that adds the caller. The return load runs MainInit_FinalizeInit,
which calls MainArcadeRaceSetup_OnFinalizeInitBegin unconditionally
(game/MAIN/MainInit.c:425), and the setup core latches FAILED/STATE when
that hook runs while SEEDED or VALIDATED, whatever the level
(game/MAIN/MainArcadeRaceSetupCore.c:250-255; header rule
MainArcadeRaceSetup.h:63-69). Every return load would therefore latch a
spurious failure. The change: OnFinalizeInitBegin while VALIDATED, when
the level being initialized (the begin view's fields.levelID) is the
main-menu level, is a no-op instead of FAILED/STATE. FAILED is already a
no-op on every level (MainArcadeRaceSetupCore.c:256-259) and stays so.
SEEDED on any level, VALIDATED on any other level, and VALIDATED with no
tracker keep today's FAILED/STATE.

The no-op needs the view. The setup adapter reads the live fields only
when MainArcadeRaceSetupCore_HookReadsView returns 1
(game/MAIN/MainArcadeRaceSetup.c:315-321), and for FINALIZE_INIT_BEGIN
that is LAUNCHED only (MainArcadeRaceSetupCore.c:226-229); otherwise
fields.levelID arrives as 0 and the no-op would never fire. So
HookReadsView(FINALIZE_INIT_BEGIN) also returns 1 in VALIDATED, and the
setup adapter fills fields.levelID there. The same slice rewrites the
header rule (MainArcadeRaceSetup.h:63-69), the core header comments
(game/MAIN/MainArcadeRaceSetupCore.h:324-325 on the begin view, and
:429-445 on HookReadsView and the pre-drivers hook), and the
HookReadsView cases in tests/main_arcade_race_setup_core_test.c. That
core test pins: VALIDATED with the main-menu level is a no-op; VALIDATED
with a race level, and VALIDATED with no tracker, still latch STATE.

An abort while LAUNCHED whose return load replaces the queued race level
(the return requested at LOAD_REQUESTED, RL-8) initializes the main-menu
level in LAUNCHED and so ends in FAILED/LEVEL_MISMATCH
(MainArcadeRaceSetupCore.c:267-271). That second failure log, after the
RL-11 one, is expected, and the RL-S7 core test or the setup core test
pins it.

A rematch is a second armed race in one process (ROSTER risk 11),
covered by the decision-core unit test and the live gate.

RL-10 Until Task 8: the launch rehearsal. A linked race loads and starts
but is not driven. From the Launch frame on, the caller installs pads
from the render hook on every race frame: pads 0 and 1 connected and
neutral, pads 2 and 3 disconnected, the proof's neutral pads
(include/platform/native_arcade_roster_proof.h:68-69), through
Platform_InputInstallPadSnapshots. Install writes the pad bus at once and
sets a flag that stays on (platform/native_input.c:1182); retail input
reads the pads from the next frame's input update, and while the flag is
on Platform_InputUpdate replays them and skips SDL and the keyboard
(:1040-1046). So no local input reaches the race, no unplugged-pad pause
fires (MainFrame_HaveAllPads), and neither cabinet drives a kart.

Unlike the proof, the caller cannot rely on exiting, so it clears the
pads with Platform_InputClearInstalledPadSnapshots (:1187-1190), but not
on the finish frame. The clear takes effect at the next VSync input poll
(game/MAIN/MainDrawCb.c:47-50), and MainFrame_GameLogic keeps running on
the race level until the LOADING bit is set (game/MAIN/MainMain.c:222-225,
:494, :499), so live local input would reach the race for a few frames: a
START tap reaches the pause check (game/MAIN/MainFrame.c:431), a missing
pad 1 pauses through MainFrame_HaveAllPads while Loading.stage is
LOAD_IDLE (MainFrame.c:554), and a surviving PAUSE_1 would fail the
rematch Launch precondition (game/MAIN/MainArcadeRaceSetupCore.c:186-189).
The rule: on the frame it reports the race finished or an RL-11 failure,
or first sees the flow leave RACING for any other reason (a link failure
or LOST), the caller sets numPlyrNextGame 1 and requests the return load
(RL-8, deferred while a race-track load runs). It clears the installed
pads on the first later frame with LOADING set, when GameLogic no longer
runs, or on the first idle main-menu frame if that comes first. Until
then RESULTS reads the neutral pads. On the normal path the clear still
lands well inside the flow's resultsDwellTicks of 30, during which
RESULTS ignores input (platform/native_arcade_flow.c:307); a return
deferred behind a race-track load only holds RESULTS on neutral pads
longer. A decision-core test and a hook-isolation pin enforce the clear
frame.

launchRehearsalTicks = 150 (5 s) after race tick 0 (RL-8) the caller
reports the race finished; the flow shows RESULTS (RACE COMPLETE) with
REMATCH and EXIT on the main-menu level. There is no input exchange and
no in-race stall or desync detection beyond the netplay adapter's
existing lobby poll, and the two cabinets are not synchronized. Task 8
replaces the rehearsal with the lockstep drive and the real finish.

RL-11 Setup failure response (closes RS-10). An Arm or Launch failure, an
RL-8 wait expiry, or a FAILED setup status before the rehearsal ends,
ends the linked race locally as LINK ERROR: the caller reports a local
race failure to the host (a new host input), the flow shows RESULTS LINK
ERROR, the cabinet returns to the main-menu level (RL-8, deferred while
a race-track load runs), the caller clears the rehearsal pads on the
RL-10 frame and Disarms on the first idle main-menu frame (RL-9), and the
log names the failure. The peer is not told: until Task 8 it finishes
its rehearsal; with Task 8 it stalls into PEER TIMEOUT.

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
pads make it unreachable. The layer's tap clearing runs after GameLogic
(the render hook, game/MAIN/MainMain.c:522, after :499) and never
blocked it.

RL-14 Sound IDs. countSounds (the sdata field,
include/regionsEXE.h:3262, incremented by CountSounds at
game/HOWL/HOWL_OtherFX.c:6) and every ID OtherFX_Play returns stay
host-local. None of the identity-bearing modules names countSounds,
CountSounds, OtherFX_Play, OtherFX_Play_LowLevel, OtherFX_RecycleNew,
rainSoundID, or OptionSlider_soundID: match config, select rules,
message, and session, the launch record, netplay, lobby, lockstep, bot
rules, canonical codecs, the race setup plan, facts, core, and setup
adapter, and the MainCanonical* encoders. An isolation test enforces it.

RL-15 One-machine proof. Two ctr_native processes on loopback (127.0.0.1
ports 7001 and 7002, cab1 and cab2), driven by an internal-only option
--arcade-link-autopilot <report path> (CTR_INTERNAL builds; rejected with
replay options like the other arcade options): START on the attract
screen, CROSS to confirm each select item, REMATCH after race 1, EXIT
after race 2, then exit with a result code. The autopilot never injects
input through installed pads, which the rehearsal owns and clears
(RL-10); it feeds the link host's own inputs instead (for example
NativeArcadeLinkHost_Enter and the held menu buttons passed to
NativeArcadeLinkHost_Tick, game/MAIN/MainArcadeLink.c:269-274).
tools/arcade-link-launch-check.ps1, run by a new ctest arcade_link_launch
(Windows, label "live", RUN_SERIAL TRUE like the two existing live tests,
CMakeLists.txt:1679 and :1705, here also because ports 7001 and 7002 are
fixed; skip 77 without assets/ctr-u.bin, a display, the internal option,
or a known build identity), starts both in parallel with Start-Process as
tools/arcade-roster-proof-check.ps1 does and requires:

- both processes exit 0;
- each report shows two races VALIDATED;
- the agreed-match line and the config, plan, bots, and bank digests of
  race k are equal across the two processes;
- the config digest of race 2 differs from that of race 1 (the rematch
  goes through a new select).

Task 7 is done only with a recorded, non-skipped PASS of this gate from a
build made from a clean tree (section 3, criterion 8).

The existing in-test two-process harness
(tests/native_lockstep_peer_link_process_test.c) proves the transport;
this gate proves the game-level launch. The asymmetric cases are proven
deterministically by the netplay loopback tests (RL-S5), not live.

Review changes. The plan review changed three defaults. RL-8 gained the
bounded waits (launchWindowTimeoutTicks, now 900 after the re-review,
and launchValidateTimeoutTicks = 1800), a race tick 0 the shipping caller
can see, and the tickOnly hook output. RL-9 now defers Disarm to the
first idle main-menu frame after every race, aborts included, and adds
the setup seam change for VALIDATED on the main-menu level. RL-10 now
installs the pads from the Launch frame and clears them before RESULTS
accepts input. The re-review of that change raised
launchWindowTimeoutTicks from 300 to 900 (race 2 must wait out the
return load and the title intro), made HookReadsView read the begin view
in VALIDATED so the RL-9 no-op can fire, moved the pad clear to the first
frame with LOADING set (or the first idle main-menu frame), and deferred
the return step while a race-track load runs. The review only clarified
RL-2 (the aux-size assert), RL-3 (the agreement lifecycle), RL-4 (what
the 300-tick linger covers), RL-5 (validation and check order), RL-6
(where lastReadyConfig is taken), RL-7 (the traced path), RL-11 (the pad
clear and the Disarm point), RL-13 (the slice name), RL-14 (the banned
list), and RL-15 (the autopilot input path, RUN_SERIAL, and the
non-skipped PASS). No other value changed.

## 5. Constraints

- The topology lease is untouched. No lease owner in checkpoints, replay,
  or canonical state; no retire hook on LOAD_Hub_ReadFile.
- The race caller, the decision core, and the RL-S2 module name no
  topology-lease acquire, activate, capture, or publish token, and no
  checkpoint, replay, or NativeCanonical token, enforced by isolation
  tests. Today such bans cover only the setup adapter and the proof
  (tests/main_arcade_race_setup_isolation_test.cmake:329-344, rule 4 of
  its header at :20-25).
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
  replay, canonical state, or the setup seam is reviewed. The one setup
  seam change in Task 7 is the RL-9 OnFinalizeInitBegin rule (VALIDATED
  on the main-menu level is a no-op, and HookReadsView reads the begin
  view in VALIDATED), in RL-S8b.

## 6. Task list

### RL-S1 -- this document

Status: done (894788d2b). docs/RACE_LAUNCH_MILESTONE.md and the Task 7
pointer in docs/GAME_LOOP_UI_MILESTONE.md. Plan reviewed; the review
changed RL-8 (bounded waits and the tickOnly hook output), RL-9 (the
Disarm point plus a setup seam change), and RL-10 (pad install and
clear), and clarified RL-2..RL-7, RL-11, and RL-13..RL-15 (section 4,
"Review changes"). Closed in the review close-out commit. Re-reviewed
after the RL-9 change (no BLOCKER); findings closed in the follow-up
commit.

### RL-S2 -- launch record codec and agreement state

Status: done (1f28628bc); reviewed, no BLOCKER; two isolation-test
should-fixes and four nits closed in the follow-up commit. Review required
(wire format). The pure RL-2 codec and the RL-3 commit state. Files: include/platform/native_arcade_launch.h,
platform/native_arcade_launch.c, tests/native_arcade_launch_test.c,
tests/native_arcade_launch_isolation_test.cmake. The isolation test
applies the section 5 token ban (lease acquire, activate, capture, and
publish; checkpoint; replay; NativeCanonical).
Built as library ctr_native_arcade_launch (links only
ctr_native_canonical_codec; not linked into ctr_native until RL-S5), with
tests native_arcade_launch_unit (frozen golden bytes, every fault cause in
check order, the agreement driven in memory) and
native_arcade_launch_isolation (the section 5 ban on comment-free code,
frozen wire constants).

### RL-S3 -- sound-ID identity isolation test

Status: done (no review required: test only). RL-14. File:
tests/arcade_sound_identity_isolation_test.cmake. The ctest
arcade_sound_identity_isolation scans the comment-free code of the 102
listed identity-bearing sources (plus any new file in their glob families)
for sound-ID identifiers, checks that every OtherFX_Play call in
game/MAIN/MainArcadeLink.c is (void)OtherFX_Play(...), and bans
MainArcadeLinkSound and OtherFX from the netplay adapter and host glue.

### RL-S4 -- flow launch gate

Status: done (b7bc0c976); reviewed, no BLOCKER or SHOULD-FIX; three nits
closed in the follow-up commit. The flow observation carries launchStatus
(enum NativeArcadeFlowLaunchStatus, PENDING 0, COMMITTED 1) at offset 10,
validated on every screen, and SELECT_RESULT phase 2 starts the race only
on READY with COMMITTED, checked before the timeout; the netplay adapter
reports COMMITTED exactly when the lobby is READY (the transitional rule
RL-S5 replaces). Files: include/platform/native_arcade_flow.h,
platform/native_arcade_flow.c, platform/native_arcade_netplay.c,
tests/native_arcade_flow_test.c.

Plan: Review required. RL-5: observation.launchStatus at
offset 10; the observation validation rejects launchStatus above
COMMITTED; SELECT_RESULT phase 2 needs READY and COMMITTED, checked
before the timeout. Fixes the phase-2 comment in
include/platform/native_arcade_flow.h:33-42. Tests in
tests/native_arcade_flow_test.c: the layout pin that holds reserved at
offset 10 (:226-230) moves to launchStatus at 10 (size still 12); READY
with PENDING waits, then times out to LINK ERROR; READY with COMMITTED on
the timeout tick starts the race; an out-of-range launchStatus is
ignored. Until RL-S5 the netplay adapter reports COMMITTED whenever the
lobby is READY, so behaviour is unchanged. For that reason RL-S8b needs
RL-S5 first: on this interim alone the caller would launch on handshake
completion.

### RL-S5 -- netplay launch agreement

Status: planned. Review required. RL-1, RL-3, RL-4, RL-6: begin on relink
READY, drain aux before the flow, send with linger, report launchStatus
to the flow, reset the agreement at the RL-3 points, take lastReadyConfig
in ArmRace for relink lobbies and skip step 2a for them. The size
static-assert of RL-2 in platform/native_arcade_netplay.c. Fixes the
AgreedConfig comments in include/platform/native_arcade_netplay.h:389-397
and platform/native_arcade_netplay.c:741-750 ("the flow only reaches
RACING on READY of the relink").
Adds ctr_native_arcade_launch to the exact link allow-list in
tests/native_arcade_netplay_isolation_test.cmake (:118-137, eight
libraries become nine). Loopback tests: the symmetric launch; a relink
that completes on one side only (neither launches, and the rematch
agrees); a lost last record (one launches, the rematch is REJECTED to
OPPONENT LEFT); stale select records ignored; a commit at the timeout
edge; no commit after CLOSE_LINK; a commit and a flow timeout on the same
tick (the race starts); a lost HEARD running the linger to the cap;
reordered and duplicated records; each reset point of RL-3; RESTART_LOBBY
during phase 2. The record carries no link epoch, so a stale record with
the same digest buffered across a reset would commit the new agreement;
safety rests on RL-S5's reset points plus Close emptying the aux inbox,
and the loopback tests cover a stale record arriving across each reset
point.

### RL-S6 -- host API

Status: planned. The agreed-config accessor and the local race-failure
input (RL-8, RL-11), and a racing query for the RL-8 policy input (the
host's link screen is private today,
platform/native_arcade_link_host.c:159), with host tests and isolation.
The host header include allow-list
(tests/native_arcade_link_host_isolation_test.cmake:88-89) excludes
native_match_config.h, so the header forward-declares struct
NativeMatchConfigV1 and the allow-list is unchanged.

### RL-S7 -- race launch decision core

Status: planned. Pure, in game/MAIN, a standalone library: launch, the
bounded waits, race tick 0, the rehearsal, return to the main menu, the
Disarm point, failure mapping, and two races in a row (RL-8..RL-11).
Tests: a core unit test with the return and pad cases (install from the
Launch frame; on the finish, on every RL-11 path, and when the flow
leaves RACING otherwise, numPlyrNextGame 1 and the return request on
that frame, and the clear only on the first later frame with LOADING set
or the first idle main-menu frame; a failure during the race-track load
defers the return step to the first LOAD_IDLE frame), the bounded-wait
expiries, race tick 0 only on the plan's level, and the Disarm point
(deferred to the idle main-menu frame; at once on an Arm or Launch
failure at the title); a purity isolation test with the section 5 token
ban.

### RL-S8a -- race frames ticked, not owned

Status: planned. Review required. Needs RL-S6 (the RACING input reads
the host racing query; the host's link screen is private today,
platform/native_arcade_link_host.c:159). The RL-8 policy change: the
RACING input and the tickOnly output of MainArcadeLinkPolicy, and
MainArcadeLink_Frame ticking the host and returning 0 on tickOnly
frames. Tests: the RL-8 policy unit test in
tests/main_arcade_link_policy_test.c, including the struct layout pins,
and hook isolation updates in
tests/main_arcade_link_hook_isolation_test.cmake. Safe before RL-S8b: no
race reaches RACING off the main-menu level until then.

### RL-S8b -- live race caller

Status: planned. Review required. Needs RL-S5, RL-S6, RL-S7, and
RL-S8a. The
caller in the unity chain, right after MAIN/MainArcadeLink.c and before
MAIN/MainArcadeRosterProof.c (game/game_unity.h:271-276), so it follows
the 230 title (:261) and MainArcadeRaceSetup.c (:128) it calls and keeps
the proof after the arcade-link hook. The rehearsal pads; the duplicated
LeaveTitle; the digest log; the RL-9 setup seam change (the VALIDATED
main-menu no-op and HookReadsView in VALIDATED) with the core header
comments and its core unit test; the setup allow-list extended with
exactly this caller file; hook
isolation updates, including the pad clear pin and the LeaveTitle pin;
the section 5 token ban on the caller.

### RL-S9 -- pause-menu vibration guard

Status: planned. Review required. RL-13, with an isolation pin.

### RL-S10 -- two-process live gate

Status: planned. Review required. RL-15: the autopilot option, the
checker script, the ctest (RUN_SERIAL). Tests: an option parser unit
test, and an isolation pin that main.c rejects the option together with
replay options.

### RL-S11 -- docs close-out

Status: planned. This document, GAME_LOOP_UI Task 7 and risks,
MATCH_SELECT risks 7 and 12, ROSTER RS-10 and risks 8, 11, and 14, and
docs/HANDOFF.md sections other than "Next work". This includes the stale
"Task 7 is not started" text in docs/GAME_LOOP_UI_MILESTONE.md:1079 and
docs/HANDOFF.md:51.

## 7. Risks and open questions

1. Two generals (RL-7). A commit on one side with no record reaching the
   other side before its launch timeout (lost, late, or at the timeout
   edge, including a peer whose ticks run slower) launches one cabinet
   alone. Fail-safe, not prevented.
2. Level loads delay link ticks. A race-track load is staged over
   rendered frames and the hook ticks the host on them, but ticks pause
   inside each synchronous file read (platform/native_cd.c:380-392) and
   on the few loop passes that break before rendering (the VLC wait on
   the return load, the load finish, world init). This delays launch
   records briefly rather than stopping them (the launch linger now, and
   a Task 8 concern). LOAD_Hub_SwapNow (game/LOAD/LOAD_Hub.c:38-43) is
   the adventure-hub swap and not on the race launch path.
3. The tap clearing on owned frames must not reach race frames (RL-8,
   RL-S8a; also a Task 8 input concern).
4. Host timing still feeds the simulation (ROSTER risk 7, Task 8).
5. Pause under lockstep is undecided (Task 8).
6. The rehearsal RESULTS says RACE COMPLETE for an undriven race. This is
   interim and development only; HANDOFF steps 6-7 need Task 8.
7. The live gate needs a build made from a clean tree (GAME_LOOP_UI risk
   5), so it skips (77) on a dirty-tree build. A skip does not count:
   Task 7 done needs a recorded, non-skipped PASS (section 3, criterion
   8).
8. Stale bundles after a rematch (GAME_LOOP_UI risk 2) stay a Task 8 test
   item.
9. The ONE_CAB lobby and UI flow is a follow-up (ROSTER risk 14).
