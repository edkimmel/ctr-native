# Lockstep race milestone

Design-and-status record for docs/GAME_LOOP_UI_MILESTONE.md Task 8,
"in-race lockstep drive and failure handling" (docs/HANDOFF.md "Next work"
item 1), on branch `arcade`. Read AGENTS.md and docs/HANDOFF.md first. This
document follows the pattern of docs/RACE_LAUNCH_MILESTONE.md: a
prospective plan with a task list, updated to record status as slices
land.

## 1. Scope

In scope (HANDOFF "Next work" item 1):

- Replace the neutral-pad launch rehearsal (RACE_LAUNCH RL-10) with
  lockstep-driven input: each cabinet samples its own player, the samples
  are exchanged as lockstep bundles, and both cabinets install the same
  committed pads on the same race tick.
- Make VBlanks per tick deterministic in a linked race, so host timing no
  longer feeds gGT->elapsedTimeMS and gGT->frameTimer_Confetti (ROSTER
  risk 7).
- Compare race-relative control: sdata->frameCounter and
  gGT->frameTimer_VsyncCallback are boot-relative.
- Project the post-setup RNG bank (MainArcadeRaceSetup_Bank()) into a live
  V4 canonical state, per tick, and exchange its digests.
- Wire the in-race stall, desync, and peer-drop paths to RESULTS, each
  bounded.
- Prove all of it on one machine with a two-process live gate.

Already done: race-launch risk 10. The link's return to title and the race
caller's deferred return step both run only at LOAD_IDLE or LOAD_REQUESTED
(c12183e08, and the follow-up eb382038e; section 2).

Out of scope: rollback or prediction (docs/LOCKSTEP_MILESTONE.md forbids
both); a standings view on RESULTS (the GAME_LOOP_UI Task 8 sketch mentions
"reusing the retail standings drawing"; see risk 16); how each cabinet
presents the two-player race it simulates (the setup plan's TWO_CAB shape
is a retail two-player race, game/MAIN/MainArcadeRaceSetupPlan.c:60); a
ONE_CAB lobby (ROSTER risk 14); more than two cabinets; physical
two-cabinet and G29 validation (HANDOFF steps 6-7).

## 2. Starting point

This section records the code at eb382038e; line citations are of that
commit.

- A linked race is the RL-10 rehearsal. The race caller installs the
  roster proof's neutral pads (pads 0 and 1 connected and neutral, pads 2
  and 3 disconnected) on every frame from the Launch frame until the clear
  (game/MAIN/MainArcadeRaceLaunch.c:182-207, :345-357), and the launch core
  reports the race finished 150 frames after race tick 0
  (game/MAIN/MainArcadeRaceLaunchCore.h:74-76, :163). Race tick 0 is the
  first frame after the frame the core first sees VALIDATED that is on the
  plan's level with the loading stage IDLE and the LOADING bit clear, and
  never the first-seen frame itself (MainArcadeRaceLaunchCore.h:63-68). The
  core's phases are MainArcadeRaceLaunchCore.h:193-200.
- Race-launch risk 10 is fixed. The link's return step runs only at
  LOAD_IDLE or LOAD_REQUESTED, and an owed one is retried on every later
  frame (game/MAIN/MainArcadeLink.c:313-319, :424). The caller's owed step
  also runs at LOAD_REQUESTED (game/MAIN/MainArcadeRaceLaunchCore.c:141-152).
  tests/main_arcade_link_return_interleave_test.c pins one main-menu load.

### 2.1 Frame order

One pass of the main loop in state 3 (game/MAIN/MainMain.c) is one retail
game tick:

1. LOAD_NextQueuedFile (:136).
2. The load block (:220-337). While a load runs it pins
   gGT->elapsedTimeMS to 32 (:231) and sets LOADING once the flag covers
   the screen (:222-225).
3. The traffic-lights countdown. It subtracts the previous tick's
   elapsedTimeMS (:341-356).
4. sdata->frameCounter++ (:359).
5. The CTR_INTERNAL frame hooks, the replay scheduler and the roster
   proof's pad install (:362-405). A shipping path cannot use them.
6. GAMEPAD_ProcessAnyoneVars (:406). It turns the gamepad buffers that the
   previous pass's VBlanks filled into this tick's held and tapped
   buttons.
7. MainFrame_ResetDB (:435), then MainFrame_GameLogic unless LOADING is set
   (:494-503). GameLogic computes this tick's elapsedTimeMS
   (game/MAIN/MainFrame.c:188-203) and runs the pause check
   (MainFrame.c:424-440).
8. MainFrame_RenderFrame (MainMain.c:522). In order, it calls:
   - the arcade-link hook, which ticks the host and the flow
     (game/MAIN/MainFrame_RenderFrame.c:81);
   - the race caller (:90);
   - the roster proof's frame (:103);
   - the retail menu collect and process (:106-123);
   - PickupBots_Update (:234) and PlayLevel_UpdateLapStats (:243), both
     simulation, and both after the hook;
   - RefreshCard_Entry while END_OF_RACE is set (:254-259);
   - RenderVSYNC (:279);
   - RenderSubmit (:286).
9. Platform_EndFrame presents (MainMain.c:527).

RenderVSYNC is the only VBlank source during an arcade race. It calls
VSync(0) once when the checkered flag is drawn (MainFrame_RenderFrame.c:1285).
Then it loops DrawSync and VSync(0) (:1296, :1308) until ReadyToFlip
(:1256-1264), which needs sdata->vsyncTillFlip below 1. RenderSubmit sets
vsyncTillFlip to 2 (:1349). So every pass asks for exactly 2 VBlanks. The
other game VSync callers are load, boot, error, and scrapbook paths.

Each emitted VBlank (platform/native_platform.c:900-911) does this, in
order:

1. It advances the native root counter by 263 units
   (platform/native_libapi.c:15-23).
2. It runs the VSync callback, game/MAIN/MainDrawCb.c:17-53:
   - frameTimer_VsyncCallback++ (:22);
   - frameTimer_Confetti++ unless PAUSE_ALL (:23-26);
   - vsyncTillFlip-- (:28);
   - it adds the root counter to sdata->rcntTotalUnits (:32-33);
   - the retail sound update (:38-42);
   - Platform_PollInput (:47);
   - GAMEPAD_PollVsync (:50).
3. It steps native audio (native_platform.c:909).

### 2.2 Pacing and time

- Default pacing is catch-up. VSync first emits every VBlank that fell due
  while the host was busy, up to 8, and only then waits for its own
  (native_platform.c:913-970, :980-1017). A late host frame therefore adds
  VBlanks to that tick: it moves elapsedTimeMS, frameTimer_Confetti, and
  frameTimer_VsyncCallback (ROSTER risk 7, docs/ROSTER_MILESTONE.md:978-990).
- Fixed pacing never emits a late VBlank. It re-anchors the schedule
  instead (native_platform.c:931-943; the pure plan is
  platform/native_vblank_pacing.c:5-20). It exists only in CTR_INTERNAL
  builds: the setter is declared inside #if defined(CTR_INTERNAL) at
  include/platform.h:32-56 and defined inside it at
  native_platform.c:1024-1035. The only caller is main.c, and only for the
  roster proof (main.c:509). tests/native_vblank_pacing_isolation_test.cmake:4-9
  pins all of that (RS-18 calls it proof-only).
- elapsedTimeMS is derived from VBlanks, not from wall time.
  Timer_GetTime_Total converts sdata->rcntTotalUnits to milliseconds as
  (units * 1000) / 0x147e in 32-bit signed arithmetic (game/Timer.c:31-43).
  Timer_GetTime_Elapsed adds 0xc7e18 when the value goes backwards
  (Timer.c:48-64). MainFrame.c:188-203 then scales the result by 32/100 and
  clamps it to 64. A pause gives 32. Two VBlanks per tick give 32. The
  first GameLogic after a load sees the whole load and clamps to 64.
- The arithmetic wraps. That makes the per-tick delta depend on the
  absolute, boot-relative rcntTotalUnits (include/regionsEXE.h:3720; its
  only writer is MainDrawCb.c:32). A simulation of 2 VBlanks per tick
  through Timer.c's arithmetic gives a delta of 99 ms, so elapsedTimeMS 31,
  at the tick where units * 1000 crosses zero from below. That happens once
  every 2^32 / 1000 units, about 16,331 VBlanks or 273 s of uptime, for
  most counter phases. Two cabinets never share a boot history, so that
  tick differs between them.
- The RS-17 audit says gGT->clockFrameStart is harmless because "only
  their deltas matter" (game/MAIN/MainArcadeRaceSetupCore.h:178-181). That
  misses the wrap (LR-8). The roster proof launches about 5,400 to 6,600
  proof ticks after boot (include/platform/native_arcade_roster_proof.h:56-60)
  and races 900 ticks, so its races appear to end just before the first
  such crossing. That would explain why the live proof never met it.
- The setup pins gGT->timer and gGT->frameTimer_Confetti to 0 at race init
  (RS-17). The pin is written only by OnFinalizeInitBegin in LAUNCHED
  (MainArcadeRaceSetupCore.h:191-194), which runs at game/MAIN/MainInit.c:425.
  sdata->frameCounter and frameTimer_VsyncCallback stay boot-relative
  (MainArcadeRaceSetupCore.h:155-168):
  - frameCounter has no race-simulation reader.
  - frameTimer_VsyncCallback's readers are the level-audio distortion
    (game/HOWL/HOWL_LevelAudio.c:52), frameTimer_notPaused (written at
    MainFrame_RenderFrame.c:1384 and never read), and the load queue's VRAM
    delay (game/LOAD/LOAD_Queue.c:77).

### 2.3 Input

- Installed pads persist. Platform_InputInstallPadSnapshots stores the four
  snapshots, sets a flag that stays on, and writes the pad bus at once
  (platform/native_input.c:1170-1185). While the flag is on,
  Platform_InputUpdate replays the installed pads and skips SDL, the
  controllers, the G29, and the keyboard (:1033-1064, the branch at
  :1040-1046). Platform_InputClearInstalledPadSnapshots only drops the flag
  (:1187-1190). With installed pads active, nothing reads the local device
  today.
- The snapshot layout is struct PlatformInputPadSnapshot
  (include/platform/native_input.h:8-16). The same bytes, without the
  reserved tail, are struct NativeCanonicalInputPadV1
  (include/platform/native_canonical_state.h:43-50). Buttons are active-low:
  a reset or disconnected snapshot holds 0xffff
  (native_input.c:137-150, :176-192). By default host controller slot 0 is
  the connected one (:141).
- Pause:
  - The pause check pauses on a START tap from any human pad, or, natively,
    when MainFrame_HaveAllPads fails (MainFrame.c:424-440). The pause is
    MainFreeze_IfPressStart, which sets PAUSE_1 (game/MAIN/MainFreeze.c:1162,
    :1225).
  - MainFrame_HaveAllPads reads the controller packets on the pad bus while
    the load stage is IDLE (MainFrame.c:551-575).
  - PAUSE_1 is in gameMode1, which is canonical control state.

### 2.4 Lockstep, failure handling, and the flow

- The relink handshake opens the lockstep session itself, with the
  configured input delay (platform/native_lockstep_peer_link.c:191). The
  adapter's delay is NATIVE_ARCADE_NETPLAY_DEFAULT_INPUT_DELAY 2
  (include/platform/native_arcade_netplay.h:185), validated in [1, 6]
  (platform/native_arcade_netplay.c:103-104).
- The session API is include/platform/native_lockstep_session.h:
  - Init :173, Open :192-193, SubmitLocalInput :212-213,
    RecordLocalDigests :224, ComposeBundle :242-243, AcceptBundle :284-285,
    TakeFrameInputs :299-300.
  - A STALL latches nothing and consumes nothing (:287-298).
  - The first inputDelay consumption frames consume the all-zero pad
    (platform/native_lockstep_session.c:505-512). That pad reads as
    disconnected with every button pressed.
  - A bundle carries the local pad for its frame and the verified digests
    of frame frameIndex - D - 1 (native_lockstep_session.h:226-243).
  - The digest history is D + 2 frames deep (:29-30), so a bundle for an
    old frame cannot be composed again later.
- The receive window accepts frames in [consumedFrame, consumedFrame + 8)
  (platform/native_lockstep_input_window.c:30-34):
  - a frame below the window is a STALE drop (:63-69);
  - a frame above it is a WINDOW_OVERRUN fault (:71-79);
  - a re-delivered frame must be byte-identical, or it is a
    CONFLICTING_INPUT fault (:81-95).
  A bundle from another match identity is a MATCH_IDENTITY fault
  (platform/native_lockstep_protocol.c:285-287).
- The peer link has no bundle-resend call. ComposeAndSendBundle composes a
  new bundle and sends it (include/platform/native_lockstep_peer_link.h:275-284).
  Receive errors, including a reset from a closed peer port, drop the
  datagram and draining continues (platform/native_lockstep_peer_link.c:273).
  So a dead peer is only ever a stall.
- The outcome tracker turns consecutive STALL polls into STALL_TIMEOUT. It
  checks DIVERGED and FAULTED first
  (include/platform/native_lockstep_match_outcome.h:93-122). Its own
  default is 180 frames, documented as 3 s at 60 Hz (:25-41). The adapter
  passes 90 ticks instead: 3 s at the real 30 Hz loop (UX-9,
  native_arcade_netplay.h:174-178, :189).
- NativeArcadeNetplay_OnTakeResult is the platform-only hook for the Task 8
  driver (native_arcade_netplay.h:30-38, :445-453). It feeds the tracker.
  A latched outcome drops the remote human in the roster
  (platform/native_lockstep_match_roster.c:35-71) and becomes the flow's
  link failure (platform/native_arcade_netplay.c:600-610, :821-839).
  NativeArcadeNetplay_EndReasonForCause maps each outcome to an end reason
  (native_arcade_netplay.c:975-988):
  - STALL_TIMEOUT to END_PEER_TIMEOUT;
  - DIVERGED to END_DESYNC;
  - FAULTED to END_LINK_ERROR.
  The adapter's own Tick polls the link during RACING. On a FAULTED or
  DIVERGED link it latches the real cause itself (native_arcade_netplay.c:651-655,
  :683-691).
- The flow leaves RACING on a link failure (which outranks a same-tick
  finish), on LOST (LINK ERROR), or on raceFinished (FINISHED)
  (platform/native_arcade_flow.c:278-301). The end reasons are
  include/platform/native_arcade_flow.h:104-109. RESULTS ignores input for
  resultsDwellTicks 30 (native_arcade_flow.h:66, native_arcade_flow.c:307).
  The in-race path to RESULTS keeps the link open
  (native_arcade_netplay.h:93-94).
- The RESULTS titles are in MainArcadeLinkLayout_Results
  (game/MAIN/MainArcadeLinkLayout.c:371-411):
  - RACE COMPLETE (FINISHED, :383);
  - OPPONENT DISCONNECTED (PEER_TIMEOUT, :388);
  - RACE OUT OF SYNC (DESYNC, :393);
  - LINK ERROR (:398);
  - REMATCH and EXIT rows (:407-410).
  WAITING FOR OPPONENT exists on the lobby screen (:362) and the rematch
  wait (:845). The layout draws nothing on RACING (:872-876).

### 2.5 Canonical state

- V4 is struct NativeCanonicalStateV4
  (include/platform/native_canonical_state_v4.h:25-38). It holds control,
  retail RNG, deterministic bank, input, drivers, world counters, mine
  registry, and topology. ComputeDigests is :42.
- There is no live V4 projection. The only live projection is V1,
  MainCanonicalState_ProjectLive (game/MAIN/MainMain.c:57-87). It sits
  inside the CTR_INTERNAL block that opens at :13, and only the replay
  scheduler and the roster proof use it (:536-582). Its control values
  copy the boot-relative counters (:66-77).
- The V4 runtime coordinator is dormant by design
  (game/MAIN/MainCanonicalRuntime.h:8-10). Its test says it "stays a typed,
  dormant seam until a live gate authorizes it"
  (tests/main_canonical_runtime_isolation_test.cmake:49-59). The same test
  bans the lease owner, the lease runtime, the replay V4 libraries, and
  MainMain from it (:19-47).
- MainCanonicalRuntime_PrepareV4 (game/MAIN/MainCanonicalRuntime.c:218-329):
  - It captures or validates the topology context, MainCanonicalTopology
    (game/MAIN/MainCanonicalTopology.h:37-48). This is not the topology
    lease (MainCanonicalTopologyLeaseAdapter_Capture is a separate API)
    (:270-293).
  - It extracts the complete drivers (:294-307).
  - It takes the world and topology domains from the caller (:309-314).
  - It builds a fresh bank from the config's masterSeed instead of the
    post-setup bank (:316-317).
- MainArcadeRaceSetup_Bank returns the post-setup bank while VALIDATED
  (game/MAIN/MainArcadeRaceSetup.h:135-136). Only the setup draws from the
  bank, on the MATCH_SETUP stream (game/MAIN/MainArcadeBotSetup.c:207,
  platform/native_arcade_bot_rules.c:428). It is constant for the whole
  race.
- The world extractors exist as libraries that ctr_native does not link
  (CMakeLists.txt:648, :656, :1026): MainCanonicalWorldCounters_ExtractV1
  and MainCanonicalWorldMineRegistry_ExtractV1. So does the topology facts
  reader (:664, MainCanonicalTopologyFactReader_ToV1 at
  game/MAIN/MainCanonicalTopologyFacts.h:107). No live topology reader
  exists.
- RS-13 left Physics, WORLD, TOPOLOGY, and live V4 projection to Task 8
  (docs/ROSTER_MILESTONE.md:589-591). The roster proof digests only V1
  control, RNG, and input, plus a topology-free drivers candidate whose
  Physics groups are zero.

### 2.6 Race end, loads, and builds

- In ARCADE_MODE the race ends when every human has finished
  (game/PlayLevel.c:435-437, then MainGameEnd_Initialize at :476, which
  sets END_OF_RACE at game/MAIN/MainGameEnd.c:546; END_OF_RACE is 0x200000,
  include/namespace_Main.h:28). The linked race sets ARCADE_MODE (the
  plan's gameMode1 set mask, game/MAIN/MainArcadeRaceSetupPlan.h:365).
  PlayLevel_UpdateLapStats runs after the hook (section 2.1), so the hook
  sees a tick's END_OF_RACE on the next pass. A human who never finishes
  never ends the race.
- No load runs during an arcade race. The end-of-race overlay is loaded
  with the level (game/LOAD/LOAD_TenStages.c:247). The only in-render
  overlay load is the adventure hub's (MainFrame_RenderFrame.c:656).
  Synchronous file reads, and with them paused link ticks (RACE_LAUNCH risk
  2), happen only around the race: the race-track load and the return load.
- ctr_native is always compiled with CTR_INTERNAL (CMakeLists.txt:989-991).
  The live tests are under if(WIN32) (:1785), not a CTR_INTERNAL switch.
  "Shipping" in this document means code outside CTR_INTERNAL guards. Two
  existing live gates carry the label live and RUN_SERIAL:
  - arcade_roster_determinism (:1812-1823);
  - arcade_link_launch (:1837-1847), the RL-15 two-process gate driven by
    --arcade-link-autopilot through tools/arcade-link-launch-check.ps1.
  arcade_link_launch passed in 78.16 s in a 451 s full run
  (RACE_LAUNCH RL-S10). ctest lists 145 tests today.

## 3. What "Task 8 done" means

1. A linked race is driven in lockstep. Each cabinet samples only its own
   player. Both cabinets install the same committed pads on the same race
   tick. The rehearsal (RL-10) and its 150-tick finish are gone.
2. Every simulation input per tick is identical on both cabinets
   (section 4.1). That includes the VBlanks per tick, elapsedTimeMS,
   frameTimer_Confetti, and the root counter.
3. Each race tick projects a live V4 state. It carries race-relative
   control and the post-setup bank. Its digests are recorded, exchanged in
   the bundles, and compared, and a mismatch ends the race as DESYNC.
4. A stall holds the simulation with no VBlank emitted. The player sees
   WAITING FOR OPPONENT after a short grace, and the race resumes when
   input arrives. Every failure path is bounded and ends on RESULTS, then
   the return load (LR-12):
   - stall timeout;
   - a peer that never starts;
   - peer drop;
   - desync;
   - protocol fault;
   - local failure;
   - the race-length bound.
5. The race ends on the retail finish (END_OF_RACE) on the same race tick
   on both cabinets, and RESULTS shows RACE COMPLETE.
6. Pause cannot happen in a linked race, and no pad reads as unplugged.
7. Default boot and replay are unchanged. Nothing new enters checkpoints,
   replay, or canonical-state formats, and the topology lease is untouched.
8. The full ctest suite passes. The extended live gate (LR-16) has a
   recorded, non-skipped PASS from a build made from a clean tree. A skip
   (77) does not count.

How each will be proven:

1. The drive core unit test (LR-S6) and the extended live gate: both
   processes log equal per-tick input digests and reach the finish on the
   same tick.
2. Two pieces:
   - arcade_roster_determinism: its runs still agree tick by tick with the
     root-counter pin and V4 in place (LR-S3, LR-S4).
   - The live gate: it compares every per-tick V4 digest across the two
     processes, including the race-relative control.
3. The live gate's forced-desync race ends in RACE OUT OF SYNC with the
   divergence frame named. The per-tick V4 lines are equal in the races
   without a fault.
4. The live gate's injected freeze, the peer kill, and the drive core
   tests of every bound. The hold spike (LR-S2) shows no extra VBlank
   across a hold.
5. The live gate's race 1.
6. The drive core test of the START mask and of pad normalization, and an
   isolation pin that committed pads are installed only through the
   normalizing mapping.
7. Isolation tests (section 5) and the unchanged default-boot pins
   (main_arcade_link_hook_isolation).
8. The gate's recorded run in LR-S10.

## 4. Decided design (defaults LR-1..LR-16)

Each default below is a default pending owner review.

LR-1 Placement. The race driver lives under platform/, because game code
may not name lockstep (tests/native_lockstep_isolation_test.cmake:128-157
bans the word in every game/ source, comments included). It has two parts:

- A pure core, platform/native_arcade_race_drive.{c,h}, library
  ctr_native_arcade_race_drive. It holds the per-tick decisions of LR-2 to
  LR-14 over caller-owned state. It has no socket, clock, SDL, heap, or
  game dependency. It may call the session and outcome libraries.
- Thin glue in platform/native_arcade_link_host.c. The glue owns the
  I/O: the peer link (through NativeArcadeNetplay_Link), OnTakeResult, the
  pacing switch, and the retained bundle bytes.

The game-side race caller (game/MAIN/MainArcadeRaceLaunch.c) reaches the
driver only through new NativeArcadeLinkHost_* calls, as Task 7 did. The
names are settled in LR-S7. For example: RaceBegin on the Launch frame,
RaceStep once per race tick, RaceHold while held, RaceEnd on the Disarm
frame. The host header forward-declares the canonical state it receives,
as it does NativeMatchConfigV1, so its include allow-list does not change.

Two new game modules have neutral names:

- MainArcadeRaceDigest projects V4 (LR-10).
- MainArcadeRaceHold runs the hold loop and its banner (LR-9).

The retail simulation stays untouched. The only retail-state change is the
LR-8 pin, made through the existing setup seam.

LR-2 Tick unit and frame index. One lockstep frame is one retail game tick,
at 30 Hz. The lockstep frameIndex is the race tick, counted from the
launch core's race tick 0. It is never frameCounter. Race tick 0 stays the
core's definition (RL-S7 interpretation (c)): the pass after the one that
first sees VALIDATED. VALIDATED is reached inside the init pass, which does
not render (MainMain.c:175, :201). So on both cabinets race tick 0 is the
second race GameLogic pass. The one extra neutral tick that costs is the
same on both.

On race tick k the drive does its step in the race caller at the hook
(MainFrame_RenderFrame.c:90). That is after GameLogic k and before
PickupBots_Update k, the lap stats, and the VBlanks of pass k. In order,
the step:

1. records the V4 digests of frame k (LR-10);
2. submits the local sample for frame k, consumed at frame k + D (LR-4);
3. sends bundles (LR-3);
4. polls the link;
5. takes frame k and installs its pads (LR-5).

The recording point is mid-tick, but it is the same code point on both
cabinets.

LR-3 Input delay, sending, and resending. D is the adapter's default 2
(native_arcade_netplay.h:185), the same on both sides; the relink
handshake already opens the session with it. Sample to simulation is D + 1
= 3 ticks (100 ms at 30 Hz). docs/LOCKSTEP_MILESTONE.md:297 quotes "33 ms
at 60 Hz" for D = 2, but at the real 30 Hz loop D = 2 buffers 67 ms.

On race tick 0 the drive also composes frames 0 to D - 1. Those carry the
zero pad and no digest. Each bundle is composed exactly once, and its
encoded bytes are kept in a ring the glue owns, as large as
NATIVE_LOCKSTEP_RING_CAPACITY. The session cannot compose an old frame
again (section 2.4), and a re-sent frame must be byte-identical.

Each race tick the drive sends the new bundle for frame k + D. It also
resends, verbatim, every kept bundle for frames k - D - 1 to k + D - 1.
The peer can still be consuming frame k - D - 1: our take of frame k - 1
needed its frame k - 1, which it composed on its tick k - 1 - D. While
held, the drive resends at most once per tick period, so a long hold does
not flood a peer that is still loading. The resend needs one new peer-link
call, a verbatim 128-byte bundle send that requires RUNNING (LR-S7).

The window bounds D. A cabinet can lead the peer's consumed frame by
2D + 1:

- the peer is stalled at frame t and has composed t + D;
- we take up to t + D;
- we then compose t + 2D + 1 before our own take stalls.

That must stay below the window of 8. So the drive refuses a session with
D above 3 and fails locally (LR-12). With D = 2 the lead is 5.

LR-4 Local sample. A new platform seam in native_input.c samples the
cabinet's own device into a snapshot without touching the pad bus. That is
host controller slot 0, where the cabinet's wheel, pad, and keyboard map by
default: controller, G29, and keyboard, as Platform_InputUpdate would read
them. The seam works while installed pads are active, and reads nothing
else. In CTR_INTERNAL builds the RL-15 autopilot can supply the sample
instead (LR-16). It never goes through installed pads.

The core normalizes every submitted sample, so only normalized bytes reach
the wire:

- connected 1;
- START released;
- a disconnected device becomes the neutral connected pad.

LR-5 Committed pads and the install frame. The committed inputs of frame k
are mapped to four snapshots by slot role:

- the CAB1_HUMAN slot's pad goes to retail pad 0 and CAB2_HUMAN to pad 1
  (NativeMatchConfigV1_FindRoleSlot);
- pads 2 and 3 are disconnected, as in the rehearsal and the roster
  proof's TWO_CAB profile;
- an all-zero pad (frames 0 to D - 1) becomes the neutral connected pad;
- every pad has START released and connected 1, whatever the peer sent.

The caller installs the four snapshots with
Platform_InputInstallPadSnapshots on race tick k, before the VBlanks of
pass k.

Those VBlanks are the ones that read them: the two VBlanks of pass k's
RenderVSYNC. There, Platform_PollInput replays them onto the pad bus
(MainDrawCb.c:47, native_input.c:1040-1046) and GAMEPAD_PollVsync copies
them into the gamepad buffers (MainDrawCb.c:50). GAMEPAD_ProcessAnyoneVars
of pass k + 1 (MainMain.c:406) derives held and tapped buttons from them,
and GameLogic k + 1 consumes them.

So GameLogic of race tick 0 reads the neutral pads installed since Launch,
and ticks 1 to D read the neutral mapping of frames 0 to D - 1. Tick D + 1
reads the first sample, taken on race tick 0. Any code that read the pad
bus between the hook and RenderVSYNC would read the same committed bytes on
both cabinets, so determinism does not depend on there being no such
reader.

Outside the race phase (from Launch to race tick 0, and from the end frame
to the clear), the caller keeps installing the RL-10 neutral pads.

LR-6 Pause and unplugged pads (answers RACE_LAUNCH risk 5). Pause is
disabled in a linked race, without touching retail code:

- START is never in a committed pad (LR-4, LR-5), so no START tap reaches
  the pause check (MainFrame.c:431).
- Every installed human pad is connected, so MainFrame_HaveAllPads never
  fails (MainFrame.c:428, :551-575).
- A retail gameMode1 pause bit is never used: it is canonical and would
  enter one side only.

The RL-13 vibration guard stays as it is, now unreachable.

LR-7 Deterministic VBlanks per tick. A linked race runs with fixed VBlank
pacing. The caller's RaceBegin turns it on, through the host glue, on the
Launch frame, before the race-track load and so before the RS-17 pins.
RaceEnd turns it off on the Disarm frame, the first idle main-menu frame
(RL-9), or before a process exit. An Arm or Launch failure never turns it
on.

The switch becomes available outside CTR_INTERNAL: the setter's guards at
include/platform.h:32-56 and native_platform.c:1024-1035 go. It stays
host-local and invisible to game code.
tests/native_vblank_pacing_isolation_test.cmake changes from "main.c only"
to exactly two callers, the roster proof's in main.c and the host glue,
with game/ still never naming it. Review required.

With fixed pacing and no other VBlank source in a race (section 2.1),
every race tick emits exactly 2 VBlanks, whatever the host frame time. So:

- frameTimer_Confetti and frameTimer_VsyncCallback advance by 2 per tick.
  Confetti starts from its RS-17 pin, and pause is impossible (LR-6).
- The root counter advances 526 units per tick, and elapsedTimeMS is 32
  after the first race tick. MainMain.c:231 pins it to 32 only while a
  load runs, and no load runs in the race.

LR-8 Root-counter pin. The wrap of section 2.2 makes elapsedTimeMS a
function of the boot-relative sdata->rcntTotalUnits on one tick about every
273 s. The RS-17 pin is extended so both cabinets start the race on the
same counter phase. OnFinalizeInitBegin in LAUNCHED, next to the timer and
Confetti pins, also writes:

- sdata->rcntTotalUnits = 0;
- gGT->clockFrameStart = -200.

The first race GameLogic then sees no VBlank since the pin, a delta of
200 ms, and elapsedTimeMS 64 (MainFrame.c:188-199). That is the value
retail computes after any load. The dangerous crossing then falls on the
same race tick on both cabinets, about race tick 8,165.

The only readers of the root counter are elapsedTimeMS and the render
statistic clockDurationStall (MainFrame_RenderFrame.c:1277, :1345). The
adapter reads both values back, as for the RS-17 pins, and the RS-17 audit
comment (MainArcadeRaceSetupCore.h:178-181) is corrected. This is a setup
seam change. Review required.

LR-9 Stall hold. A take that stalls must not advance the simulation, and
no VBlank may fire until the take succeeds.

The mechanism is a blocking hold in the race caller at the hook, which is
before the VBlanks that would read the pads. RaceStep does not block: it
returns GO (pads to install), HOLD, or END. On HOLD the caller enters
MainArcadeRaceHold's loop, which never calls VSync. So no VBlank is
emitted, and no sound or input poll runs. Each iteration:

- pumps host events (Platform_PollHostEvents), so the window stays
  responsive and a quit still exits;
- calls RaceHold, which polls the link, resends at most once per tick
  period, and retries the take;
- sleeps about 1 ms through a new host-local platform wait that emits no
  VBlank.

On GO the loop returns, and the pass continues exactly as it would have.
Fixed pacing re-anchors the next VSync instead of catching up (LR-7).

Stall time is counted in tick periods of wall time (2 VBlank periods,
33.4 ms). The drive calls OnTakeResult(STALL) once per full period held,
not once per retry. So a stall of a few milliseconds costs a few
milliseconds and never counts toward the timeout. A take that succeeds
calls OnTakeResult(OK), which resets the count. No retail gameMode1 bit is
used.

Presentation. For the first holdGraceTicks = 10 periods (0.33 s) the last
frame simply stays on screen. After that the hold loop redraws the
displayed frame with a WAITING FOR OPPONENT banner, in the arcade-link
layout's font and style, and presents it with Platform_PresentVRAMDisplay
(native_platform.c:643-648). It redraws once per period until GO or END.
The banner is drawn over the displayed image only. The next rendered frame
replaces it, and it never touches simulation state.

LR-S2 spikes the mechanism with a pass/fail (section 6). If the banner
cannot be drawn without touching simulation or render-pass state, the
fallback is the frozen frame without a banner, and risk 2 records it.

LR-10 Live V4 projection. It is made on linked races only, once per race
tick, by the game module MainArcadeRaceDigest through
MainCanonicalRuntime_PrepareV4. This is the runtime's first live caller,
and the "live gate" its isolation test waits for. The domains:

- Control. The MainMain.c:66-77 fields read live, with frameTimer
  (frameTimer_VsyncCallback) and frameCounter projected race-relative: the
  value minus its value at race tick 0, in unsigned 32-bit arithmetic. With
  LR-7 they read 2k and k on race tick k. A pacing fault would therefore
  show in the digest, where dropping the fields would hide it. timer is
  already race-relative through its pin. Every other field is level- or
  race-relative (MainArcadeRaceSetupCore.h:184-189).
- Retail RNG. The MainMain.c:80-84 fields.
- Deterministic bank. A copy of *MainArcadeRaceSetup_Bank(). It is
  constant during the race, so "project" means the RNG domain proves every
  tick that both cabinets hold the same post-setup bank. PrepareV4 takes
  it from the request instead of re-deriving a fresh one
  (MainCanonicalRuntime.c:316-317). The projector still checks masterSeed
  and the derivation version against the config
  (game/MAIN/MainCanonicalStateV4.c:61-62). The dormant MainArcadeSetupV4
  boundary stays dormant: it re-plans the setup on every call.
- Input. The four snapshots GameLogic k read, captured with
  Platform_InputCapturePadSnapshots before this tick's install and frozen
  as V1 input.
- Drivers. The runtime's complete extraction, Physics included (RS-13).
  It uses the MainCanonicalTopology context (Capture and Validate) and
  never the lease. MainArcadeRaceDigest invalidates the context on race
  tick 0 and on the end frame, because a rematch loads a level that can
  reuse addresses.
- World. MainCanonicalWorldCounters_ExtractV1 and
  MainCanonicalWorldMineRegistry_ExtractV1, newly linked into ctr_native.
- Topology. A new live topology fact reader over the race level's quad
  checkpoints, restart points, and nav paths, through
  MainCanonicalTopologyFactReader_ToV1. It is computed once on race tick 0
  and reused, because the level does not change during a race; LR-S4 checks
  that assumption.
- Identity and frame. The build and content identity (the link requires a
  known identity), the agreed config's digest, and frameNumber = k.

The per-tick cost is measured with NativePerf in LR-S4 (risk 3).
Review required: this touches canonical state and the runtime's
authorization. The format does not change.

LR-11 Digest exchange and desync. RecordLocalDigests gets the frame k
state on every race tick. The bundle for frame k + D + 1 carries it
(section 2.4), and AcceptBundle compares it with the peer's record of the
same frame. A mismatch latches DIVERGED, with the frame and the domain
mask. OnTakeResult or the adapter's own poll (section 2.4) turns it into
END_DESYNC. The glue logs the divergence report once:

    arcade link: race <n> out of sync at race tick <v> domains <mask> local <hex16> remote <hex16>

The last D + 1 frames before the finish are never carried on the wire. The
live gate compares every per-tick digest offline instead (LR-16).

LR-12 Failure mapping, bounds, and what the player sees. This reuses the
existing layers and adds no new end reason. Every bound is in ticks at
30 Hz, and every row ends on RESULTS, then the return load (the core's
return step, RL-8):

    event                         bound                       RESULTS title
    stall, held (resumes)         < 90 periods                none; banner after 10 periods (0.33 s)
    stall timeout                 90 periods (3.0 s)          OPPONENT DISCONNECTED (PEER_TIMEOUT)
    peer never starts             810 + 90 periods (30 s)     OPPONENT DISCONNECTED (PEER_TIMEOUT)
    peer drop (killed, unplugged) as stall timeout (3.0 s)    OPPONENT DISCONNECTED (PEER_TIMEOUT)
    desync                        detected by D + 1 ticks
                                  after the frame, else the
                                  stall timeout               RACE OUT OF SYNC (DESYNC)
    protocol fault                the record that faults      LINK ERROR (LINK_ERROR)
    local drive failure           the tick it happens         LINK ERROR (RL-11 path)
    race-length bound             18000 ticks (600 s)         RACE COMPLETE (FINISHED)

The rows in detail:

- Held stall. The player sees the frozen last frame, then the WAITING FOR
  OPPONENT banner over it (LR-9). Game sound does not advance: no VBlank
  runs the retail sound update or NativeAudio_StepVBlank.
- A stall that times out. The hold ends and the frame runs on. The caller
  installs neutral pads at once, so later ticks are not compared and
  cannot matter. The next host Tick moves the flow to RESULTS OPPONENT
  DISCONNECTED over the still-running race level, and the return load
  follows. This is the RL-10 and risk 3 behaviour.
- A peer that never starts. On race tick 0 the first cabinet waits for the
  other's load. The drive does not count the first raceStartGraceTicks =
  810 periods of that wait toward the stall timeout, so the whole start
  wait is 900 ticks (30 s). A one-sided launch (RL-7), or a peer that
  failed locally (RL-11), ends here.
- Peer drop. It ends the race for the survivor straight to RESULTS; the
  survivor does not race on against bots. The peer-drop roster is still
  applied for the record (native_arcade_netplay.c:600-610).
- A local drive failure. Examples: a V4 projection, record, or compose
  failure; a REJECTED take; D above 3. It goes through
  NativeArcadeLinkHost_ReportRaceFailure, and the log names it.

The race-length bound is deterministic: both cabinets reach it on the same
tick, so both end FINISHED. The log names "race tick limit".

The only new player-visible text is the hold banner, WAITING FOR OPPONENT
(the wording of MainArcadeLinkLayout.c:362).

LR-13 Race finish and the finish linger. The drive's finish is the first
race tick F whose hook sees END_OF_RACE. END_OF_RACE is set by the lap
stats of pass F - 1 (section 2.6), so F is the same tick on both cabinets
when the simulations agree. On F the drive records the digest, takes
nothing, and ends. The caller reports the finish (the core's
raceFinishedInput latch). It installs neutral pads from F until the clear,
as today (answers RACE_LAUNCH risk 3). The retail end-of-race screens that
run until the return load are host-local presentation and are not
compared.

After the finish the drive keeps resending its kept bundles for
finishLingerTicks = 15 ticks (0.5 s), so a peer that lost our last bundles
can still reach F. It stops earlier when the link closes or the flow
leaves RESULTS. It sends nothing after a failure end.

The race-length bound (LR-12) is 18000 ticks: seven laps of the longest
track fit. A kart parked for good cannot hold a cabinet forever.

LR-14 Stale bundles after a rematch (GAME_LOOP_UI risks 2 and 10). A
bundle of the finished match that reaches the rematch link would fault the
new session with MATCH_IDENTITY (section 2.4). Only the finish linger
sends after RACING, and it is bounded by 15 ticks:

- Both cabinets reach F within D + 1 ticks of each other.
- RESULTS ignores input for 30 ticks.
- So no cabinet can confirm REMATCH, and open its new link, while the
  other still lingers on a working link.

A stalled peer sees its own stall timeout first, well after the linger.
Tests:

- A new loopback case in tests/native_arcade_netplay_test.c
  (native_arcade_netplay_unit): a finished race with lingering bundles
  followed by a rematch must reach READY with no fault, and a bundle
  injected into the new link must fault exactly as described.
- A drive core case (native_arcade_race_drive_unit): nothing is sent after
  the linger or off RESULTS.
- The live gate's race 1 to race 2 transition.

LR-15 Host-local state. The drive state, the kept bundles, the pacing
flag, the hold state, the sample seam, and the banner are host-local.
None of them is in a checkpoint, in replay, or in canonical state, and none
touches the topology lease: no acquire, activate, capture, or publish, no
lease owner anywhere new, and no hook on LOAD_Hub_ReadFile. The V4 state is
projected and digested, never serialized or replayed.

Default boot, replay, and the roster proof are unchanged unless the host is
in LINK mode with a launched race. The link and replay options already
exclude each other (GAME_LOOP_UI risk 7), and the caller's first statement
stays the dormant return (MainArcadeRaceLaunch.c:374-378).

LR-16 One-machine proof. The RL-15 gate arcade_link_launch is extended. It
keeps its name, its ports (cab1 7001, cab2 7002), RUN_SERIAL, the label
live, TIMEOUT 900, and its skip rules. It becomes one run of three races,
each driven in lockstep:

- Race 1, the finish. The select defaults give the fixture's Crash Cove
  and 3 laps (include/platform/native_arcade_link_options.h:57-58). Each
  autopilot drives its own human. At race tick 600, cab2 freezes itself
  for 45 periods (1.5 s): it sends and takes nothing. That is a
  stall-then-resume on cab1. Both must reach END_OF_RACE on the same race
  tick and show RACE COMPLETE.
- Race 2, the desync, after REMATCH. At race tick 300, cab2 XORs one bit
  into the digest it records for that tick. The simulations stay identical,
  but cab2's reported digest now differs. At least one cabinet must show
  RACE OUT OF SYNC naming frame 300. The other must show RACE OUT OF SYNC
  or OPPONENT DISCONNECTED (risk 7).
- Race 3, the peer drop, after REMATCH. The checker kills cab2 once cab2's
  log reports race 3 tick 300. cab1 must show OPPONENT DISCONNECTED within
  90 periods, plus a margin, of its last taken frame. It then takes EXIT
  and exits 0 with PASS. cab2's kill exit is expected.

The checks, beyond today's (the agreed-match line and the config, plan,
bots, and bank digests of the k-th race equal across the two, and each
race's config different from the one before):

- each report logs one line per race tick with the combined V4 digest and
  the domain digests;
- for every tick both logged, the lines are equal across the two
  processes, the D + 1 tail included;
- the finish tick is equal;
- the end reasons are as above.

The finish problem. A kart that only holds accelerate does not finish a
race (section 2.6). The replay tooling does have a per-tick input trace
(the V2 replay, docs/REPLAYS.md). But it records a whole session from boot
against one seed and build, the match seed comes from host entropy in the
select, and the link rejects replay options. So it is not used.

Instead, in CTR_INTERNAL builds the autopilot drives closed-loop:

- It holds CROSS and steers toward the next restart point ahead of its own
  kart. The restart points are the checkpoint loop the lap logic walks
  (gGT->level1->ptr_restart_points, MainFrame_RenderFrame.c:241).
- The steering decision is pure (platform/native_arcade_link_autopilot.c),
  over pointer-free facts that the glue reads from game state, which it
  only reads.
- Its pads enter as the local sample (LR-4).
- The cabinets do not need identical autopilot behaviour, because only the
  committed bytes matter.

The fault injections (freeze and digest XOR) are internal-only autopilot
options, rejected like the other arcade options. LR-S2 spikes the
autopilot finish. If it fails, the fallback is an internal race-tick cap
for the gate: the drive ends the race at the cap as FINISHED on both
cabinets. The natural finish is then proven only by the drive core test
and a recorded manual two-process run (risk 1).

Time budget. Today's gate takes about 79 s, of which the two 150-tick
rehearsals are 10 s. Estimates, to be measured in LR-S2 and recorded in
LR-S10:

- race 1: about 3,600 to 4,500 ticks (120 to 150 s);
- race 2: about 10 s to the desync;
- race 3: about 13 s to the timeout;
- a third select, load, and RESULTS: about 25 s.

That puts the gate near 270 s, so it adds about 190 s. The full suite
goes from about 450 s to about 640 s. `ctest -LE live` does not change.

### 4.1 Per-tick simulation inputs

Every input the simulation reads per race tick, and why it is identical on
both cabinets:

    input                     source per race tick k                       why identical
    human pads                frame k - 1 pads, installed on tick k - 1    lockstep commit; one mapping (LR-5)
                              (neutral on tick 0)
    bot input                 none: bots run from seed and roster          same config, RS-7 seeds
    VBlanks per tick          2, from RenderVSYNC                          fixed pacing (LR-7); no other source in race
    elapsedTimeMS             root counter over 2 VBlanks: 32              VBlank-derived counter; LR-8 pin
                              (64 on the first race tick)
    trafficLightsTimer        previous tick's elapsedTimeMS                follows from the row above
    frameTimer_Confetti       pinned 0 at race init, +2 per tick           RS-17; LR-7; no pause (LR-6)
    gGT->timer                pinned 0 at race init, +1 per GameLogic      RS-17
    frameTimer_VsyncCallback  boot-relative; no simulation reader          compared race-relative (LR-10)
    frameCounter              boot-relative; no race reader                compared race-relative (LR-10)
    retail RNG states         seeded at race init, then drawn by the sim   RS-7; equal inputs
    deterministic bank        constant after setup                         MainArcadeRaceSetup_Bank, same config
    pause                     impossible                                   LR-6
    demo mode, cheats         pinned by the setup                          RS-16, RS-2
    loads                     none in an arcade race                       section 2.6
    hold                      no VBlank, no simulation code                LR-9
    race tick 0               the core's definition                        same pass on both (LR-2)
    sound, rendering, window  host-local; not read by the simulation       assumed; the V4 digest catches a leak (risk 8)

Sound runs per emitted VBlank (MainDrawCb.c:38-42, native_platform.c:909),
so it follows the same VBlank sequence on both cabinets. Its own RNG is
not canonical (MainMain.c:79). Render scale and display options are
cabinet-local (docs/REPLAYS.md, render-scale sweep).

## 5. Constraints

- The topology lease is untouched. No lease owner in checkpoints, replay,
  or canonical state, and no retire hook on LOAD_Hub_ReadFile. The live V4
  path uses the MainCanonicalTopology context only. The runtime's lease
  and replay bans (tests/main_canonical_runtime_isolation_test.cmake:19-47)
  stay.
- No change to the canonical-state schema, the replay format, the bundle,
  the handshake, NativeMatchConfigV1, or the launch record. The peer link
  gains one send call; the session and window are unchanged.
- The drive core is pure. The game modules and the caller name no lockstep
  token. Every structural rule gets an isolation test:
  - a new tests/native_arcade_race_drive_isolation_test.cmake: purity, the
    RACE_LAUNCH section 5 token ban, and C17;
  - updates to the pacing, runtime, hook, race setup, and sound-identity
    isolation tests.
- No heap. Portable C17 with compiler extensions off.
- Default boot is unchanged.
- Done means the MSVC build plus the full ctest suite, with a
  non-skipped live gate.
- Commit on `arcade` only, no push. Anything touching simulation identity,
  the wire, replay, canonical state, the setup seam, or the pacing switch
  is reviewed.

## 6. Task list

Slices are grouped so that one milestone run does at most four. The last
slice of any group that touches the launch or race path records a fresh,
non-skipped run of arcade_link_launch and arcade_roster_determinism, from
a clean tree (RACE_LAUNCH risk 7).

### LR-S1 -- this document

Status: done (this document); plan review pending. docs/LOCKSTEP_RACE_MILESTONE.md
and the Task 8 pointer in docs/GAME_LOOP_UI_MILESTONE.md.

### LR-S2 -- spikes: the hold and the autopilot finish

Status: planned. Internal only.

Plan: two spikes on the roster proof, which already runs a real race on
installed pads under fixed pacing.

- (a) The hold. An internal proof option holds for 45 periods at proof
  race tick 300, through the MainArcadeRaceHold loop and banner of LR-9.
  - Pass: arcade_roster_determinism gains a run K, A's seed with the hold,
    and K equals A at every tick in every digest.
  - Pass: K's report shows frameTimer_VsyncCallback advancing by exactly 2
    across the held tick.
  - Pass: the hold lasts 45 periods of wall time within 10%.
  - Pass: the window keeps pumping events (no "not responding").
  - Pass: a frame capture taken during the hold shows the banner.
  - Fail: any of these. The banner falls back to the frozen frame alone
    (LR-9, risk 2); the rest is a design stop.
- (b) The finish. An internal steering pad profile of the LR-16 autopilot
  drives the proof's player 0.
  - Pass: 3 laps of Crash Cove reach END_OF_RACE within 6000 race ticks in
    5 of 5 seeds; the measured lengths set the LR-16 time budget.
  - Fail: the gate falls back to the internal race-tick cap (LR-16).

Tests: the K run in tools/arcade-roster-proof-check.ps1; a unit test of the
steering decision.

### LR-S3 -- deterministic time for linked races

Status: planned. Review required (the setup seam and the pacing switch).

Plan: LR-7 and LR-8.

- The pacing setter leaves CTR_INTERNAL. The host glue turns pacing on at
  RaceBegin (the Launch frame) and off at RaceEnd (the Disarm frame).
- The setup pins rcntTotalUnits and clockFrameStart in LAUNCHED, reads
  them back, and the RS-17 audit comment is corrected.

Tests:

- native_vblank_pacing_isolation: two callers.
- The setup core test: the new pins and their readback.
- main_arcade_race_setup_isolation.
- A unit test of the Timer.c arithmetic: it shows the crossing and that
  the pin fixes its race tick.
- Fresh non-skipped arcade_link_launch and arcade_roster_determinism runs
  (last slice of the group).

### LR-S4 -- live V4 projection

Status: planned. Review required (canonical state, the runtime's
authorization). This is the slice docs/GAME_LOOP_UI_MILESTONE.md Task 8
points to.

Plan: LR-10.

- game/MAIN/MainArcadeRaceDigest.{c,h}, in the unity chain.
- The PrepareV4 request carries the bank.
- A live topology fact reader.
- The world extractors are linked into ctr_native.
- The race-relative control base is captured on race tick 0.

The roster proof also projects V4 each proof tick and logs its combined
and domain digests (report v9). tools/arcade-roster-proof-check.ps1
requires them equal wherever it requires rng, input, and drivers equal:
A = B, C and E equal A, I and J equal F. The proof also checks that the
topology digest is constant across a race.

Tests:

- main_canonical_runtime_isolation: one live caller, still lease-,
  replay-, and MainMain-free.
- main_canonical_runtime_stack_budget_v4 still holds.
- A unit test of the race-relative control projection.
- The per-tick cost recorded with NativePerf.
- A fresh arcade_roster_determinism run.

### LR-S5 -- local sample seam and normalization

Status: planned.

Plan: LR-4. Platform_InputSampleLocalPad (name settled here) in
platform/native_input.c reads host slot 0 while installed pads are active,
without writing the pad bus. The normalization (START released, connected,
disconnected to neutral) is a pure function in the drive core.

Tests: a native_input unit case with installed pads active; normalization
cases in native_arcade_race_drive_unit.

### LR-S6 -- pure drive core

Status: planned.

Plan: LR-2, LR-3, LR-5, LR-9 accounting, LR-12, LR-13, LR-14.
platform/native_arcade_race_drive.{c,h}, library
ctr_native_arcade_race_drive. It covers:

- per-tick step order;
- the zero frames at race tick 0;
- the resend window and the kept-bundle ring;
- the refusal of D above 3;
- pad mapping by role;
- hold accounting in periods, the start grace, and the hold grace;
- finish detection, the linger, the race-length bound, and end mapping.

Tests: native_arcade_race_drive_unit, with two cores exchanging bundles
through the real session library in memory. It covers:

- equal inputs, stall and resume, stall timeout at 90, the start wait at
  900;
- a peer drop and a forced desync on both sides;
- WINDOW_OVERRUN impossible at D = 2 and refused at D = 4;
- no send after the linger or off RESULTS.

native_arcade_race_drive_isolation covers purity, the token ban, and C17.

### LR-S7 -- drive glue and the rehearsal's replacement

Status: planned. Review required (simulation identity, the launch and race
path).

Plan: LR-1, LR-3, LR-9.

- The host API (RaceBegin, RaceStep, RaceHold, RaceEnd).
- The glue over the netplay adapter.
- The peer-link verbatim bundle send.
- The caller:
  - it projects (LR-S4), samples (LR-S5), steps, and installs committed
    pads on race ticks;
  - it runs the basic blocking hold, without the banner;
  - it reports the finish from the drive.
- The launch core's REHEARSAL phase becomes the drive phase. It takes the
  drive's end and failure as inputs, and a new failure code DRIVE_FAILED
  is appended.
- An internal race-tick cap for the autopilot keeps today's two-race gate
  short until LR-S10.

Tests:

- main_arcade_race_launch_core_unit and its isolation.
- native_arcade_link_host_unit.
- native_lockstep_peer_link_unit, for the new send.
- main_arcade_link_hook_isolation: the install only through the mapping,
  and the dormant return.
- arcade_sound_identity_isolation, which lists the new files.

### LR-S8 -- hold presentation

Status: planned.

Plan: the LR-9 grace and banner from the LR-S2 result: the
MainArcadeRaceHold draw path and a layout item for the banner.

Tests: a layout unit case; the preview capture checker gains the hold
banner if LR-S2 found it capturable.

### LR-S9 -- failure wiring to RESULTS

Status: planned. Review required.

Plan: LR-11 to LR-14 end to end:

- the start grace;
- stall timeout, fault, and desync through OnTakeResult;
- local failure through ReportRaceFailure;
- the race-length bound;
- the finish linger;
- the divergence log line;
- neutral pads from the end frame.

Tests: the native_arcade_netplay_unit stale-bundle loopback case (LR-14);
host and core cases for every row of the LR-12 table. Fresh non-skipped
arcade_link_launch and arcade_roster_determinism runs (last slice of the
group).

### LR-S10 -- one-machine race gate

Status: planned. Review required.

Plan: LR-16.

- The autopilot's steering sample.
- The freeze and digest-XOR injections and the per-tick report lines.
- RESULTS decisions for DESYNC and PEER_TIMEOUT: REMATCH after race 2,
  EXIT after race 3.
- tools/arcade-link-launch-check.ps1 runs three races, kills cab2 in
  race 3, and does the new comparisons; -TimeoutSeconds goes up to fit.
- CMakeLists.txt comments.

Tests: native_arcade_link_autopilot_unit and its isolation (the new
options are rejected outside CTR_INTERNAL and with replay options). A
recorded non-skipped PASS of the full suite from a clean tree, with the
measured gate time.

### LR-S11 -- docs close-out

Status: planned.

Plan: this document; GAME_LOOP_UI Task 8 and risks 1, 2, 3, and 10;
RACE_LAUNCH risks 3, 4, 5, and 8; ROSTER risk 7, RS-13, and RS-18;
docs/LOCKSTEP_MILESTONE.md's 60 Hz figures; and docs/HANDOFF.md sections
other than "Next work".

## 7. Risks and open questions

1. The scripted finish. The closed-loop autopilot may not finish 3 laps
   reliably: walls, jumps, or items can hit it. LR-S2 (b) decides. On
   failure, the gate uses an internal race-tick cap. The natural
   END_OF_RACE path is then proven only by the drive core test and a
   recorded manual run.
2. The hold mechanism. A blocking hold in the hook is new, and so is
   drawing a banner onto the displayed frame. SDL or GL presentation
   behaviour during a long hold, and audio underrun while no VBlank steps
   the mixer, are host-local but visible. LR-S2 (a) decides. The fallback
   is a frozen frame without a banner.
3. Per-tick V4 cost. Drivers extraction and assembly, topology
   validation, and the world extractors now run every tick, in Debug too.
   The topology summary is computed once per race. LR-S4 measures the cost.
   If it threatens the 33 ms frame, the drive projects every tick but the
   slice reports it for a budget decision; frames are never skipped.
4. Asymmetric host speed. Fixed pacing never recovers a slow frame, so a
   host hitch of x ms costs its cabinet x ms of wall time for good. The
   other cabinet then holds once for about x ms, because its lead is
   capped at D + 1 ticks. Healthy hosts drift only by clock ppm, so stalls
   should be rare and short: holds are polled at 1 ms and the timeout
   counts only full periods. A slow host still makes its peer's race
   hitch.
5. Paused link ticks in file reads. None happen inside an arcade race
   (section 2.6). The race-track load and the return load still pause
   them. The start grace covers the load gap at race tick 0, up to 30 s.
6. Two generals at the race end. A peer that lost our last bundles
   depends on the 15-tick linger. If those are lost too, it stalls into
   OPPONENT DISCONNECTED while we show RACE COMPLETE. This is fail-safe,
   not prevented, as RL-7 is at launch.
7. Asymmetric desync detection. A cabinet that detects the divergence
   before composing the bundle that carries its own digest of that frame
   stops composing (DIVERGED is terminal). The other side may then stall
   into OPPONENT DISCONNECTED instead of RACE OUT OF SYNC. The live gate
   accepts either for the second cabinet.
8. Host-local state leaking into the simulation. Sound, rendering,
   memory-card refresh, and the window are assumed not to feed the
   simulation. The render-scale sweep supports this for rendering. Physics,
   WORLD, and TOPOLOGY have never been compared live before (RS-13). LR-S4's
   V4 proof runs and the live gate's per-tick comparison are the checks. A
   leak shows as a desync naming the domain.
9. The root-counter wrap (section 2.2). Without LR-8, linked races
   desync, on one tick, at about 273 s of either cabinet's uptime.
10. A new race-tick-0 load gap. Two loads on one machine usually finish
    within seconds of each other. A slow real cabinet could exceed the
    30 s start bound; the bound is a default for review.
11. Stale bundles after a rematch (LR-14). This is safe by the linger bound
    plus the RESULTS dwell, and tested. A shorter dwell or a longer linger
    would reopen it.
12. The live gate needs a build from a clean tree (GAME_LOOP_UI risk 5),
    and a skip does not count.
13. The 30 Hz assumption. Every bound here is in 30 Hz ticks (RS-14
    enforces 30/1). The failure-handling layer's own constants and
    docs/LOCKSTEP_MILESTONE.md are written for 60 Hz: the stall default is
    180 frames and D = 2 is quoted as 33 ms. The adapter already passes 90
    (UX-9). LR-S11 fixes the prose.
14. Suite time. The extended gate adds about 190 s. If that is too slow,
    races 2 and 3 can use the internal race-tick cap, but race 1 cannot.
15. D is fixed at 2 and at most 3, because of the window lead of LR-3.
    The ring capacity is frozen at 8 by tests/native_lockstep_isolation_test.cmake,
    and raising it is out of scope.
16. Open for the owner:
    - Should the race-length bound end as RACE COMPLETE (LR-12) or as
      another reason? Another reason would change the flow.
    - Does RESULTS show standings, as the GAME_LOOP_UI Task 8 sketch said?
      Not planned here.
    - How should each cabinet present the two-player race (today the
      retail two-player view on both)?
