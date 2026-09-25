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
- End the race at the finish grace (LR-18). Its 3-4-human start rule
  (all but one finished) is exercised only in the pure drive core
  (LR-S8), since more than two cabinets is out of scope.
- Prove all of it on one machine with a two-process live gate.

Already done: race-launch risk 10. The link's return to title and the race
caller's deferred return step both run only at LOAD_IDLE or LOAD_REQUESTED
(c12183e08, and the follow-up eb382038e; section 2).

Out of scope:

- rollback or prediction (docs/LOCKSTEP_MILESTONE.md forbids both);
- a standings view on RESULTS (the GAME_LOOP_UI Task 8 sketch mentions
  "reusing the retail standings drawing"; see risk 16);
- a live TOPOLOGY comparison. Task 8 supplies the unavailable topology
  summary, and live topology waits for a future lease-activation milestone
  (LR-10, risk 17);
- a single-view presentation. Each cabinet shows the retail two-player
  race it simulates (section 3, criterion 9; the setup plan's TWO_CAB
  shape, game/MAIN/MainArcadeRaceSetupPlan.c:60). The owner ruled on
  2026-09-25 that the retail split screen ships. A full-screen view of
  one's own player on each cabinet is a later stretch milestone, out of
  Task 8's scope (risk 18);
- a ONE_CAB lobby (ROSTER risk 14);
- more than two cabinets;
- physical two-cabinet and G29 validation (HANDOFF steps 6-7).

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
vsyncTillFlip to 2 (:1349). So every pass that follows a rendered pass
asks for exactly 2 VBlanks. The other game VSync callers are load, boot,
error, and scrapbook paths.

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
  platform/native_vblank_pacing.c:5-20). Before LR-S3 (a) it existed only
  in CTR_INTERNAL builds and its only caller was main.c, for the roster
  proof (RS-18 calls it proof-only). Since LR-S3 (a) (LR-7) the setter is
  outside CTR_INTERNAL: declared at include/platform.h:87 and defined at
  native_platform.c:1070. It has two callers: main.c, still only for the
  roster proof (main.c:511), and the arcade-link host glue for a linked
  race (platform/native_arcade_link_host.c:175-215).
  tests/native_vblank_pacing_isolation_test.cmake:4-18 pins all of that.
- elapsedTimeMS is derived from VBlanks, not from wall time.
  Timer_GetTime_Total converts sdata->rcntTotalUnits to milliseconds as
  (units * 1000) / 0x147e in 32-bit signed arithmetic (game/Timer.c:31-43).
  Timer_GetTime_Elapsed adds 0xc7e18 when the value goes backwards
  (Timer.c:48-64). MainFrame.c:188-203 then scales the result by 32/100 and
  clamps it to 64. A pause gives 32. Two VBlanks per tick give 32. The
  first GameLogic after a load computes up to 64 from the whole load. It
  stores 32, though: the load set gameMode1_prevFrame to 1
  (LOAD_TenStages.c:499), and MainFrame.c:200-203 overrides the value
  after a paused previous frame (observed in LR-S3 (b)).
- The arithmetic wraps. The multiply overflows a signed 32-bit int, which
  is undefined behaviour in C17; MSVC, the reference toolchain, wraps it.
  That makes the per-tick delta depend on the absolute, boot-relative
  rcntTotalUnits (include/regionsEXE.h:3720; its only retail writer is
  MainDrawCb.c:32, and since LR-S3 (b) the setup's LR-8 pin,
  MainArcadeRaceSetup_Apply, is the only other). A simulation of 2 VBlanks per tick through Timer.c's
  arithmetic gives a delta of 99 ms, so elapsedTimeMS 31, at the tick where
  units * 1000 crosses zero from below. That happens once every
  2^32 / 1000 units, about 16,331 VBlanks or 273 s of uptime, for most
  counter phases. Two cabinets never share a boot history, so that tick
  differs between them.
- The RS-17 audit said gGT->clockFrameStart is harmless because "only
  their deltas matter" (game/MAIN/MainArcadeRaceSetupCore.h:178-181 before
  LR-S3 (b)). That missed the wrap (LR-8). LR-S3 (b) corrected it, and
  rcntTotalUnits and clockFrameStart are now pinned
  (MainArcadeRaceSetupCore.h:178-207). The roster proof launches about
  5,400 to 6,600 proof ticks after boot
  (include/platform/native_arcade_roster_proof.h:56-60) and races 900
  ticks, so its races appear to end just before the first such crossing.
  That would explain why the live proof never met it.
- The setup pins gGT->timer and gGT->frameTimer_Confetti to 0 at race init
  (RS-17), and since LR-S3 (b) sdata->rcntTotalUnits to 0 and
  gGT->clockFrameStart to -200 (LR-8). The pins are written only by
  OnFinalizeInitBegin in LAUNCHED (MainArcadeRaceSetupCore.h:217-220),
  which runs at game/MAIN/MainInit.c:425.
  sdata->frameCounter and frameTimer_VsyncCallback stay boot-relative
  (MainArcadeRaceSetupCore.h:155-168):
  - frameCounter has no race-simulation reader.
  - frameTimer_VsyncCallback's readers are the level-audio distortion
    (game/HOWL/HOWL_LevelAudio.c:52), frameTimer_notPaused (written at
    MainFrame_RenderFrame.c:1384 and never read), and the load queue's VRAM
    delay (game/LOAD/LOAD_Queue.c:77).
- The roster proof uses the same setup seam: it arms and launches the race
  through MainArcadeRaceSetup_Arm and _Launch
  (game/MAIN/MainArcadeRosterProof.c:289-296), so every pin the setup
  writes applies to its races too.

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
- Where the snapshots live. s_controllers[].snapshot is both the V4 input
  source and the device state:
  - Platform_InputCapturePadSnapshots copies it
    (native_input.c:1155-1168);
  - the installed path overwrites it (NativeInput_WriteInstalledSnapshots,
    :264-272);
  - the device path resets and rewrites it through the Apply calls in
    Platform_InputUpdate (:1053-1063), and the controller and G29 Apply
    calls also write s_lastActiveControllerSlot (:469, :631), which
    Platform_InputCaptureState saves (:1211).
- The pad bus gets a snapshot's status and id bytes verbatim
  (NativeInput_WritePadPacket, :201-202). A connected device writes status
  0 and id 0x41 (digital) or 0x73 (analog) (:393-394, :621-622, :735-736);
  a disconnected slot writes 0xff for both (:184-185). The port state reads
  only the connected flag (:1152).
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
  - A STALL latches nothing and consumes nothing (:287-298). A take in any
    mode other than RUNNING is REJECTED (:288-291).
  - The first inputDelay consumption frames consume the all-zero pad
    (platform/native_lockstep_session.c:505-512). That pad reads as
    disconnected with every button pressed.
  - A bundle carries the local pad for its frame and the verified digests
    of frame frameIndex - D - 1 (native_lockstep_session.h:226-243). A
    bundle composed on race tick k is for frame k + D, so it carries frame
    k - 1 whatever D is.
  - The digest history is D + 2 frames deep (:29-30), so a bundle for an
    old frame cannot be composed again later.
- The session compares a peer digest on arrival. AcceptBundle verifies
  every record the window ACCEPTED at once
  (native_lockstep_session.c:457-462). If the carried frame is newer than
  the last recorded one, FindDigests returns NULL (:58-61), and the session
  latches a FRAME_UNAVAILABLE divergence (:146-154). So today a peer that
  is one tick ahead is a desync (LR-11 changes this).
- The adapter's Tick drains the link at the hook: the lobby poll
  (native_arcade_netplay.c:654) runs inside MainArcadeLink_Frame
  (MainFrame_RenderFrame.c:81), before the race caller (:90) could record
  this tick's digests.
- The receive window accepts frames in [consumedFrame, consumedFrame + 8)
  (platform/native_lockstep_input_window.c:30-34):
  - a frame below the window is a STALE drop (:63-69);
  - a frame above it is a WINDOW_OVERRUN fault (:71-79);
  - a re-delivered frame must be byte-identical, or it is a
    CONFLICTING_INPUT fault (:81-95).
  A bundle from another match identity is a MATCH_IDENTITY fault
  (platform/native_lockstep_protocol.c:285-287). The decoder checks the
  identity only after the size, magic, version, and the bundle's own
  digest have passed (:264-283).
- The peer link routes 128-byte records by link mode
  (native_lockstep_peer_link.c:211-222): into the session while RUNNING,
  and into an 8-entry staging buffer while HANDSHAKING. The staged records
  are replayed into the session as soon as it opens (:130-160, :194). A
  record staged from an older match therefore faults the new session.
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
- The launch agreement's records (RL-4) are sent only from the adapter's
  Tick: its launch intake (NativeArcadeNetplay_DriveLaunch, :550-571) and
  its send and linger tick (NativeArcadeNetplay_SendLaunch, :577-595,
  called at :803). After the commit they linger for up to
  NATIVE_ARCADE_LAUNCH_DEFAULT_LINGER_TICKS = 300 ticks
  (include/platform/native_arcade_launch.h:38-43, :66-67).
- Confirming REMATCH closes the lobby and opens a new lobby and link on the
  same port (NativeArcadeNetplay_BeginRematch, native_arcade_netplay.c:235-271,
  run for BEGIN_REMATCH at :772). A rematch wait that never reaches READY
  ends as OPPONENT LEFT after rematchWaitTimeoutTicks = 300
  (include/platform/native_arcade_flow.h:54-55, :68).
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
  registry, and topology. ComputeDigests is :42. The domain order is
  CONTROL, RNG, INPUT, DRIVERS, WORLD, TOPOLOGY
  (platform/native_canonical_codec.c:9-16), so domain digest i is bit i of
  a divergence's canonicalDomainMask. NativeCanonicalStateV4_Validate does
  not recheck the digests (platform/native_canonical_state_v4.c:30).
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
  - It extracts the complete drivers (:294-307). That extraction reads
    NavHeader.last for every bot. :296 calls
    MainCanonicalDrivers_ExtractCompleteFromPreludeInPlace
    (game/MAIN/MainCanonicalDrivers.c:927), which calls
    MainCanonicalDrivers_ExtractMetaAndBot for every present driver
    (:953-954). For a bot that calls MainCanonicalDrivers_BotNavIndex
    (:834), which reads sourceData->NavPath_ptrHeader, the level's
    LevNavTable, and the NavFrame array (:720-725) and dereferences
    header->last (:726). It does so to prove the bot's botNavFrame lies in
    its path's frame array and to turn it into an index. A TWO_CAB race has
    four bots (slots 2 to 5: MainArcadeRaceSetupPlan.c:58-64,
    include/platform/native_arcade_bot_rules.h:152-153).
  - It takes the world and topology domains from the caller (:309-314). It
    touches no lease API. Its comments at MainCanonicalRuntime.h:137-140
    and MainCanonicalRuntime.c:309-311 say it never dereferences
    NavHeader.last; the bot read above makes that false (LR-17, ruled
    (a): the read may go live, check-only).
  - It builds a fresh bank from the config's masterSeed instead of the
    post-setup bank (:316-317).
  - Any failure poisons the workspace until Reset
    (MainCanonicalRuntime.h:115-117; V4 mirrors V3, :137). Reset
    invalidates the topology context and clears the poison
    (MainCanonicalRuntime.c:69-87).
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
- A live topology reader is a topology capture, and captures belong to the
  lease:
  - MainCanonicalTopologyLease_ObservePostInit is documented as the sole
    API that observes NavHeader.last
    (game/MAIN/MainCanonicalTopologyLeaseAuthority.h:72-75). The drivers
    extraction's bot read above contradicts that comment, and the
    CTR_INTERNAL roster proof already makes the same read live:
    MainArcadeRosterProof.c:579 calls
    MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta,
    which reaches BotNavIndex through ExtractMetaAndBot
    (MainCanonicalDrivers.c:860, :834). That was a precedent, not a
    ruling; the owner has since ruled (a) (LR-17);
  - the fact reader's one live consumer is the lease adapter, behind
    Acquire, ObservePostInit, and the residency checks
    (tests/main_canonical_topology_lease_adapter_isolation_test.cmake:43),
    and ctr_native may not link that adapter (:18-24);
  - the lease is retire-only, and every activation or capture call site
    needs a new live-cabinet gate, deterministic capture evidence, and
    review (docs/TOPOLOGY_LEASE_AUTHORITY.md:12-14, :24-30; AGENTS.md).
- The unavailable topology summary is valid V4. NativeCanonicalTopologyV1_Init
  builds it: flags 0, levelID and every count 0, every digest the empty
  digest (platform/native_canonical_topology.c:31-39), and
  NativeCanonicalTopologyV1_Validate accepts it (:64-66).
- RS-13 left Physics, WORLD, TOPOLOGY, and live V4 projection to Task 8
  (docs/ROSTER_MILESTONE.md:589-591). The roster proof digests only V1
  control, RNG, and input, plus a drivers candidate without the
  MainCanonicalTopology context, whose Physics groups are zero. That
  candidate still makes the bot nav-index read above (LR-17, ruled
  (a)).

### 2.6 Race end, loads, presentation, and builds

- In ARCADE_MODE the race ends when every human has finished
  (game/PlayLevel.c:435-437, then MainGameEnd_Initialize at :476, which
  sets END_OF_RACE at game/MAIN/MainGameEnd.c:546; END_OF_RACE is 0x200000,
  include/namespace_Main.h:28). The linked race sets ARCADE_MODE (the
  plan's gameMode1 set mask, game/MAIN/MainArcadeRaceSetupPlan.h:365).
  PlayLevel_UpdateLapStats runs after the hook (section 2.1), so the hook
  sees a tick's END_OF_RACE on the next pass. Under the retail rule
  alone, a human who never finishes never ends the race. In a linked
  race that human no longer holds the race beyond the finish grace: the
  drive ends it 900 ticks after the first human finish (LR-18).
- No load runs during an arcade race. The end-of-race overlay is loaded
  with the level (game/LOAD/LOAD_TenStages.c:247). The only in-render
  overlay load is the adventure hub's (MainFrame_RenderFrame.c:656).
  Synchronous file reads, and with them paused link ticks (RACE_LAUNCH risk
  2), happen only around the race: the race-track load and the return load.
- The TWO_CAB race is a retail two-player race (numPlyrNextGame 2,
  MainArcadeRaceSetupPlan.c:60). Its presentation is the retail split
  screen: camera 0 fills the top half and camera 1 the bottom half
  (game/PushBuffer.c:44-76, set up at game/MAIN/MainInit.c:499-500), and
  driver i reads gamepad i (for example game/GAMEPAD.c:967 and
  game/CAM.c:1053).
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
   control and the post-setup bank, and the unavailable TOPOLOGY summary
   (LR-10). Its digests are recorded, exchanged in the bundles, and
   compared, and a mismatch ends the race as DESYNC. A cabinet that leads
   the other by up to D + 1 ticks is not a mismatch (LR-11).
4. A stall holds the simulation with no VBlank emitted. The hold keeps
   servicing the link and the launch linger (LR-9). The player sees
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
5. The race ends on the retail finish (END_OF_RACE) or at the finish
   grace (LR-18), on the same race tick on both cabinets, and RESULTS
   shows RACE COMPLETE.
6. Pause cannot happen in a linked race, and no pad reads as unplugged.
7. Default boot and replay are unchanged. Nothing new enters checkpoints,
   replay, or canonical-state formats, and the topology lease is
   untouched. The one lease-relevant read is the check-only NavHeader.last
   read of the bot nav index, ruled (a) in LR-17. A bundle from an
   earlier match never faults a rematch (LR-14).
8. The full ctest suite passes. The extended live gate (LR-16) has a
   recorded, non-skipped PASS from a build made from a clean tree. A skip
   (77) does not count.
9. Both cabinets show the race as the retail two-player split screen:
   cab1's player (retail player 0) in the top half and cab2's player
   (retail player 1) in the bottom half, the same picture on both
   (section 2.6). Task 8 adds no presentation code for this. The owner
   ruled on 2026-09-25 that this split screen is what ships.

How each will be proven:

1. The drive core unit test (LR-S8) and the extended live gate: both
   processes log equal per-tick input digests and reach the finish on the
   same tick.
2. Two pieces:
   - arcade_roster_determinism: its runs still agree tick by tick with the
     root-counter pin and V4 in place (LR-S3, LR-S4).
   - The live gate: it compares every per-tick V4 digest across the two
     processes, including the race-relative control.
3. The live gate's forced-desync race ends in RACE OUT OF SYNC with the
   divergence frame named. The per-tick V4 lines are equal in the races
   without a fault, and race 1's freeze leaves one cabinet leading without
   a desync (LR-S5's lead cases prove every lead up to the bound, for D
   from 1 to 3).
4. The live gate's injected freeze, the peer kill, and the drive core
   tests of every bound. The hold spike (LR-S2) shows no extra VBlank
   across a hold.
5. The live gate's race 1, and the drive core tests of the finish grace
   (LR-S8).
6. The drive core test of the START mask and of pad normalization, and an
   isolation pin that committed pads are installed only through the
   normalizing mapping.
7. Isolation tests (section 5), the unchanged default-boot pins
   (main_arcade_link_hook_isolation), and the stale-bundle rematch tests
   (LR-S6).
8. The gate's recorded run in LR-S13.
9. By construction: the setup plan's TWO_CAB shape and the retail split
   screen (section 2.6), unchanged by any slice; LR-S13 records one frame
   capture per cabinet in race 1, kept under build-msvc-x86 and never
   committed (retail imagery).

## 4. Decided design (defaults LR-1..LR-50; LR-17 is the owner's ruling)

The owner reviewed these defaults on 2026-09-25. LR-1..LR-16 stand as
written, except that LR-18, the finish grace, amends LR-1, LR-12, LR-13,
and LR-16, and LR-17's ruling updates the wording of LR-1, LR-10, and
LR-15. LR-17 is ruled (a). LR-3's D is accepted pending a feel test on
the physical cabinets (LR-3). Before that, the plan review changed
several defaults; "Review changes" at the end of this section lists what
changed, and "Owner decisions (2026-09-25)" after it lists the owner's
decisions. LR-19..LR-27 were added by LR-S4, LR-28..LR-32 by LR-S5,
LR-33..LR-36 by LR-S6, LR-37..LR-40 by LR-S7, LR-41..LR-48 by LR-S8,
and LR-49..LR-50 by LR-S9; each records the mechanics its slice settled.

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
driver only through new NativeArcadeLinkHost_* calls, as Task 7 did:
RaceBegin on the Launch frame and RaceEnd on the Disarm frame (LR-S3, the
pacing switch only at first), RaceStep once per race tick and RaceHold
while held (LR-S9). The host header forward-declares the canonical state
it receives, as it does NativeMatchConfigV1, so its include allow-list does
not change. Platform code therefore cannot read gGT. Every game fact the
drive needs is read by the game-side caller and passed in as
pointer-free values: END_OF_RACE; the finished-human count and the human
count, for the finish grace (LR-18); and in CTR_INTERNAL builds the
autopilot's steering facts (LR-16).

Two new game modules have neutral names:

- MainArcadeRaceDigest projects V4 (LR-10). It is read-only and lease-free
  (isolation test main_arcade_race_digest_isolation, LR-S4). The drivers
  extraction it calls reads NavHeader.last for each bot, check-only. The
  owner ruled that read allowed (LR-17, ruled (a)).
- MainArcadeRaceHold runs the hold loop and its banner (LR-9). It never
  calls VSync and never writes simulation state (isolation test
  main_arcade_race_hold_isolation, LR-S2).

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

1. records the V4 digests of frame k (LR-10), which also compares any
   peer digest of frame k parked earlier (LR-11);
2. submits the local sample for frame k, consumed at frame k + D (LR-4);
3. sends bundles (LR-3);
4. polls the link;
5. takes frame k and installs its pads (LR-5).

The adapter's Tick has already drained the link earlier in the same pass
(section 2.4). A peer that leads can have sent digests for frames up to
k + D - 1 by then; the session parks them (LR-11).

The recording point is mid-tick, but it is the same code point on both
cabinets.

LR-3 Input delay, sending, and resending. D is the adapter's default 2
(native_arcade_netplay.h:185), the same on both sides; the relink
handshake already opens the session with it. Sample to simulation is D + 1
= 3 ticks (100 ms at 30 Hz). docs/LOCKSTEP_MILESTONE.md:297 quotes "33 ms
at 60 Hz" for D = 2, but at the real 30 Hz loop D = 2 buffers 67 ms.

The owner accepted an input delay of "3 ticks" on 2026-09-25, pending a
feel test on the cabinets. That is exactly the figure above: 3 ticks from
sample to simulation, D + 1 with D = 2. D stays 2; it is not raised to 3.
The feel test belongs to the physical two-cabinet validation (HANDOFF
steps 6-7), which is out of Task 8's scope (section 1). Task 8's
one-machine gate cannot judge feel. If the test finds 3 ticks too slow,
D cannot go below 1 (NATIVE_LOCKSTEP_MIN_INPUT_DELAY,
include/platform/native_lockstep_input_window.h:21), and raising D is
bounded at 3 (risk 15).

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
call, a verbatim 128-byte bundle send (LR-S9). It requires both the link
mode and the session mode to be RUNNING. The link mode alone is not
enough: the link copies the session mode only in Poll and in the
staged-record replay (platform/native_lockstep_peer_link.c:108-120,
:140-141, :215-216), so a divergence that RecordLocalDigests latches
(LR-11) leaves the link RUNNING until the next poll.
ComposeAndSendBundle already sends nothing then, because the session's
ComposeBundle requires a RUNNING session (native_lockstep_session.c:354-358).

The window bounds D. A cabinet can lead the peer's consumed frame by
2D + 1:

- the peer is stalled at frame t and has composed t + D;
- we take up to t + D;
- we then compose t + 2D + 1 before our own take stalls.

That must stay below the window of 8. So the drive refuses a session with
D above 3 and fails locally (LR-12). With D = 2 the lead is 5.

LR-4 Local sample. A new platform seam in native_input.c samples the
cabinet's own device into caller-owned scratch storage. That is host
controller slot 0, where the cabinet's wheel, pad, and keyboard map by
default: controller, G29, and keyboard, as Platform_InputUpdate would read
them. It works while installed pads are active. It writes only the
caller's snapshot:

- never s_controllers[].snapshot, which is what
  Platform_InputCapturePadSnapshots copies into the V4 input domain
  (native_input.c:1155-1168) and what the installed path and the Apply
  calls write (:264-272, :1053-1063);
- never the pad bus;
- never s_lastActiveControllerSlot, which Platform_InputCaptureState
  saves (:1211).

So the Apply steps are refactored to take a snapshot pointer, and
Platform_InputUpdate keeps passing &s_controllers[slot].snapshot. In
CTR_INTERNAL builds the RL-15 autopilot can supply the sample instead
(LR-16). It never goes through installed pads.

The core normalizes every submitted sample, so only normalized bytes reach
the wire:

- connected 1 and status 0;
- id 0x73 (analog) if the device reported 0x73, else 0x41 (digital), so
  the pad bus never carries 0xff or any other id (section 2.3);
- START released;
- a disconnected device becomes the neutral connected pad: status 0, id
  0x41, buttons 0xffff, analog 0x80.

LR-5 Committed pads and the install frame. The committed inputs of frame k
are mapped to four snapshots by slot role:

- the CAB1_HUMAN slot's pad goes to retail pad 0 and CAB2_HUMAN to pad 1
  (NativeMatchConfigV1_FindRoleSlot);
- pads 2 and 3 are disconnected, as in the rehearsal and the roster
  proof's TWO_CAB profile;
- an all-zero pad (frames 0 to D - 1) becomes the neutral connected pad;
- every pad is normalized again as in LR-4 (connected 1, status 0, an
  allowed id, START released), whatever the peer sent.

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
- Every installed human pad is connected, with status 0 and an allowed
  id, so its pad-bus packet reads as a plugged pad and
  MainFrame_HaveAllPads never fails (MainFrame.c:428, :551-575).
- A retail gameMode1 pause bit is never used: it is canonical and would
  enter one side only.

The RL-13 vibration guard stays as it is, now unreachable.

LR-7 Deterministic VBlanks per tick. A linked race runs with fixed VBlank
pacing. The caller's RaceBegin turns it on, through the host glue, on the
Launch frame, before the race-track load and so before the RS-17 pins.
RaceEnd turns it off on the Disarm frame, the first idle main-menu frame
(RL-9). NativeArcadeLinkHost_Shutdown
(include/platform/native_arcade_link_host.h:281) turns it off before a
process exit. An Arm or Launch failure never turns it on. The off calls
(RaceEnd, Shutdown) are gated by the host's own flag (g_racePacing) and
touch only a pacing that RaceBegin turned on, so the host never switches
off the roster proof's pacing, and normal boot never calls the setter.
Shutdown is also reached mid-race, and then turns the race pacing off too:
from a replacing Configure (unit-tested) and from AbortToTitle's defensive
branch when the adapter fails to reinitialize. Both are harmless: the
race's link is gone, so that race cannot go on.

The switch becomes available outside CTR_INTERNAL: the setter's guards at
include/platform.h and native_platform.c go (done in LR-S3 (a); it is now
at include/platform.h:87 and native_platform.c:1070). It stays
host-local and invisible to game code.
tests/native_vblank_pacing_isolation_test.cmake changes from "main.c only"
to exactly two caller files, with game/ still never naming it:

- main.c, whose one call stays the roster proof's
  Platform_SetFixedVBlankPacing(1) (main.c:511);
- platform/native_arcade_link_host.c, whose calls are RaceBegin's (on),
  RaceEnd's (off), and Shutdown's (off).

Review required.

With fixed pacing and no other VBlank source in a race (section 2.1),
every race tick emits exactly 2 VBlanks, whatever the host frame time. So:

- frameTimer_Confetti and frameTimer_VsyncCallback advance by 2 per tick.
  Confetti starts from its RS-17 pin, and pause is impossible (LR-6).
- The root counter advances 526 units per tick, and elapsedTimeMS is 32
  from race tick 1 on. MainMain.c:231 pins it to 32 only while a load
  runs, and no load runs in the race. The pass before race tick 0 sees 32:
  the load set gameMode1_prevFrame to 1, and MainFrame.c:200-203 overrides
  the computed 64 (LR-8). Race tick 0 sees the value the VBlanks of that
  pass give; their count depends on the vsyncTillFlip the load left, not
  on host time, so it is the same on both cabinets. LR-S3 (b) logged
  elapsedTimeMS of the first three race GameLogic passes as 32 32 32 on
  both cabinets.

LR-8 Root-counter pin. The wrap of section 2.2 makes elapsedTimeMS a
function of the boot-relative sdata->rcntTotalUnits on one tick about every
273 s. The RS-17 pin is extended so both cabinets start the race on the
same counter phase. OnFinalizeInitBegin in LAUNCHED, next to the timer and
Confetti pins, also writes:

- sdata->rcntTotalUnits = 0;
- gGT->clockFrameStart = -200.

The first race GameLogic then sees no VBlank since the pin, a delta of
200 ms, which scales to 64 (MainFrame.c:188-199). Observed in LR-S3 (b):
both cabinets log rcntTotalUnits 0, 526, and 1052 and clockFrameStart 0,
100, and 200 at race ticks 0..2, after GameLogic and before that pass's
RenderVSYNC. Retail then stores 32
instead. The load set gameMode1_prevFrame to 1 (LOAD_TenStages.c:499), and
MainFrame.c:200-203 stores 32 after a paused previous frame. Settled in
LR-S3 (b): race ticks 0..2 log 32 32 32 with the pin on both cabinets and
in all eleven proof runs; without the pin, inferred from identical A/F
control digests (the control digest encodes elapsedTimeMS,
platform/native_canonical_state.c:40). The dangerous crossing then falls on the
same race tick on both cabinets, race tick 8,166 at 2 VBlanks per tick
(then 16,331; tests/game_timer_wrap_test.c). That tick assumes no VBlank
between the pin and race tick 0, as observed; N such VBlanks would move it
(8,165 for N = 1 or 2), so both cabinets must also agree on N, which the
link check now requires. Both run the same build (the
link requires a known identity), so both wrap the same way.

The only readers of the root counter are elapsedTimeMS and the render
statistic clockDurationStall (MainFrame_RenderFrame.c:1277, :1345). The
adapter reads both values back, as for the RS-17 pins, and the RS-17 audit
comment (MainArcadeRaceSetupCore.h:178-181) is corrected (done in LR-S3
(b); now :178-207). This is a setup seam change. Review required.

The pin applies to every launched race, including the roster proof's,
which launches through the same seam (MainArcadeRosterProof.c:289-296).
That is deliberate: scoping it to LINK would leave the proof's races on
boot-relative counter phases, the very thing the pin removes. What changes
for the proof:

- Its per-tick digests are expected not to change. Its first race
  GameLogic stores 32 after the load with or without the pin (the
  paused-previous-frame override above), and its 900-tick races end
  before the crossing both before and after the pin (section 2.2). LR-S3
  compares a pre-pin and a post-pin proof run tick by tick and records
  the result. Any change must be explained before an expectation moves.
  Settled in LR-S3 (b): the 900 tick lines of A and of F are
  byte-identical pre vs post.
- The pin readback (the two new fields) appears in the setup's log and
  the proof report (the seeded line, report v10), and
  tools/arcade-roster-proof-check.ps1's seeded-line pattern requires the
  pinned values.
- The C - A, E - A, I - F, and J - F counter offsets the check prints are
  unchanged: rcntTotalUnits and clockFrameStart are not among the four
  counters it compares.

LR-9 Stall hold. A take that stalls must not advance the simulation, and
no VBlank may fire until the take succeeds.

The mechanism is a blocking hold in the race caller at the hook, which is
before the VBlanks that would read the pads. RaceStep does not block: it
returns GO (pads to install), HOLD, or END. On HOLD the caller enters
MainArcadeRaceHold's loop, which never calls VSync. So no VBlank is
emitted, and no sound or input poll runs. Each iteration does exactly
this host work:

1. It pumps host events (Platform_PollHostEvents), so the window stays
   responsive and a quit still exits.
2. It calls RaceHold, which:
   - polls the link through the lobby (NativeLobbyState_Poll, as the
     adapter's Tick does at native_arcade_netplay.c:654), every
     iteration, so bundles and launch records are drained;
   - once per tick period of wall time, and only then: resends the kept
     bundles (LR-3); runs the adapter's launch intake and its launch send
     and linger tick (NativeArcadeNetplay_DriveLaunch and
     NativeArcadeNetplay_SendLaunch, :550-571 and :577-595) through one new
     adapter call; and calls OnTakeResult(STALL) if the take still stalls;
   - retries the take; a take that is not a STALL calls OnTakeResult with
     its result at once.
3. It sleeps about 1 ms through a new host-local platform wait that emits
   no VBlank (its isolation test: native_host_wait_isolation, LR-S2).

The hold does not run the flow, the menu input, or the rest of the
adapter's Tick, so no flow timer advances while held. The launch linger
must keep running: the start wait holds at race tick 0 for up to 900
periods, the launch records (RL-4) are sent only from the adapter
(section 2.4), and a peer that has not committed yet needs them to launch
at all (RL-7). The linger counts one tick per period, as it would have
counted game ticks.

On GO the loop returns, and the pass continues exactly as it would have.
Fixed pacing re-anchors the next VSync instead of catching up (LR-7).

Stall time is counted in tick periods of wall time (2 VBlank periods,
33.4 ms). The drive calls OnTakeResult(STALL) once per full period held,
not once per retry. So a stall of a few milliseconds costs a few
milliseconds and never counts toward the timeout. A take that succeeds
calls OnTakeResult(OK), which resets the count. No retail gameMode1 bit is
used.

Classifying a take. The drive always calls OnTakeResult first, so the
tracker, which checks DIVERGED and then FAULTED before any stall logic
(section 2.4), latches the real cause. A divergence can also latch
inside RecordLocalDigests, at step 1 of the tick (LR-11). The adapter's
own latch cannot see it on that tick: it acts only when the lobby is
PEER_LOST (native_arcade_netplay.c:683-691), and the lobby takes
PEER_LOST only from the link mode after a poll
(native_lobby_state.c:173-178). So when a record leaves the session
non-RUNNING, the drive calls OnTakeResult at once, sends and takes
nothing more, and ends the step; the outcome latches END_DESYNC on that
tick. After DIVERGED or FAULTED,
TakeFrameInputs returns REJECTED (native_lockstep_session.h:288-291); that
REJECTED is the latched outcome, not a local failure. A REJECTED take, or
a failed submit, record, or compose, is a local drive failure (LR-12) only
while the session is still RUNNING.

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

LR-S2 (a) result: the banner is drawn, but not in the arcade-link font.
DecalFont_DrawLine writes gGT->backBuffer's primitive memory and links into
the UI ordering table (game/DecalFont.c:169, :196-205), which is
render-pass state. So the banner is a host overlay instead:
Platform_PresentVRAMDisplayBanner runs Platform_PresentVRAMDisplay's
present and fills a black bar and the text, in a built-in 5x7 block font,
into the window framebuffer before the capture and the swap. The banner
draw touches only the window framebuffer: it writes neither VRAM nor the
ordering table. The pinned present it rides on does go through
Platform_BeginScene and NativeRenderer_BeginScene, which bind the main
render target and clear it or reload it from VRAM, as every
Platform_PresentVRAMDisplay does; the next DrawOTag rebuilds that target,
and VRAM is not written. LR-S11 settles the final style.

Note for LR-S9: Platform_PollHostEvents indirectly calls
Platform_InputControllerAdded/Removed and SubmitName_UseKeyboard, which
writes the game global kbCurr; this is harmless in a race, because kbCurr
is read only after the next VBlank poll.

LR-10 Live V4 projection. It is made on linked races only, once per race
tick, by the game module MainArcadeRaceDigest through
MainCanonicalRuntime_PrepareV4. MainArcadeRaceDigest is the runtime's one
live caller, and the "live gate" its isolation test waits for. It has two
callers of its own: the race caller (from LR-S10) and the roster proof,
which projects V4 through it (from LR-S4). It resets the runtime
(MainCanonicalRuntime_Reset) on race tick 0, so a workspace poisoned in an
earlier race (section 2.5) never carries into the next one, and
invalidates the topology context on the end frame, because a rematch loads
a level that can reuse addresses. The domains:

- Control. The MainMain.c:66-77 fields read live, with frameTimer
  (frameTimer_VsyncCallback) and frameCounter projected race-relative: the
  value minus its value at race tick 0, in unsigned 32-bit arithmetic. With
  LR-7 they read 2k and k on race tick k. A pacing fault would therefore
  show in the digest, where dropping the fields would hide it. timer is
  already race-relative through its pin. Every other field is level- or
  race-relative (MainArcadeRaceSetupCore.h:210-215).
- Retail RNG. The MainMain.c:80-84 fields.
- Deterministic bank. A copy of *MainArcadeRaceSetup_Bank(). It is
  constant during the race, so "project" means the RNG domain proves every
  tick that both cabinets hold the same post-setup bank. PrepareV4 takes
  it from the request instead of re-deriving a fresh one
  (MainCanonicalRuntime_StageBankV4, MainCanonicalRuntime.c:49-59, called
  at :337). The projector still checks masterSeed and the derivation
  version against the config (game/MAIN/MainCanonicalStateV4.c:107-108,
  the in-place projector the runtime calls). The dormant MainArcadeSetupV4
  boundary stays dormant: it re-plans the setup on every call.
- Input. The four snapshots GameLogic k read, captured with
  Platform_InputCapturePadSnapshots before this tick's install and frozen
  as V1 input.
- Drivers. The runtime's complete extraction, Physics included (RS-13).
  It uses the MainCanonicalTopology context (Capture and Validate) and no
  lease API. It does read NavHeader.last once per bot, in
  MainCanonicalDrivers_BotNavIndex (section 2.5). The owner ruled that
  read allowed on the live path, check-only (LR-17, ruled (a)), so this
  domain goes live.
- World. MainCanonicalWorldCounters_ExtractV1 and
  MainCanonicalWorldMineRegistry_ExtractV1, compiled into ctr_native
  through the unity chain, not linked (LR-22).
- Topology. Not compared in Task 8. MainArcadeRaceDigest supplies the
  unavailable summary, NativeCanonicalTopologyV1_Init's value (section
  2.5), on every tick. Its domain digest is the same constant on both
  cabinets and detects nothing (risk 17). A live summary would be a
  topology capture outside the lease (section 2.5), so MainArcadeRaceDigest
  itself names no topology fact reader, no NavHeader, and no lease API
  (the drivers extraction's bot read is inside MainCanonicalDrivers, and
  is the check-only read LR-17 ruled (a)). Live TOPOLOGY is deferred to a
  future lease-activation milestone that meets the AGENTS.md gate: a new
  live-cabinet gate, deterministic capture evidence, and review. It is
  not a Task 8 slice.
- Identity and frame. The build and content identity (the link requires a
  known identity), the agreed config's digest, and frameNumber = k.

The per-tick runtime lifecycle is fixed by the runtime:

1. MainCanonicalRuntime_BeginFrame. It is refused while a prepared state
   is outstanding or a frame is already active
   (MainCanonicalRuntime.c:111-117).
2. MainCanonicalRuntime_PrepareV4. Without an active frame it poisons the
   workspace with FRAME_STATE (MainCanonicalRuntime.c:252-256).
3. MainCanonicalRuntime_ViewV4 (MainCanonicalRuntime.h:162-164). The
   digests are recorded, or the state copied, from this view.
4. MainCanonicalRuntime_ReleaseV4 (MainCanonicalRuntime.c:401-411). It
   zeroes the workspace's state and ends the frame, so nothing may read
   the view after it. The header's rule is that a prepared state is
   released before another lifecycle mutation (MainCanonicalRuntime.h:118).

MainArcadeRaceDigest runs all four on every race tick, and a projection
failure at any step is a local drive failure (LR-12). No failure leaves
the frame open: a failed PrepareV4 poisons the workspace, which ends the
frame; a VIEW failure releases the prepared state, or resets the runtime
when there is no view; and a refused Release resets it.

The per-tick cost is measured with NativePerf in LR-S4 (risk 3).
Review required: this touches canonical state and the runtime's
authorization. The format does not change.

LR-11 Digest exchange and desync. RecordLocalDigests gets the frame k
state on every race tick. The bundle for frame k + D + 1, composed on race
tick k + 1, carries it (section 2.4). A mismatch latches DIVERGED, with the
frame and the domain mask. OnTakeResult or the adapter's own poll (section
2.4) turns it into END_DESYNC. A divergence latched by RecordLocalDigests
(a parked digest, below) reaches neither the link mode nor the adapter's
poll on that tick, so the drive calls OnTakeResult right after that
record (LR-9). The glue logs the divergence report once:

    arcade link: race <n> out of sync at race tick <v> domains <mask> local <hex16> remote <hex16>

A lead is not a desync. Before LR-S5 the session compared a digest on
arrival and latched FRAME_UNAVAILABLE when the frame was newer than its
last recorded one (section 2.4). But one cabinet leading the other by one
or more ticks is the normal state of a linked race under fixed pacing
(risk 4), and the adapter drains bundles before the caller records
(LR-2). So the session changes (LR-S5, Review required):

- A peer digest for a frame not yet recorded locally is parked, and
  RecordLocalDigests compares it when it records that frame. A mismatch
  latches exactly the divergence the on-arrival comparison would have:
  the same frame, sender, masks, and both digests.
- A digest for a frame still in the history is compared on arrival, as
  before LR-S5.
- FRAME_UNAVAILABLE stays only for a frame already retired from the
  history, or a frame at or below r (the last recorded frame) that
  recording skipped, and for a parked frame that recording skips.

The bound. A leader can take only the frames the other cabinet has sent.
Let r be the other cabinet's last recorded frame. It sent frame f on race
tick f - D, after recording f - D, so it has sent at most frames up to
r + D. The leader can take those, so it can be at most D + 1 ticks ahead:
it records r + D + 1 and stalls on the take of r + D + 1. The newest
digest it can have sent is the one its tick r + D + 1 composed, for frame
r + D. So at most D digests per peer are parked, for frames r + 1 to
r + D; with nothing recorded yet, none. (The review's D + 1 counts the
leader's ticks; the parked digests are one fewer because a bundle composed
on tick t carries frame t - 1.) The park holds
NATIVE_LOCKSTEP_MAX_INPUT_DELAY entries per peer.

The window caps that bound. The digest of frame r + j travels in the
bundle for frame r + j + D + 1, and the receiver's window accepts only
[c, c + 8), where c, its consumed frame, is r before its take of r and
r + 1 after it (native_lockstep_input_window.c:30-34; above the window
is a WINDOW_OVERRUN fault, :73-80). So the leader's newest bundle at
the full lead, for frame r + 2D + 1, is accepted only for D up to 3.
For D from 4 to 6, which the session accepts but the drive refuses
(LR-3), at most 7 - D digests can be parked after the take of r
(6 - D before it), and the full lead faults the receiver with
WINDOW_OVERRUN. So "at most D" holds, and is reached, only for D up to
3.

A digest beyond the bound, for a frame after r + D, is impossible from a
conforming peer: the decoder already pins the peer's D and the digest lag
(native_lockstep_protocol.c:293-295, :308-312). The window can still
accept such a record (a forged one). The first such digest, for frame
r + D + 1, travels in the bundle for frame r + 2D + 2. That is inside
the window for D up to 2 at either consumed frame, and for D = 3 only
after the take of r (r + 8 is below r + 1 + 8); for D of 4 or more it
is a WINDOW_OVERRUN instead. The session latches it as a protocol
FAULT with a new, appended local cause,
NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD. The cause
is not on the wire, and the bundle does not change. This replaces the
"never simulated" half of the lockstep milestone's FRAME_UNAVAILABLE rule
(docs/LOCKSTEP_MILESTONE.md:463-466, :698-699), and LR-S14 updates that
prose.

The finish. On F the drive records F and composes nothing new (LR-13), so
the digests of F - 1 and F are never carried on the wire: the last two
frames, whatever D is, because a bundle composed on tick t carries frame
t - 1. The live gate compares every per-tick digest offline instead
(LR-16).

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
    desync only in F - 1 or F     may go undetected in        RACE COMPLETE on the side that
                                  band (below)                finishes on F (RACE OUT OF SYNC
                                                              if the other leads); RACE
                                                              COMPLETE or OPPONENT
                                                              DISCONNECTED on the other
    protocol fault                the record that faults      LINK ERROR (LINK_ERROR)
    local drive failure           the tick it happens         LINK ERROR (RL-11 path)
    finish grace (LR-18)          900 race ticks after the    RACE COMPLETE (FINISHED)
                                  first human finish (3-4
                                  humans: after all but one);
                                  about 30.1 s plus any holds
    race-length bound (backstop)  18000 ticks (600 s)         RACE COMPLETE (FINISHED)

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
  wait is 900 ticks (30 s). The hold keeps sending the launch linger's
  records through it (LR-9), so a peer that has not committed yet can
  still launch. A one-sided launch (RL-7), or a peer that failed locally
  (RL-11), ends here.
- Desync. Detected when the digest arrives or, for a parked digest, when
  the frame is recorded (LR-11). Either way it is before the detecting
  cabinet takes frame x + D + 1, because that take needs the bundle that
  carries frame x.
- A divergence visible only in frames F - 1 or F. F is the drive's end
  tick, whichever of END_OF_RACE, the finish grace, or the race-length
  bound ended it (LR-18); the cases below name END_OF_RACE, and read the
  same for any end on F. A cabinet that finishes on F never sends its
  digests of F - 1 and F (LR-11, "The finish"). The outcomes:
  - Both cabinets see END_OF_RACE on F. Both show RACE COMPLETE. Nothing
    in band detects it; only the live gate's offline per-tick comparison
    does (LR-16).
  - Only cabinet A finishes on F. A shows RACE COMPLETE, unless the next
    case applies. A's last bundle,
    composed on its tick F - 1, is for frame F + D - 1 and carries A's
    digest of F - 2, so B never sees A's digests of F - 1 or F and never
    detects the divergence. B takes up to frame F + D - 1. If B's own
    END_OF_RACE comes by its tick F + D, B also shows RACE COMPLETE;
    otherwise B stalls on frame F + D and shows OPPONENT DISCONNECTED
    after the stall timeout.
  - B, which did not finish on F, does send its own digest of F - 1, in
    the bundle it composes on its tick F. If B leads A, that bundle can
    reach A before A's finish is on the flow: parked and compared at A's
    record of F - 1, compared on arrival, or drained by the adapter Tick
    of pass F + 1. That Tick is the one that sees A's finish (the race
    caller reports it after pass F's Tick, section 2.1), and there a link
    failure outranks a same-tick finish (native_arcade_flow.c:278-301).
    In each case A shows RACE OUT OF SYNC. Once A's flow is on RESULTS a
    later latch changes nothing: the adapter turns a latched cause into
    the flow's end only while RACING (native_arcade_netplay.c:683-686).
  So the in-band result of such a divergence is RACE COMPLETE on the side
  that finishes on F, or RACE OUT OF SYNC there when the other side leads,
  and RACE COMPLETE or OPPONENT DISCONNECTED on the other side. With the
  cabinets in step and B's finish more than D ticks after F, it is RACE
  COMPLETE on one side and OPPONENT DISCONNECTED on the other. It is
  fail-safe, as risk 6 is.
- Peer drop. It ends the race for the survivor straight to RESULTS; the
  survivor does not race on against bots. The peer-drop roster is still
  applied for the record (native_arcade_netplay.c:600-610).
- A local drive failure. Examples: a V4 projection, record, or compose
  failure while the session is RUNNING; a REJECTED take while the session
  is RUNNING (LR-9 classifies the others); D above 3. It goes through
  NativeArcadeLinkHost_ReportRaceFailure, and the log names it.

The race-length bound is deterministic: both cabinets reach it on the same
tick, so both end FINISHED. The log names "race tick limit".

The finish grace (LR-18) is deterministic in the same way: its end tick
follows from simulation state, so both cabinets reach it on the same tick
when the simulations agree, and both end FINISHED. The log names "finish
grace". The race-length bound stays as the backstop for a race in which
no human finishes.

The only new player-visible text is the hold banner, WAITING FOR OPPONENT
(the wording of MainArcadeLinkLayout.c:362).

LR-13 Race finish and the finish linger. The drive's natural finish is
the first race tick F whose hook sees END_OF_RACE. The game-side caller
reads it and passes it to RaceStep (LR-1). END_OF_RACE is set by the lap
stats of pass F - 1 (section 2.6), so F is the same tick on both cabinets
when the simulations agree. The finish grace's end (LR-18) is also an F:
F is END_OF_RACE or the grace end, whichever comes first, and the drive
handles both as this default describes. On F the drive records the
digest, composes and takes nothing, and ends. The caller reports the
finish (the core's raceFinishedInput latch). It installs neutral pads
from F until the clear, as today (answers RACE_LAUNCH risk 3). The
retail end-of-race screens that run until the return load are host-local
presentation and are not compared.

After the finish the drive keeps resending its kept bundles for
finishLingerTicks = 15 ticks (0.5 s), so a peer that lost our last bundles
can still reach F. It stops earlier when the link closes, the session
leaves RUNNING, or the flow leaves RESULTS. It sends nothing after a
failure end, including a divergence latched inside RecordLocalDigests:
the drive ends on that tick (LR-9), and the verbatim send checks the
session mode, not only the link mode (LR-3). The linger overlaps
the return load, whose synchronous reads pause ticks, so 15 ticks is not a
wall-time bound; LR-14 does not rely on it.

The race-length bound (LR-12) is 18000 ticks: seven laps of the longest
track fit. A kart parked for good cannot hold a cabinet forever. The
retail race ends only when both humans have finished (section 2.6), but a
human who never finishes no longer runs the race to this bound: the
finish grace ends it 900 ticks after the first finish (LR-18). The
18000-tick bound remains the backstop for a race in which no human
finishes.

LR-14 Stale bundles after a rematch (GAME_LOOP_UI risks 2 and 10). A
bundle of a finished match can reach the rematch link. Confirming REMATCH
opens a new lobby and link on the same port, and records that arrive while
the new link handshakes are staged and replayed into the new session,
where today an old record is a MATCH_IDENTITY fault (section 2.4).

No timing argument prevents it. The two cabinets can end a race
differently: one is on RESULTS after a DESYNC, a local drive failure, or a
pre-race RL-11 failure while the other is still held. The held one keeps
resending for up to 90 periods, or 900 in the start wait, while the first
may confirm REMATCH once the 30-tick dwell ends. And the finish linger
overlaps the return load (LR-13), so even the clean finish has no
wall-time bound. The live gate's race 2 to race 3 transition, a DESYNC
followed by REMATCH, reaches this.

After a pre-race failure the timings rarely meet in practice. The held
peer's start wait is 900 periods (30 s, LR-12), longer than the rematch
wait's rematchWaitTimeoutTicks = 300 (10 s, section 2.4). So in real
timings the side that confirms REMATCH usually times out to OPPONENT
LEFT before the held peer reaches RESULTS and can confirm. That is
bounded and fail-safe. The drop still covers the case in which the held
peer does confirm within the other side's rematch wait, and
TestRematchAfterPreRaceFailureDropsStaleBundles schedules exactly that
case (below).

Decision. The peer link drops and counts any 128-byte record whose match
identity is not the current session's, both when it replays staged
records and while RUNNING, instead of handing it to the session (LR-S6,
Review required):

- The link decodes each record against the session's identity, protocol
  version, and D. A record whose first decode failure is MATCH_IDENTITY
  is dropped and counted in a new foreign-bundle drop counter, exposed
  like NativeLockstepPeerLink_DroppedEarlyBundleCount. The decoder
  returns only the first failure. It checks the record's own digest
  (native_lockstep_protocol.c:281-283) before the identity (:285-287), so
  a corrupt record, of either identity, is a BAD_DIGEST fault, reaches the
  session, and still faults. The checks after the identity (:289-312:
  protocol version, D, the record's slots and pads, the digest lag) never
  run on a foreign record; that is harmless, because it is dropped and
  never reaches the session.
- Every other record goes to AcceptBundle unchanged, so every other fault
  still latches.

Why dropping is safe:

- The match identity is the agreed config's digest
  (native_lockstep_session.c:240). Both cabinets already proved they hold
  that config, through the lobby handshake and the launch agreement's
  config digest (RL-1). A record with another identity can only belong to
  another match. It is never a disagreement about this one.
- A peer that only ever sends stale records still ends bounded. In a race
  its missing frames are a stall, and the stall timeout ends it. In the
  rematch wait, rematchWaitTimeoutTicks ends it (section 2.4).
- Stale records can fill the 8-entry staging buffer, and later early
  records are then dropped and counted (GAP 4). The verbatim resend
  (LR-3) delivers those again once the link is RUNNING.

Tests:

- tests/native_lockstep_peer_link_test.c (native_lockstep_peer_link_unit):
  TestForeignIdentityDroppedWhileStaging,
  TestForeignIdentityDroppedWhileRunning,
  TestForeignIdentityCorruptStillFaults, and
  TestCurrentIdentityBadDelayOrSlotStillFaults.
  TestForeignIdentityCorruptStillFaults exercises the drop path's edge: a
  foreign-identity record with one corrupted byte, and a record whose
  identity bytes are flipped without recomputing its digest, must each
  fault BAD_DIGEST, not be dropped.
  TestCurrentIdentityBadDelayOrSlotStillFaults: a current-identity record
  with a wrong D (INPUT_DELAY, native_lockstep_protocol.c:293-295) and
  one with a wrong sender slot (BAD_SLOT) must still fault.
- tests/native_arcade_netplay_test.c (native_arcade_netplay_unit), each a
  loopback pair where one side keeps sending the old match's bundles while
  the other confirms REMATCH, then both must reach READY on the new match
  with no fault and a nonzero drop count:
  TestRematchAfterDesyncDropsStaleBundles,
  TestRematchAfterPreRaceFailureDropsStaleBundles, and
  TestRematchDuringFinishLingerDropsStaleBundles.
  TestRematchAfterPreRaceFailureDropsStaleBundles cannot use the real
  900-period start wait, which outlasts the 300-tick rematch wait (above).
  It ends the held side's stale sending and moves it to RESULTS and
  REMATCH well inside the other side's 300 ticks, and asserts that the
  other side's rematch wait has not expired when both reach READY.
- A drive core case (native_arcade_race_drive_unit): nothing is sent after
  the linger or off RESULTS.
- The live gate's race 1 to race 2 and race 2 to race 3 transitions.

LR-15 Host-local state. The drive state, the kept bundles, the parked
digests, the pacing flag, the hold state, the finish-grace state (the
grace start tick G and its countdown, LR-18), the sample seam, and the
banner are host-local. None of them is in a checkpoint, in replay, or in
canonical state, and none touches the topology lease: no acquire,
activate, capture, or publish, no lease owner anywhere new, and no hook on
LOAD_Hub_ReadFile. MainArcadeRaceDigest reads no topology facts: it
supplies the unavailable summary (LR-10). The one lease question, the
Drivers extraction's NavHeader.last read, is ruled (a) by the owner
(LR-17): a check-only read, never written, and never used to acquire,
activate, capture, or publish the lease.
The V4 state is projected and digested, never serialized or replayed.

Default boot and replay are unchanged unless the host is in LINK mode with
a launched race. The link and replay options already exclude each other
(GAME_LOOP_UI risk 7), and the caller's first statement stays the dormant
return (MainArcadeRaceLaunch.c:374-378).

The roster proof is not unchanged. It changes on purpose, in three slices:
run K and the hold module (LR-S2), the LR-8 pin, which reaches it through
the shared setup seam (LR-S3), and its V4 lines through
MainArcadeRaceDigest (LR-S4). LR-8 lists which proof expectations shift.

LR-16 One-machine proof. The RL-15 gate arcade_link_launch is extended. It
keeps its name, its ports (cab1 7001, cab2 7002), RUN_SERIAL, the label
live, TIMEOUT 900, and its skip rules. It becomes one run of three races,
each driven in lockstep:

- Race 1, the finish. The select defaults give the fixture's Crash Cove
  and 3 laps (include/platform/native_arcade_link_options.h:57-58). Each
  autopilot drives its own human. At race tick 600, cab2 freezes itself
  for 45 periods (1.5 s): it sends and takes nothing. cab1 runs on to the
  last frame cab2 sent, D + 1 ticks ahead of cab2, and holds: a
  stall-then-resume on cab1. When cab2 resumes, it drains cab1's bundles,
  whose digests run up to D frames ahead of its own record; the session
  parks them (LR-11) and must report no divergence. cab1 then keeps a
  lead of up to D + 1 ticks. Both must end the race on the same race
  tick, with the same end kind, and show RACE COMPLETE. END_OF_RACE is
  expected: in the LR-S2 (b) seeds the two players' finishes were at most
  107 ticks apart, far inside the 900-tick finish grace (LR-18). The
  checker still accepts a finish-grace end on both cabinets, and records
  which end happened. A "race tick limit" end in race 1 fails the check:
  the natural finish or the grace is required. A grace end passes, but it
  leaves the natural END_OF_RACE path unproven live in that run (risk 1).
- Race 2, the desync, after REMATCH. At race tick 300, cab2 XORs bit 0
  into domainDigests[0], the CONTROL domain digest, of the copy of the V4
  state it passes to RecordLocalDigests. combinedDigest is left alone, and
  RecordLocalDigests' Validate does not recheck digests (section 2.5). The
  simulations stay identical, but cab2's recorded and sent digest of frame
  300 now differs. Expected: divergence mask
  NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN (2) without COMBINED, and
  canonicalDomainMask 0x1 (CONTROL), so the log line reads "domains 0x1".
  At least one cabinet must show RACE OUT OF SYNC naming frame 300. The
  other must show RACE OUT OF SYNC or OPPONENT DISCONNECTED (risk 7).
- Race 3, the peer drop, after REMATCH. The rematch after a DESYNC is the
  LR-14 case: one cabinet may still be sending race 2's bundles when the
  other's new link comes up, and those are dropped. The checker requires
  both reports to show race 3 VALIDATED with no LINK ERROR. It then kills
  cab2 once cab2's log reports race 3 tick 300. cab1 must show OPPONENT
  DISCONNECTED within 90 periods, plus a margin, of its last taken frame.
  It then takes EXIT and exits 0 with PASS. cab2's kill exit is expected.

The checks, beyond today's (the agreed-match line and the config, plan,
bots, and bank digests of the k-th race equal across the two, and each
race's config different from the one before):

- each report logs one line per race tick with the combined V4 digest and
  the domain digests;
- for every tick both logged, the lines are equal across the two
  processes, the uncarried last two frames included;
- race 1's end tick and end kind are equal, and the kind is END_OF_RACE
  or finish grace, never race tick limit;
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
- Reading them is reading level topology, by risk 17's own definition.
  The read is non-canonical and internal-only, and it is not a lease
  capture: the race caller reads gGT->level1->ptr_restart_points only
  inside its CTR_INTERNAL guards, and only to form the autopilot's
  steering facts. No restart-point value enters the V4 state, a digest,
  or the wire; only the autopilot's pad does, as the local sample.
  MainArcadeRaceDigest never names ptr_restart_points. Both rules are
  pinned: main_arcade_race_digest_isolation (LR-S4) bans the token from
  the digest module, and main_arcade_link_hook_isolation (LR-S10) allows
  it in the race caller only inside the CTR_INTERNAL block.
- The steering decision is pure (platform/native_arcade_link_autopilot.c),
  over pointer-free facts. Platform code cannot read gGT (LR-1), so the
  game-side race caller reads the facts (its kart's position and heading,
  and the next restart point) and passes them to the host with RaceStep.
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
rehearsals are 10 s. Race 1 was measured in LR-S2 (b); the rest are
estimates, to be recorded in LR-S13:

- race 1: the limit is 6000 race ticks (200.6 s of wall time at the real
  33435 us tick period). The measured typical length is about 3,760
  ticks: 5 seeds of the proof's Crash Cove race reached END_OF_RACE at
  3,588 to 3,753 race ticks (120.0 to 125.5 s of wall time; 119.6 to
  125.1 s at a nominal 30 Hz), set by the slower of the two autopilots,
  since both must finish. The typical length is an estimate for the
  totals below, not a basis for a cap or a timeout: the gate's seeds come
  from the select, not from these 5;
- race 2: about 10 s to the desync;
- race 3: about 13 s to the timeout;
- a third select, load, and RESULTS: about 25 s.

That puts the gate near 245 s, so it adds about 165 s. The full suite
goes from about 450 s to about 615 s. `ctest -LE live` does not change.
A race 1 that runs to the 6000-tick limit puts the gate near 320 s,
still inside TIMEOUT 900. The finish grace does not change the budget:
it can only end race 1 earlier. The 6000-tick cap still bounds race 1,
so the budget is unchanged.

LR-17 Lease ruling on the bot nav-index read (ruled (a) by the owner,
2026-09-25). This was not a default the plan could decide. AGENTS.md
holds the topology lease retire-only and requires a new live-cabinet
gate, deterministic capture evidence, and review before any acquire,
activate, capture, or publish. Whether this read is one of those was
therefore the owner's call. It was recorded here as an open ruling with
a recommended option, and LR-S4 and every slice that consumes live
Drivers digests (LR-S10, LR-S12, LR-S13) waited for it. The owner ruled
(a) on 2026-09-25; the ruling's terms are below the options.

The fact. The live Drivers extraction reads NavHeader.last outside the
lease. MainCanonicalRuntime_PrepareV4 calls
MainCanonicalDrivers_ExtractCompleteFromPreludeInPlace
(MainCanonicalRuntime.c:296), which calls
MainCanonicalDrivers_ExtractMetaAndBot for every present driver
(game/MAIN/MainCanonicalDrivers.c:953-954), which calls
MainCanonicalDrivers_BotNavIndex for every bot (:834). BotNavIndex reads
sourceData->NavPath_ptrHeader, gGT->level1->LevNavTable, and the NavFrame
array (:720-725) and dereferences header->last (:726). A TWO_CAB race has
four bots (section 2.5), so the live projection would make this read on
every race tick. It contradicts
MainCanonicalTopologyLeaseAuthority.h:72-75, which calls ObservePostInit
"the sole API that observes NavHeader.last". The CTR_INTERNAL roster
proof already makes the same read live (MainArcadeRosterProof.c:579):
that is a precedent, not a ruling. What
is not in question: the MainCanonicalTopology context the extraction
validates is not the lease (it reads the level's mesh and quad-block
array, MainCanonicalTopology.c:57-64, and the runtime may not name the
lease, tests/main_canonical_runtime_isolation_test.cmake:18-47), and the
unavailable TOPOLOGY summary (LR-10) is sound either way.

The options:

- (a), taken. The owner rules that MainCanonicalDrivers_BotNavIndex's
  read is a validation-only read of a driver's own nav pointer, not an
  acquire, activate, capture, or publish of the topology lease. It checks
  that the bot's botNavFrame lies inside its own path's frame array and
  turns it into an index; it copies no topology fact out. The design is
  kept, and LR-S4 also:
  - corrects the "sole API" comment
    (MainCanonicalTopologyLeaseAuthority.h:72-75) and the runtime's
    comments (MainCanonicalRuntime.h:137-140,
    MainCanonicalRuntime.c:309-311) to name both readers;
  - adds an isolation pin that the only readers of a NavHeader's last
    among the first-party game/MAIN/MainCanonical* and
    game/MAIN/MainArcade* sources are
    MainCanonicalTopologyLease_ObservePostInit
    (MainCanonicalTopologyLeaseAuthority.c:190, :196) and
    MainCanonicalDrivers_BotNavIndex (MainCanonicalDrivers.c:726). The
    retail bot code's own reads (for example game/BOTS.c:643) are
    simulation and outside the pin, and the layout contract's
    offsetof(struct NavHeader, last) assert
    (MainCanonicalTopologyLayoutContract.h:108) is not a read. LR-S4
    settles the mechanics; one workable form checks every function body
    in those .c files that names struct NavHeader and allows ->last only
    in those two.
- (b), not taken. Any NavHeader.last read is lease-only. Then the
  Drivers domain cannot go live in Task 8 unless its projection leaves
  out the bot nav index: a V4 path that never calls BotNavIndex, with the
  roster proof's precedent read ruled on as well. That loses desync
  coverage. A divergence in a bot's botNavFrame (its place on its nav
  path) is no longer compared directly; it shows only later, through the
  bot's position, velocity, and estimate fields in DRIVERS, so the report
  names a later frame. The projector also stops proving that each bot's
  nav pointer lies inside its path. Whether an omitted index fits the
  unchanged V4 schema (section 5) is itself unproven. This is a design
  change: LR-10, LR-S4, and the roster proof's V4 lines would be
  re-planned, and the plan returns for review.

Recommended: (a). It keeps the design and matches the roster proof's
existing read. It was the owner's call, not this plan's, and the owner
ruled (a) on 2026-09-25. LR-S5 and LR-S6 never depended on it.

The ruling's terms. MainCanonicalDrivers_BotNavIndex may read
NavHeader.last check-only on the live path, as the roster proof already
does. The read is never written, and never used to acquire, activate,
capture, or publish the lease. LR-S4 carries option (a)'s comment fixes
and isolation pin. LR-S4, LR-S10, LR-S12, and LR-S13 are unblocked.

main_arcade_race_digest_isolation (LR-S4) must not be able to pass while
the ruling is broken. It carries (a)'s reader pin above and requires the
corrected comments, so it fails on today's tree (the "sole API" text).
Under (b), not taken, it would instead have pinned that ObservePostInit
is the only such reader and that the V4 path reaches no BotNavIndex
call.

LR-18 Finish grace. The owner's race-end rule (2026-09-25): the race ends
when every human has finished, or 30 s after the first human finishes,
whichever comes first. With 3 or 4 humans the 30 s starts when all but
one have finished. The 18000-tick race-length bound stays as a backstop.
This default is the rule's mechanism. It amends LR-1 (the caller passes
the counts), LR-12 (the end row), LR-13 (F), and LR-16 (race 1 accepts a
grace end).

The facts. Retail marks a finished driver with ACTION_RACE_FINISHED
(0x2000000, include/namespace_Vehicle.h:607), set by the lap stats at
game/PlayLevel.c:180. The ARCADE_MODE end rule is game/PlayLevel.c:437,
humanPlayerCount <= finishedHumanCount, where humanPlayerCount is
gGT->numPlyrCurrGame (:422); it then calls MainGameEnd_Initialize
(:476). Retail VS mode already ends at "all but one" (:433), the shape of
the owner's rule. PlayLevel_UpdateLapStats runs after the hook
(MainFrame_RenderFrame.c:243, :246; the hook is :90), so, as with
END_OF_RACE, the hook sees a tick's finish bits on the next pass.

The count. The game-side race caller counts the humans, drivers 0 to
numPlyrCurrGame - 1, whose actionsFlagSet has ACTION_RACE_FINISHED. It
passes that count and the human count to RaceStep as pointer-free values,
as it passes END_OF_RACE (LR-1). Platform code still cannot read gGT.

A finished human is also a bot. Retail converts a finished human with
BOTS_Driver_Convert (game/PlayLevel.c:226), which sets ACTION_BOT, and
the drivers extraction derives each driver's kind from ACTION_BOT every
tick (game/MAIN/MainCanonicalDrivers.c:385). So from its finish until
the end, up to 900 ticks, a finished human is projected as a BOT and
goes through MainCanonicalDrivers_BotNavIndex (:834), the check-only
read LR-17 rules on. The count is by slot, 0 to numPlyrCurrGame - 1, not
by kind, so a converted human still counts as a finished human.

The grace. It starts on the first race tick G whose hook sees
finished >= max(1, humans - 1). With 2 humans (TWO_CAB, all of Task 8)
that is the first human finish. finishGraceTicks = 900 race ticks. The
owner's 30 s is race time, not wall time: 900 ticks are about 30.1 s at
the real 33435 us tick period (LR-16), plus any holds, which pause race
ticks (as LR-13 says of the linger's ticks).

The end. The drive ends on the first of:

- END_OF_RACE seen, on tick F (LR-13): the natural finish;
- tick G + 900: the grace end;
- the 18000-tick race-length bound (LR-12).

When more than one falls on the same tick, the end kind is the first in
this order: END_OF_RACE, then the finish grace, then the race tick
limit. The internal option of LR-S10 that lowers the bound to a short
cap counts as the race tick limit. All three end FINISHED, and RESULTS
shows RACE COMPLETE. The log names "finish grace" for the grace end, as
it names "race tick limit" for the bound.

The grace end is deterministic. The finish bits come from the
simulation, and they are in the DRIVERS domain through actionsFlagSet,
which the drivers extraction projects with only bit 0x04000000 masked
(game/MAIN/MainCanonicalDrivers.c:830). So G and G + 900 are the same
tick on both cabinets when the simulations agree.

The grace-end tick is handled like F in LR-13. The drive records that
tick's digest, composes and takes nothing, ends, and lingers for
finishLingerTicks = 15. The caller reports the finish and installs
neutral pads from that tick until the clear. LR-11's "The finish" and
LR-12's F - 1 / F analysis apply to the drive's end tick, whichever of
the three ended it.

The safer choice, recorded. The drive writes no retail state. It does
not force END_OF_RACE or ACTION_RACE_FINISHED on the unfinished human,
and it does not call MainGameEnd_Initialize. The retail simulation stays
untouched (LR-1: the only retail-state change is the LR-8 pin). The
consequence: at a grace end the unfinished player gets no retail
end-of-race screen. The flow goes to RESULTS RACE COMPLETE over the
still-running race level, then the return load, as for the race-length
bound.

The rule is pinned structurally. main_arcade_link_hook_isolation (LR-S10)
checks that the race caller (game/MAIN/MainArcadeRaceLaunch.c) never
writes actionsFlagSet or gameMode1, never names MainGameEnd_Initialize,
and names ACTION_RACE_FINISHED only in a read. The count itself has a
unit case (LR-S10).

LR-19..LR-27 record the mechanics LR-S4 settled for LR-10. Each takes the
safer option. None changes the canonical-state schema, the replay format,
or any existing V4 byte.

LR-19 Bank in the request. MainCanonicalRuntimeV4Request gains a last
field, bank. NULL keeps the old fresh derivation, so every existing
caller is unchanged. Non-NULL is copied into the state instead. The
pointer is part of the transaction token: ViewV4, GetSubmissionV4, and
ReleaseV4 refuse a request with another bank pointer. The projector still
checks the bank's masterSeed and derivation version against the config.
A mismatch fails PrepareV4 with reason PROJECT.

LR-20 Digest API and sequence. MainArcadeRaceDigest_Project(raceTick,
sources, out) takes an explicit race tick and a sources struct of const
pointers. It returns a pointer-free tick: frameNumber, combined digest,
and domain digests in NativeCanonicalDomainOrder. Race tick 0 resets the
runtime and the module and starts a race. Every later call must be the
next tick, with the same config bytes. Any failure is latched, with its
cause and the runtime's reason, until the next race tick 0, and every
call in between fails. MainArcadeRaceDigest_EndRace on the end frame
invalidates the runtime's topology context. It fails after a latched
failure or with no race. The module copies no state out: the V4 state
copy for RecordLocalDigests, if LR-11 needs one, is LR-S10's.

LR-21 Storage. The digest state (request, bank copy, extracted values,
base) is one file-scope static, and the runtime workspace is its own
file-scope global (MainCanonicalRuntime_Global). There is no heap. The
state is not in any saved state, recording, or canonical state.

LR-22 World extractors in the unity chain. ctr_native compiles
MainCanonicalWorldCounters.c and MainCanonicalWorldMineRegistry.c
through game/game_unity.h. It does not link their libraries: a pulled
library member would define sdata a second time (LNK2005 against
main.obj). The libraries remain for their own unit tests. The digest
isolation test pins the unity includes and bans the two libraries from
ctr_native's link line. This replaces the "newly linked into
ctr_native" of LR-10's first draft; LR-10 and the LR-S4 plan now point
here.

LR-23 Sources from the caller. The caller passes gGT, sdata, the mine
pool (&D231), the config, the bank (MainArcadeRaceSetup_Bank()), and the
frozen V1 input of the tick. The module names no game global, so its
reads are exactly its sources.

LR-24 Topology check. On every tick the module compares the view's
TOPOLOGY value with the unavailable summary, after Release, and fails
TOPOLOGY on any difference. Its digest, d75d92ae427cd357, is pinned in
main_arcade_race_digest_unit and in tools/arcade-roster-proof-check.ps1,
and the isolation test keeps the two equal. So a live topology cannot
arrive unnoticed.

LR-25 Cost bucket. The whole projection runs inside one NativePerf
scope, arcade_race_digest_ms. It is a no-op outside CTR_INTERNAL.
NativePerf buckets are independent, so the race caller (LR-S10) must not
call MainArcadeRaceDigest_Project inside the GAME_LOGIC scope, or its
time is counted twice.

LR-26 Report v11. Each tick line appends v4, v4control, v4rng, v4input,
v4drivers, v4world, and v4topology, 16 hex digits each. A projection or
end-frame failure ends the proof with the new result V4_FAILED (34).
The domain-to-field mapping is in the platform module
(NativeArcadeRosterProof_SetTickLineV4), because
main_arcade_race_setup_isolation bans topology tokens in the game hook.

LR-27 The LR-17 pin. main_arcade_race_digest_isolation splits every
game/MAIN/MainCanonical* and game/MAIN/MainArcade* source (comments
removed, literals blanked) into top-level brace units. A unit that
names a nav header (NavHeader, or one of the two ways to reach one,
NavPath_ptrHeader and LevNavTable) and reads a last member must be
MainCanonicalTopologyLease_ObservePostInit or
MainCanonicalDrivers_BotNavIndex, and both must read one. The scan is
textual: a read through a pointer whose unit names none of the three,
such as a nav header pointer passed in untyped or from another file's
helper, is not seen. Review covers that case.

LR-28 Park storage (LR-S5). The park is
NativeLockstepSession.parked[senderSlot][verifiedFrameIndex % 6], with
NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY defined as
NATIVE_LOCKSTEP_MAX_INPUT_DELAY. An entry holds the verified frame, the
sender, a present flag, and the domain and combined digests (64 bytes; 48
entries add 3072 bytes to the session, still no heap). A peer's parked
frames always lie in (r, r + D], at most 6 consecutive frames, so no two
share an index, and the window plus the codec's lag pin let at most one
ACCEPTED record carry a given verified frame. A delay or sender that Open
never admits (unreachable) parks nothing: the digest is FRAME_UNAVAILABLE,
as FindDigests treats a depth Open never wrote.

LR-29 Classification order (LR-S5). AcceptBundle offers the record to the
window first, so a record above the window stays a WINDOW_OVERRUN. Only an
ACCEPTED record with a digest is classified: VERIFY_AHEAD when nothing is
recorded or its frame is after r + D, parked from r + 1 to r + D, and
compared on arrival at or below r. A VERIFY_AHEAD record stays in the peer
window, but a FAULTED session never takes it. Classification is the same
in every non-IDLE mode, so a fault still latches after a divergence.
The r + D bound is sound only while every cabinet keeps LR-2's step
order: it sends the bundle for frame f only after recording f - D, and
sends no bundle at all, not even the digest-free frames 0 to D - 1,
before its first record. ComposeBundle needs only f - D - 1 recorded and
does not enforce the order (the peer-link harnesses compose early
digest-free frames), so the drive core keeps it (LR-S8), and a bundle
sent one frame early lets a conforming peer trip VERIFY_AHEAD.

LR-30 Settling parked digests (LR-S5). RecordLocalDigests writes the
record first, then settles the parked frames from the previous r + 1 up
to the recorded frame, in increasing frame order and then slot order. A
frame below the recorded one was skipped and is FRAME_UNAVAILABLE (local
digests zero, remote from the park); the recorded frame is compared. Every
settled entry is cleared. The once-only latch therefore keeps the earliest
frame: a skipped clean frame outranks a real mismatch parked after it. The
call returns 1 whatever it latched. On arrival, a digest for a frame at or
below r that recording skipped is FRAME_UNAVAILABLE too: FindDigests finds
no record of it.

LR-31 Latch priority with parked entries (LR-S5). No call both compares a
parked digest and faults: AcceptBundle parks or faults, and
RecordLocalDigests compares. RecordLocalDigests still requires RUNNING, so
a digest parked when either latch ends the match is never compared; the
first latch stands, and DIVERGED still outranks FAULTED.

LR-32 VERIFY_AHEAD pinned (LR-S5). NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD is
15, appended after VERIFY_SHAPE. It is session-local: the decoder never
returns it and it is never on the wire. native_lockstep_isolation pins
every fault-cause value (append-only) and bans the token from the codec
and the window sources. native_lockstep_protocol_unit's causes[] lists
decoder causes only, so it asserts VERIFY_AHEAD is not among them.

LR-33 Where the peer link screens identity (LR-S6). Only when it hands a
record to the session: on replay of the staged records and while
RUNNING. Staging does not screen. A HANDSHAKING link has no agreed
identity yet, so stale records can still fill the 8 staging slots
(GAP 4, counted by DroppedEarlyBundleCount), and the resend (LR-3)
recovers what those slots displaced. The link decodes each 128-byte
record once, with the unchanged decoder, against the session's identity,
protocol version, and D, before AcceptBundle. Only a first failure of
MATCH_IDENTITY is dropped. A dropped record never reaches the session: it
takes no window space and never changes the link mode. A drop does not
stop a replay; a fault still does.

LR-34 The foreign-bundle counter (LR-S6). The counter is
NativeLockstepPeerLink.droppedForeignBundleCount, read through
NativeLockstepPeerLink_DroppedForeignBundleCount (0 for NULL). It is per
link. Like DroppedEarlyBundleCount, only a successful Open zeroes it, and
it survives Close. It is host-local: never sent, and never part of a
checkpoint, replay, or canonical state.

LR-35 The adapter adds up the drops over its links (LR-S6). Stale records
usually land on the rematch lobby's link, and RELINK closes that link
before the race it leads to. So the adapter reads the open link's count
after every lobby poll and before every close or restart of the lobby.
A read below the last one is a new link, and all of it is new. Init and
each race end (LR-36) reset the running total; Close and restart never
do. The host's AbortToTitle re-runs Init, so it resets the total too. One case is not counted: drops in a replay that also
faults a still-HANDSHAKING link, in the same lobby poll that closes that
link at its attempt budget. The counter feeds a log line only.

LR-36 The end-of-race record and log line (LR-S6). The adapter has no
log call: its isolation test allows no stdio or log header. So it
latches a record for the game hook to log. The record is latched on the
Tick whose flow moves RACING -> RESULTS, once per race and for any end
reason. A race aborted to the title never reaches RESULTS and has no
record. The record holds the race number (the adapter's matchCount), the
end reason, and the drops since the previous race end. That count then
restarts. NativeArcadeNetplay_TakeRaceEnd, then
NativeArcadeLinkHost_TakeRaceEnd, returns the record once. A record not
taken is replaced at the next race end, and Shutdown drops it.
MainArcadeLink_LinkTick takes it right after the host tick and the
autopilot's AfterTick, and logs through Platform_Log:

    [CTR Native] arcade link: race <n> ended (reason <r>); foreign bundles dropped <k>

<r> is the NATIVE_ARCADE_FLOW_END_* value. main_arcade_link_hook_isolation
(12b) pins the call site and the format. Drops after a session's last
race are never logged. <k> is a lower bound: it counts only records
screened on replay or while RUNNING. While a link is HANDSHAKING only the
first 8 records are staged (and later screened and counted); the rest
overflow into DroppedEarlyBundleCount, which is not included and not
reported.

LR-37 What the local sample may touch (LR-S7).
Platform_InputSampleLocalPad builds host slot 0 as Platform_InputUpdate
would: the slot-0 reset (connected 1, status 0, id 0x41, buttons 0xffff,
analog 0x80), then the slot-0 gamepad, the slot-0 G29, and the keyboard
when s_keyboardControllerSlot is 0 and Alt is not held. It writes only the
caller's snapshot. It never writes s_controllers[].snapshot, the pad bus,
anything Platform_InputCaptureState saves (the keyboard slot,
s_lastActiveControllerSlot, the installed-pad flag and snapshots, and each
slot's g29State, analogEnabled, switchingAnalog, and slot mapping), or the
G29 diagnostic state and log. It works whether or not installed pads are
active. It returns 1 with the snapshot written; 0 for a NULL snapshot,
with nothing written; and 0 with the neutral slot-0 snapshot while input
is not initialized. The bytes are not normalized: the drive core does that
(LR-40). The G29 pedal hysteresis the sample needs lives in a sample-only
state, s_sampleG29State in platform/native_input.c. Like the G29
diagnostic it is host-local: not in NativeInputStateSnapshot, never saved
or restored, and never written by Platform_InputUpdate. It is re-armed by
a (non-repeat) Platform_InputInit (a repeat call returns early and does
not re-arm), Platform_InputShutdown, an install that turns installed pads
on (inactive to active; a repeat install while active does not re-arm),
Platform_InputClearInstalledPadSnapshots, a successful
Platform_InputRestoreState (it may turn installed pads on and replaces
slot 0's g29State), and every change of slot 0's device: closing slot 0,
opening a device on slot 0, and a slot swap involving slot 0 (a fresh
device's pedals start asleep, so a stale awake state would read a
re-enumerated wheel's raw 0 pedals as pressed). The first
sample after a re-arm seeds it from slot 0's live g29State (read-only).
Later samples advance it alone, so a race's samples keep one continuous
hysteresis while Platform_InputUpdate replays installed pads.

LR-38 The sample does not pump (LR-S7). Platform_InputSampleLocalPad
never calls SDL_PumpEvents and does not consult g_padCommEnable. The main
loop and the hold loop pump host events, and the sample reads the device
state they left.

LR-39 The sample never toggles analog mode (LR-S7). It reads the slot's
analogEnabled only to choose the id it reports (0x73 or 0x41). A
SELECT+START chord on the gamepad is still suppressed to 0xffff, as
Platform_InputUpdate does, but without the toggle: analogEnabled and
switchingAnalog are never written. Platform_InputUpdate's own toggle,
once per chord, is unchanged.

LR-40 Normalization decides disconnection by the connected byte only
(LR-S7). NativeArcadeRaceDrive_NormalizePad treats a pad whose connected
byte is 0 as disconnected and maps it to the neutral connected pad
(NativeArcadeRaceDrive_NeutralPad). Any nonzero connected byte (1, 2,
0xff, ...) counts as connected. A connected pad's status byte is
overwritten with 0, never used to decide disconnection, even when it is
0xff with id 0xff (the pad bus's disconnected packet). Its id becomes 0x73
if it was 0x73, else 0x41. START (bit 0x0008 of the active-low button
word, buttons[0] |= 0x08) is released, and every other button bit and the
four analog bytes are kept. NULL in or out is a no-op; in and out may be
the same pad.

LR-41 The drive API and its I/O (LR-S8). The drive core
(include/platform/native_arcade_race_drive.h) works over three
caller-owned objects and does no I/O of its own:

- struct NativeArcadeRaceDrive, the drive's state;
- struct NativeArcadeRaceDriveKept, the kept-bundle ring: 8 entries
  (NATIVE_LOCKSTEP_RING_CAPACITY), each a frame tag, a present flag, and
  the 128 encoded bytes, indexed frame % 8;
- struct NativeArcadeRaceDriveCallbacks: a context pointer, passed back
  verbatim, and four callbacks. sendBundle(context, frameIndex, bytes,
  size) is the verbatim send and returns 1 if sent, 0 if refused. poll
  (context) drains the link into the session. onTakeResult(context,
  result, frameIndex) is the adapter's OnTakeResult and returns nonzero
  once the adapter's outcome is latched. servicePeriod(context) is the
  hold's once-per-period launch intake and linger (LR-9); it is optional
  (NULL: nothing). The other three are required.

Begin(drive, session, kept, callbacks, raceTickLimit) first reinitializes
the whole drive, then checks its arguments, and clears the ring only on
success. It requires a RUNNING session on which nothing has been recorded
or consumed (recordedAny 0, consumedFrame 0): one session per race, begun
before race tick 0. D is the session's inputDelay. The CAB1_HUMAN and
CAB2_HUMAN slots come from the session's config through
NativeMatchConfigV1_FindRoleSlot and must differ, and the session's
localSlot must be one of them. A refusal returns 0 and ends the drive as
LOCAL_FAILURE with a named reason (ARGUMENT, SESSION_MODE,
SESSION_STARTED, INPUT_DELAY for D outside 1..3, ROLE_SLOT, LOCAL_SLOT,
TICK_LIMIT). Nothing is sent. Step(drive, raceTick, state, localSample,
facts, padsOut[4]) and Hold(drive, periods, newPeriod, padsOut[4]) return
GO, HOLD, or END. LingerTick(drive, onResults) returns the bundles it sent.
The facts are the pointer-free struct NativeArcadeRaceDriveFacts
{endOfRace, finishedHumans, humans}. The accessors are EndKind,
FailureReason, EndIsFinish (the three finish kinds), RaceTick (the tick
of the last Step that passed its checks), EndTick, GraceStartTick,
HeldPeriods, LingerTicksLeft, RaceTickLimit, and ComposedCount. The
fixed log names are EndKindName ("none", "end of race", "finish grace",
"race tick limit", "outcome", "local failure") and FailureName. After END
every Step and Hold returns END and does nothing: no poll, no send, no
take. LR-S9 maps LOCAL_FAILURE to ReportRaceFailure, the finish kinds to
the finish report, and OUTCOME to nothing, because OnTakeResult has
already latched it. The core reads the session's mode, inputDelay,
config, localSlot, recordedAny, recordedFrame, and consumedFrame directly
and read-only; it changes the session only through RecordLocalDigests,
SubmitLocalInput, ComposeBundle, and TakeFrameInputs. The drive and the
ring are host-local (LR-15).

LR-42 The race tick limit (LR-S8). The limit tick itself ends the race:
race ticks 0 to L - 1 run, and race tick L records and ends as
RACE_TICK_LIMIT. With the default 18000 the race takes 18000 frames. A
raceTickLimit of 0 is 18000. 1 to 18000 is the internal override
(LR-S10), which may only lower the bound. Anything above 18000 is refused
(FAILURE_TICK_LIMIT). The end checks run after the record, in LR-18's
order: END_OF_RACE, then the grace, then the limit. The grace starts on
the first tick G whose facts reach max(1, humans - 1) finished humans. It
ends on tick G + 900. Its start is checked before its end, so G itself
never ends, and the start latches once per race.

LR-43 The committed pads (LR-S8). padsOut is written only on GO; HOLD and
END leave it untouched. [0] is the committed pad whose slotIndex is the
CAB1_HUMAN slot, and [1] the CAB2_HUMAN slot's, each normalized again
(LR-40, so the all-zero pad of frames 0 to D - 1 becomes neutral). [2]
and [3] are the disconnected pad: status 0xff, id 0xff, buttons 0xff
0xff, analog 0x80 x4, connected 0. Those are the bytes of the TWO_CAB
disconnected pads that NativeArcadeRosterProof_ScriptedPads gives and
MainArcadeRaceLaunch_InstallPads installs (RL-10), exposed as
NativeArcadeRaceDrive_DisconnectedPad. A committed set without a role's
pad, or for another frame, is FAILURE_ROLE_PAD, after OnTakeResult(OK).

LR-44 Hold accounting (LR-S8). Hold takes the hold loop's own arguments
(MainArcadeRaceHoldStepFn): periods, the full tick periods held so far,
and newPeriod. The core counts wall-time periods from periods, not
newPeriod calls, because a late pump raises newPeriod once however many
periods it skipped (MainArcadeRaceHoldCore_Pump), and counting calls
would stretch the 3 s timeout and the 30 s start wait. HeldPeriods
equals the hold's periods. Every Step that passes its argument checks
resets it to 0 (a new hold loop starts at 0). periods must never go
backwards, and newPeriod must be set exactly when periods exceeds
HeldPeriods. Anything else is FAILURE_PERIODS, before the poll. It is
refused, not clamped, because a clamp could freeze the count and with it
the stall timeout. Every other Hold call polls first. A newPeriod call
counts the periods up to periods. It resends the ring window
max(0, k - D - 1) to k + D once (the newest bundle, k + D, included; on
race tick 0, frames 0 to D), calls servicePeriod once, and takes, all
once per call however many periods elapsed. If the take stalls, it calls
onTakeResult(STALL, k) once for each newly elapsed period outside the
start grace (race tick 0 and period <= 810), and stops at the first
latched return, which ends as OUTCOME. Any other call only retries the
take, and a stall there reports nothing. A take that succeeds on any
call is GO, and reports no stall. So on race tick 0 the first reported
stall is period 811, and the timeout is counted period 900, also when
periods jump across 810 or 900; elsewhere it is counted period 90.
BannerDue(periods) is periods >= 10 (LR-9's hold grace); the hold module
keeps its own identical rule until LR-S11. Step while held, or Hold while
not held, is FAILURE_SEQUENCE.

LR-45 Take classification and the send gate (LR-S8).

- When the session is not RUNNING after the record (a parked digest
  diverged inside it, or an earlier drain latched and the record was
  refused), the drive calls onTakeResult(REJECTED, k). REJECTED is what
  every take returns from then on, and the tracker checks DIVERGED and
  FAULTED before it looks at the result. Then it ends as OUTCOME,
  whatever the call returns. A failed submit or compose follows the same
  rule: the outcome when the session is not RUNNING, else a local
  failure.
- A take that is neither OK nor STALL: onTakeResult(result, k) first.
  If that call returns latched, or the session is not RUNNING, the end is
  OUTCOME; otherwise it is LOCAL_FAILURE (FAILURE_TAKE). A latched return
  after an OK take or a counted stall also ends as OUTCOME, and padsOut
  is not written.
- The send gate (NativeArcadeRaceDrive_MaySend). Every compose and every
  send requires a RUNNING session, a first record, and frame f <=
  recordedFrame + D. The drive composes each frame once into the ring,
  and a second compose of a kept frame is FAILURE_COMPOSE. Every send,
  the first included, is a copy from the ring. A compose that the gate
  forbids is FAILURE_SEND_ORDER, unreachable in the step order. A send
  refused in Step or Hold is ignored: the poll and the take classify
  what the link did.
- The order of a Step's sends: on race tick 0, frames 0 to D in order and
  no resend; on tick k > 0, the new bundle k + D first, then the resends
  k - D - 1 to k + D - 1 in increasing order.

LR-46 The finish linger (LR-S8). Only a finish-kind end arms it, with 15
ticks. onResults is nonzero while the flow shows RESULTS. Each LingerTick
call, while ticks remain, onResults is nonzero, and the session is
RUNNING, resends the kept frames max(0, F - D - 1) to F + D - 1 (those
composed; an end on race tick 0 has none). It then counts one tick down,
whatever it sent. A refused send is ignored, as in Step and Hold,
because the transport also refuses on a transient socket error; the
return value counts only the sends the link accepted. The flow reaches
RESULTS only on the host Tick of pass F + 1
(platform/native_arcade_flow.c), so until the first call with onResults
nonzero, onResults 0 does nothing: no send, no count, no stop. After
RESULTS was seen, onResults 0 stops it for good (LR-13: it stops when
the flow leaves RESULTS). So the linger stops only on the count reaching
0, a session that is not RUNNING (seen or not), or leaving RESULTS after
seeing it. An OUTCOME or LOCAL_FAILURE end never arms it, and the send
gate refuses anyway while the session is not RUNNING. Note for LR-S9:
call it after the adapter's Tick of the same host tick, so that the
first call after F already sees RESULTS.

LR-47 Step's argument checks (LR-S8). They run before the record, and
each failure is a local failure with nothing recorded:

- a NULL state, sample, facts, or padsOut (ARGUMENT);
- a raceTick that is not the next tick (RACE_TICK);
- state->frameNumber not equal to raceTick (STATE_FRAME);
- humans outside 1..4, or finishedHumans above humans (FACTS).

Once they pass, the step resets the held-period count to 0, so an early
END (a failed record, a finish-kind end) leaves no stale count. The grace
start G latches before the END_OF_RACE check, so G is logged even when
END_OF_RACE comes on G itself; the tie order of LR-42 is unchanged. A
drive that was never begun is FAILURE_NOT_BEGUN. A NULL drive is END,
and nothing happens.

LR-48 The drive core's isolation (LR-S8).
tests/native_arcade_race_drive_isolation_test.cmake allows exactly these
includes: stdint.h, stddef.h, string.h, the core's own header,
platform/native_canonical_state.h, platform/native_lockstep_session.h,
and platform/native_match_config.h. The core links exactly
ctr_native_lockstep_session, which links ctr_native_match_config PUBLIC,
so the core's FindRoleSlot call resolves through it. Only
native_arcade_race_drive_test links the core; the test also links
ctr_native_lockstep_match_outcome, and ctr_native_arcade_roster_proof
for the RL-10 disconnected pad bytes (LR-43). The token ban strips
NativeCanonicalInputPadV1 and NativeCanonicalStateV4 only as whole
identifiers, so NativeCanonicalStateV4_Validate still trips it. The state
type may appear only as const struct NativeCanonicalStateV4 *. The ban
also covers the peer-link, adapter, lobby, outcome-tracker, host, and UDP
tokens. The send gate is pinned as structurally as text allows: exactly
one ComposeBundle call and one sendBundle call, each in a unit that
passes the MaySend guard first, and a guard that names
NATIVE_LOCKSTEP_RUNNING, recordedAny, recordedFrame, and
drive->inputDelay. Macro values are checked with any spacing, so
clang-format's macro alignment does not break the test.

LR-49 The verbatim bundle send and its guards (LR-S9).
NativeLockstepPeerLink_SendBundleVerbatim(link, bytes, size) sends the
caller's bytes unchanged to the peer address over the link's transport,
as ComposeAndSendBundle sends a composed bundle, and returns 1 once the
transport took them. It returns 0 with nothing sent and no link or session
state changed unless link and bytes are non-NULL, size is exactly 128
(NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES), the link mode is RUNNING, and
the session mode is RUNNING (LR-3). Beyond the plan, it also requires the
bytes to decode (NativeLockstepBundleV1_Decode, the unchanged decoder)
against the link's session: its match identity, protocol version, and
input delay, with senderSlot equal to the session's localSlot. That is the
safer option. The ring is caller-owned and a raw 128-byte buffer, the
session behind a drive is re-initialized by REMATCH (the LR-S8 note), and a
wrong buffer is one pointer away, so the link itself guarantees that only
this session's own bundle goes out: a stale ring entry of an earlier match,
a peer's record, a record with another D, or corrupt bytes are refused.
The decode is the receiver's own check (LR-33), so any bundle the local
session composed passes it. A failed transport send also returns 0 and,
like UDP, is simply lossy. Two findings the tests pin:

- The link mode check is not redundant with the session mode check. Open
  does not re-initialize the session (only a completed handshake does), so
  a link closed and reopened on the same struct is HANDSHAKING over its old,
  still RUNNING session, and the old match's bytes would otherwise decode
  and go out.
- The link copies the session mode into the link mode only when it hands a
  received bundle to the session (in Poll and in the staged replay). A
  Poll that reads no bundle leaves the link RUNNING over a DIVERGED
  session, so the session mode check cannot be replaced by "the next Poll"
  either: LR-3's "until the next poll" means until the next poll that
  reads a bundle.

The send never touches the wire format, the session, or any canonical,
checkpoint, or replay state. The drive's sendBundle callback is its
intended caller, through the host glue (the second half of LR-S9).

LR-50 The adapter's race hold service (LR-S9).
NativeArcadeNetplay_RaceService(netplay, launchPeriod) is the one new
adapter call the hold uses (LR-9). It is the third platform-only hook, next
to OnTakeResult and Link: its signature names no lockstep type, but it runs
a slice of Tick behind the flow's back, so only the host glue calls it (no
game source calls any NativeArcadeNetplay_* name today, and
main_arcade_link_hook_isolation bans them in the hook's files). On every
call it runs Tick's
step 2 through the same helper Tick uses (NativeArcadeNetplay_PollLobby:
NativeLobbyState_Poll while a lobby is open, then the LR-35 drop read), so
the drop tally stays exact and no drop is counted twice. When launchPeriod
is nonzero (the caller passes it once per held tick period) it then runs
Tick's launch intake (DriveLaunch; on RACING Tick's select branch never
runs) and its launch send and linger tick (SendLaunch), once each, so the
launch linger counts one tick per held period. It runs nothing else: no
flow tick, menu input, lobby-status mapping, lastMenuEvent reset, READY or
PEER_LOST handling, select drive, outcome latch, race-end record, or
action. The safer option taken: it is a no-op for NULL, an uninitialized
adapter, and every screen but RACING, OFF included. The drive only holds
on RACING (the flow leaves RACING only inside Tick, which the hold does not
run), and off RACING Tick already runs the same steps, so a call there
could only double-count the launch linger or drain the link behind the
flow. tests/native_arcade_netplay_isolation_test.cmake (section 8) pins
the body: it must name the RACING gate, the launchPeriod test, PollLobby,
DriveLaunch, and SendLaunch, and must not name the flow (other than
NativeArcadeFlow_Screen), the menu input, the launch, lobby, outcome,
roster, or select modules directly, Tick, OnTakeResult, the lobby actions,
lastMenuEvent, pendingLinkFailure, localRaceFailure, or raceEnd; and the
adapter has exactly one NativeLobbyState_Poll call, in PollLobby, which is
called exactly twice (Tick and RaceService).

Review changes. The plan review (on befa152a9) changed these defaults:

- LR-11 no longer treats a lead as a desync. A peer digest for a frame
  not yet recorded is parked and compared when recorded, at most D per
  peer, and a digest beyond that bound is a FAULT. That is a new session
  slice, LR-S5. LR-2, section 5, the race-1 freeze, and risk 4 follow.
- LR-10 drops the live topology reader. TOPOLOGY carries the unavailable
  summary and is not compared (risk 17); live topology waits for a
  lease-activation milestone. LR-10 also resets the runtime on race tick
  0, and the roster proof projects through MainArcadeRaceDigest.
- LR-14 drops its timing argument. The peer link drops and counts
  foreign-identity records instead of faulting, a new slice, LR-S6. Risk
  11 and race 3 follow.
- LR-9 names the host work of each hold iteration, including the launch
  linger, and classifies REJECTED takes.
- LR-4 and LR-5 sample into scratch storage and normalize status and id.
- LR-8 records that the pin reaches the roster proof, and which proof
  expectations shift.
- LR-7 corrects race tick 0's elapsedTimeMS and names the two caller
  files.
- LR-1, LR-13, and LR-16 have the game-side caller pass the game facts.
- Section 3 gains the presentation criterion 9.
- Section 4.1 gains psxRandSeed, audioRNG, and
  frameTimer_MainFrame_ResetDB.
- Section 6 splits the drive glue, renumbers the slices, groups them
  explicitly, and adds the missing isolation tests.

The re-review (on bbc6c55c7) added LR-17, the owner's then-open ruling
on the Drivers extraction's NavHeader.last read, which blocked LR-S4 and
the live-digest slices, corrected the claims that the live V4 path never
reads NavHeader.last, and tightened LR-11's window bound and LR-S5's
tests, the same-tick surfacing of a divergence latched at record (LR-3,
LR-9, LR-13, LR-S5, LR-S8, LR-S9), LR-14's drop tests and rematch timing,
LR-16's restart-point read, LR-10's runtime lifecycle, LR-12's
finish-frame desync row, and LR-S2's review marker.

Owner decisions (2026-09-25). The owner reviewed the defaults and
decided:

1. Race end. The race ends when every human has finished, or 30 s after
   the first human finishes, whichever comes first. With 3 or 4 humans
   the 30 s starts when all but one have finished. The 10-minute
   (18000-tick) race-length bound stays as a backstop. LR-18 is the
   mechanism; it amends LR-1, LR-12, LR-13, and LR-16.
2. LR-17 is ruled (a). The live digest may read the bot nav-path pointer
   NavHeader.last check-only, as the roster proof already does. It is
   never written, and never used to acquire, activate, capture, or
   publish the lease. LR-S4, LR-S10, LR-S12, and LR-S13 are unblocked.
3. Input delay: 3 ticks (LR-3), accepted pending a feel test on the
   cabinets. That is LR-3's sample to simulation, D + 1 = 3 ticks
   (100 ms at 30 Hz). D stays 2. The feel test is part of the physical
   two-cabinet validation (HANDOFF steps 6-7), out of Task 8's scope
   (section 1, risk 15).
4. Screen. The retail 2P split screen ships (criterion 9). A full-screen
   per-cabinet view is a later stretch milestone, out of Task 8's scope
   (risk 18).
5. LR-1..LR-16 otherwise stand as written. LR-18 amends LR-1, LR-12,
   LR-13, and LR-16, and LR-17's ruling updates the wording of LR-1,
   LR-10, and LR-15.

### 4.1 Per-tick simulation inputs

Every input the simulation reads per race tick, and why it is identical on
both cabinets:

    input                     source per race tick k                       why identical
    human pads                frame k - 1 pads, installed on tick k - 1    lockstep commit; one mapping (LR-5)
                              (neutral on tick 0)
    bot input                 none: bots run from seed and roster          same config, RS-7 seeds
    VBlanks per tick          2, from RenderVSYNC                          fixed pacing (LR-7); no other source in race
    elapsedTimeMS             root counter over 2 VBlanks: 32              VBlank-derived counter; LR-8 pin
                              (64 on the pass before race tick 0; tick 0
                              set by that pass's VBlanks, LR-7)
    trafficLightsTimer        previous tick's elapsedTimeMS                follows from the row above
    frameTimer_Confetti       pinned 0 at race init, +2 per tick           RS-17; LR-7; no pause (LR-6)
    gGT->timer                pinned 0 at race init, +1 per GameLogic      RS-17
    frameTimer_VsyncCallback  boot-relative; no simulation reader          compared race-relative (LR-10)
    frameCounter              boot-relative; no race reader                compared race-relative (LR-10)
    retail RNG states         seeded at race init, then drawn by the sim   RS-7; equal inputs
    deterministic bank        constant after setup                         MainArcadeRaceSetup_Bank, same config
    psxRandSeed (BIOS rand)   seeded at race init; drawn by presentation   RS-7; no simulation reader (below)
    audioRNG                  seeded at race init; drawn by sound          RS-7; no simulation reader (below)
    frameTimer_MainFrame_     boot-relative; +1 per frame                  no simulation reader (below)
      ResetDB
    pause                     impossible                                   LR-6
    demo mode, cheats         pinned by the setup                          RS-16, RS-2
    loads                     none in an arcade race                       section 2.6
    hold                      no VBlank, no simulation code                LR-9
    race tick 0               the core's definition                        same pass on both (LR-2)
    sound, rendering, window  host-local; not read by the simulation       assumed; the V4 digest catches a leak (risk 8)

The three counters the table marks "below", checked at their call sites.
None needs a new pin:

- psxRandSeed. The PSX BIOS rand (game/MixRNG/PSX_BIOS_Rand.c) is not
  boot-relative in a linked race: the setup seeds it at race init
  (PSX_BIOS_SetRandSeed at game/MAIN/MainArcadeRaceSetup.c:170-172, from
  the bot-rules draw at platform/native_arcade_bot_rules.c:407). Its only
  draws are:
  - the weapon roulette icon, game/UI/UI_Weapon.c:109, into a local
    itemID, drawn from UI_RenderFrame_Racing (game/UI/UI_RenderFrame.c:326;
    MainFrame_RenderFrame.c:584), a render path;
  - the crate-explosion rotation, game/231/RB_Crate.c:143 and :440, which
    only orients a visual instance whose thread animates and dies
    (RB_Crate.c:5-28).
  Nothing reads it back into the simulation, and V1 and V4 do not project
  it (MainMain.c:79). The draw count follows the HUD draws, which are the
  same on both cabinets while both render the same two-player view; a
  presentation change could change the count, harmlessly (risk 18).
- audioRNG. Also seeded at race init (MainArcadeRaceSetup.c:173-175).
  Drawn only by sound selection (game/HOWL/HOWL_LevelAudio.c:140,
  HOWL_Voiceline.c:126, HOWL_Garage.c:110). Not projected (MainMain.c:79).
- frameTimer_MainFrame_ResetDB. +1 per frame (MainFrame.c:64) and
  boot-relative. Its readers are audio cooldowns against stored timestamps
  (the RS-17 audit, MainArcadeRaceSetupCore.h:169-177). The one in
  vehicle physics, the crash feedback cooldown
  (game/Vehicle/VehPhysCrash.c:521-527), gates only sounds and voicelines:
  VehPhysCrash_Attack uses its canPlayFeedback argument only to play them
  (:209-213, :227-237), and the damage writes do not read it.

Sound runs per emitted VBlank (MainDrawCb.c:38-42, native_platform.c:909),
so it follows the same VBlank sequence on both cabinets. Its own RNG is
not canonical (MainMain.c:79). Render scale and display options are
cabinet-local (docs/REPLAYS.md, render-scale sweep).

## 5. Constraints

- The topology lease is untouched. No lease owner in checkpoints, replay,
  or canonical state, and no retire hook on LOAD_Hub_ReadFile. The live V4
  path uses the MainCanonicalTopology context only, for the drivers
  extraction, and supplies the unavailable TOPOLOGY summary: no topology
  fact reader and no lease API (LR-10). It does read NavHeader.last: the
  drivers extraction's MainCanonicalDrivers_BotNavIndex dereferences
  header->last for every bot (game/MAIN/MainCanonicalDrivers.c:726,
  reached from MainCanonicalRuntime.c:296 through :953-954 and :834).
  The owner ruled that read allowed on 2026-09-25 (LR-17, ruled (a)): it
  is check-only, never written, and never used to acquire, activate,
  capture, or publish the lease. The runtime's lease and replay bans
  (tests/main_canonical_runtime_isolation_test.cmake:18-47) stay.
- No change to the canonical-state schema, the replay format, the bundle,
  the handshake, NativeMatchConfigV1, or the launch record. Three reviewed
  changes land below that level:
  - the session parks early peer digests and gains one appended local
    fault cause, which is not on the wire (LR-11, LR-S5);
  - the peer link drops foreign-identity records (LR-14, LR-S6) and gains
    one verbatim send call (LR-3, LR-S9);
  - the window is unchanged.
- The drive core is pure. The game modules and the caller name no lockstep
  token. Every structural rule gets an isolation test:
  - a new tests/native_arcade_race_drive_isolation_test.cmake: purity, the
    RACE_LAUNCH section 5 token ban, and C17 (LR-S8);
  - a new tests/main_arcade_race_hold_isolation_test.cmake: the hold module
    names no VSync, no VSync callback, no GAMEPAD_ or Platform_Input call,
    no gGT or sdata write, and no lockstep token, and includes only an
    allow-list (LR-S2);
  - a new tests/native_host_wait_isolation_test.cmake: the new platform
    wait emits no VBlank (it names no VBlank emit, callback, or audio
    step), and only the hold module calls it (LR-S2);
  - a new tests/arcade_roster_proof_autopilot_isolation_test.cmake: the
    spike's steering decision is pure (include allow-list, no game state,
    lease, lockstep, replay, or checkpoint token, C17), only the roster
    proof calls it, and the proof reads ptr_restart_points and names the
    autopilot's state only in its autopilot code, never into a digest, a
    tick line, or the report (LR-S2);
  - a new tests/main_arcade_race_digest_isolation_test.cmake: the digest
    module is read-only (const game state only) and lease-free (no lease,
    topology fact reader, NavHeader, ptr_restart_points, replay, or
    MainMain token), and it carries the LR-17 pin for the owner's ruling
    (a), so it cannot pass while that rule is broken: the only NavHeader
    last readers are ObservePostInit and BotNavIndex, and the corrected
    comments say so (LR-S4);
  - updates to the pacing, runtime, hook, race setup, peer link, and
    sound-identity isolation tests.
- No heap. Portable C17 with compiler extensions off.
- Default boot is unchanged.
- Done means the MSVC build plus the full ctest suite, with a
  non-skipped live gate.
- Commit on `arcade` only, no push. Anything touching simulation identity,
  the wire, the session, the peer link, replay, canonical state, the setup
  seam, or the pacing switch is reviewed.
- Frame captures of the race or the hold banner contain retail imagery.
  They stay under build-msvc-x86 and are never committed.

## 6. Task list

Slices run in groups, one group per milestone run. Every group that
touches the launch or race path ends with a fresh, non-skipped run of
arcade_link_launch, plus arcade_roster_determinism where the setup seam
or the proof changes, from a clean tree (RACE_LAUNCH risk 7):

- Run 1: LR-S2, LR-S3. Ends with arcade_link_launch and
  arcade_roster_determinism (the proof's run K, the setup seam, and the
  pacing switch on the Launch frame).
- Run 2: LR-S4, LR-S5, LR-S6. Ends with arcade_link_launch (the peer link
  under the gate's rematch, and the world extractors now linked into
  ctr_native) and arcade_roster_determinism (the proof's V4 lines).
- Run 3: LR-S7, LR-S8, LR-S9. Ends with arcade_link_launch (the host glue
  on the launch path) and arcade_roster_determinism (LR-S7 refactors the
  native_input.c paths the proof's installed pads use).
- Run 4: LR-S10, LR-S11. Ends with arcade_link_launch (the rehearsal
  replaced) and arcade_roster_determinism (the hold module the proof's
  run K uses).
- Run 5: LR-S12, LR-S13. Ends with the full suite and the extended gate's
  recorded PASS (LR-S13).
- Run 6: LR-S14, docs only.

LR-S4 was blocked until the owner ruled on LR-17, and so were LR-S10,
LR-S12, and LR-S13, which consume its live digests. The owner ruled (a)
on 2026-09-25, so all four are unblocked. Run 2's LR-S5 and LR-S6 never
depended on the ruling.

### LR-S1 -- this document

Status: done. docs/LOCKSTEP_RACE_MILESTONE.md and the Task 8 pointer in
docs/GAME_LOOP_UI_MILESTONE.md. The plan review is closed in the commit
after befa152a9 (section 4, "Review changes"). The re-review of
bbc6c55c7 is closed in the commit after it, except LR-17, which waited
for the owner. The owner's ruling (a) of 2026-09-25 closes that
exception.

### LR-S2 -- spikes: the hold and the autopilot finish

Status: done. Internal only. Review required (it
introduces the hold module, MainArcadeRaceHold, and the host-local
platform wait, both of which later ship). Run 1.

(a) result. Every pass criterion passed. The banner is drawn with one
deviation from LR-9, for the owner's review: it is not in "the
arcade-link layout's font and style" but in a built-in host block font
(LR-9, "LR-S2 (a) result").

- The hold module is game/MAIN/MainArcadeRaceHold.{c,h} (unity chain,
  CTR_NATIVE). Its pure period core is game/MAIN/MainArcadeRaceHoldCore.{c,h}
  (library ctr_native_arcade_race_hold_core). The tick period is 33435 us,
  2 native VBlanks (897619 / 53693175 s each), and holdGraceTicks is 10.
- The host-local wait is named Platform_HostWaitMs (include/platform.h,
  one SDL_Delay). The hold's clock is Platform_HostClockUs (one
  SDL_GetTicksNS read). The banner is Platform_PresentVRAMDisplayBanner
  (LR-9, "LR-S2 (a) result"). It presents nothing while a scene is in
  progress or a pinned present is owed. Only the hold module calls the
  three.
- The internal option --arcade-roster-proof-hold makes the proof hold at
  race tick 300 for 45 periods. The hold runs from
  MainArcadeRosterProof_Frame, after GameLogic and before RenderVSYNC, the
  LR-9 hook position. The report is v9 and gains a "hold" line.
- Run K of arcade_roster_determinism is A's seed and options plus the
  hold. K equals A in every report line but the hold line: the header, the
  setup evidence, and the control, rcontrol, rng, input, and drivers
  digests of all 900 ticks.
- frameTimer_VsyncCallback is 2975 at race tick 299, 2975 at the hold's
  entry and exit, and 2977 at race tick 300: +2 across the held tick.
- Two gate runs held for 1505086 us and 1504696 us against 1504575 us
  expected (+0.03%, +0.01%). They pumped host events 1100 and 1072 times,
  at least 22 times in every period, and each presented 35 of 35 banners
  (periods 10 to 44).
- The hold time is also measured on a clock the loop does not use: the
  proof hook reads C11 timespec_get(TIME_UTC) around its
  MainArcadeRaceHold_Run call, the hold line carries it as "independent
  us", and run K checks the 10% bound on both clocks. A later gate run:
  1505662 us on the loop's clock and 1505664 us on the independent clock
  against 1504575 us expected (+0.07% on both), 1090 pumps, at least 22
  in every period, 35 of 35 banners presented.
- A capture during the hold shows the frozen frame under a black bar with
  WAITING FOR OPPONENT; the first frame after the hold has no banner. The
  captures stay under build-msvc-x86 and are never committed. Capture frame
  numbers shift by one between runs (load-time presents), so internal
  builds log each banner's capture frame number.
- Tests: main_arcade_race_hold_core_unit, main_arcade_race_hold_unit (the
  loop itself over stubbed platform calls), native_hold_banner_unit,
  main_arcade_race_hold_isolation, native_host_wait_isolation, and the
  proof unit test's hold option and v9 hold line.

(b) result. Pass: in 5 of 5 seeds the 3 laps of Crash Cove reach
END_OF_RACE with both players finished, the slowest at race tick 3753 of
the 6000 allowed. No tuning was needed: the first parameters passed.

- The steering decision is pure, in platform/native_arcade_link_autopilot.{c,h}
  (the RL-S10 autopilot module, library ctr_native_arcade_link_autopilot):
  NativeArcadeLinkAutopilot_Angle (an integer atan2 in 12-bit angle
  units, 0 facing +z, within 3 units), NativeArcadeLinkAutopilot_Steer
  (CROSS, plus LEFT or RIGHT when the heading error is outside a 48-unit
  deadband; LEFT raises the heading, as measured in the game), and
  NativeArcadeLinkAutopilot_Passed, the target advance rule: a restart
  point is passed within 256 world units of it, or once the kart is past
  the line through it square to its approach from the previous point.
  Its facts are pointer-free: kart x, z, and heading, and restart point
  x, z.
- The internal option --arcade-roster-proof-autopilot (TWO_CAB only;
  rejected without --arcade-roster-proof, and so in non-internal builds)
  replaces the scripted pads of players 0 and 1 from race tick 1. Only
  this option raises the tick cap, from 3600 to 6000
  (NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS). After a tick line is
  kept, MainArcadeRosterProof_EndFrame reads each kart's posCurr (>> 8)
  and angle, and gGT->level1->ptr_restart_points. angle is the yaw the kart
  steers by: rotCurr.y adds the turn and wobble render offsets to it. The
  target starts at the nearest restart point and moves forward past every
  passed point. The kart aims one point beyond its target. BeginFrame
  installs the buttons for the next race tick. The read is internal-only
  and non-canonical: no restart point value enters a digest, a tick line,
  or the report, and the report stays v9. The finish ticks go to the
  process log. No NavHeader read, no lease call, and no game code outside
  the proof changed.
- Crash Cove has 72 restart points, about 768 world units apart.
- The runs (ticks are race ticks; seconds at a nominal 30 Hz; at the
  real 33435 us tick period, 3753 ticks are about 125.5 s of wall time):

  | seed | END_OF_RACE | player 0 finished | player 1 finished |
  | --- | --- | --- | --- |
  | 1 | 3588 (119.6 s) | 3588 | 3583 |
  | 2 | 3648 (121.6 s) | 3613 | 3648 |
  | 0x5EED | 3675 (122.5 s) | 3568 | 3675 |
  | 0xC0FFEE | 3686 (122.9 s) | 3686 | 3682 |
  | 1234567 | 3753 (125.1 s) | 3654 | 3753 |

  END_OF_RACE is set on the tick the second player finishes, as the
  ARCADE_MODE rule requires. Each run logged all 6000 race ticks and
  reported PASS. The spike's autopilot keeps holding CROSS and steering
  after END_OF_RACE, up to race tick 6000; the measurement does not depend
  on it, and the LR-16 race drive decides the pads on RESULTS. A second run of seed 1 wrote a byte-identical report.
  Command line, with the report under build-msvc-x86 (never committed):
  `ctr_native.exe --arcade-roster-proof <report> --arcade-roster-proof-seed <seed> --arcade-roster-proof-ticks 6000 --arcade-roster-proof-autopilot`.
- Default runs are unchanged: runs A, D, and F give byte-identical
  reports from the base commit and from this change (both under the fixed
  proof build identity), and arcade_roster_determinism passes.
- Tests: native_arcade_link_autopilot_unit (the angle on the axes, the
  diagonals, and every direction; straight, left, and right; the
  deadband; angle wrap; the just-behind case; clamped extremes; the passed
  rule), native_arcade_roster_proof_unit (the option), and
  arcade_roster_proof_autopilot_isolation. The isolation test checks that
  the steering stays pure and that only the proof calls it. It also
  checks that the proof reads the restart points only in its autopilot
  helpers, never into a digest, a tick line, or the report; that the
  autopilot's state, which holds restart point indices, is named only in
  its declaration, the autopilot's helpers and step, and the gated branch
  of the pad install; and that runs A to K never pass the option.

Plan: two spikes on the roster proof, which already runs a real race on
installed pads under fixed pacing.

- (a) The hold. An internal proof option holds for 45 periods at proof
  race tick 300, through the MainArcadeRaceHold loop and banner of LR-9
  and the new host-local platform wait (name settled here).
  - Pass: arcade_roster_determinism gains a run K, A's seed with the hold,
    and K equals A at every tick in every digest.
  - Pass: K's report shows frameTimer_VsyncCallback advancing by exactly 2
    across the held tick.
  - Pass: the hold lasts 45 periods of wall time within 10%.
  - Pass: the window keeps pumping events (no "not responding").
  - Pass: a frame capture taken during the hold shows the banner. The
    capture stays under build-msvc-x86 and is never committed.
  - Fail: any of these. The banner falls back to the frozen frame alone
    (LR-9, risk 2); the rest is a design stop.
- (b) The finish. An internal steering pad profile of the LR-16 autopilot
  drives both proof players, 0 and 1. The proof's TWO_CAB race has two
  humans, and an ARCADE_MODE race ends only when both have finished
  (game/PlayLevel.c:435-437).
  - Pass: 3 laps of Crash Cove reach END_OF_RACE, both players finished,
    within 6000 race ticks in 5 of 5 seeds; the measured lengths set the
    LR-16 time budget.
  - Fail: the gate falls back to the internal race-tick cap (LR-16).

Tests: the K run in tools/arcade-roster-proof-check.ps1; a unit test of the
steering decision; main_arcade_race_hold_isolation,
native_host_wait_isolation, and arcade_roster_proof_autopilot_isolation
(section 5).

### LR-S3 -- deterministic time for linked races

Status: done ((a) LR-7, (b) LR-8). Review required (the setup seam and
the pacing switch). Run 1.

(a) result (LR-7, the pacing switch only):

- Platform_SetFixedVBlankPacing is outside CTR_INTERNAL
  (include/platform.h:87, platform/native_platform.c:1070). Its store and
  the pacer are unchanged.
- New host calls in include/platform/native_arcade_link_host.h:
  NativeArcadeLinkHost_RaceBegin (LINK only: turns fixed pacing on and
  returns 1; otherwise 0 and nothing done) and NativeArcadeLinkHost_RaceEnd
  (turns off what RaceBegin turned on). NativeArcadeLinkHost_Shutdown turns
  it off first, the same way. A host-local flag, g_racePacing, gates both
  off calls, so the host never touches a pacing it did not turn on (the
  roster proof's, and the default off state). The flag never enters a
  saved state, a recording, or canonical state. The design is deliberate:
  a conditional off, rather than an unconditional one, is what keeps the
  host from ever switching off the proof's pacing or calling the setter on
  a normal boot. Shutdown is also reached mid-race (a replacing Configure,
  or AbortToTitle's defensive init-failure branch) and then turns the race
  pacing off with the link already gone (LR-7).
- The host library ctr_native_arcade_link_host now has a link-time
  dependency on Platform_SetFixedVBlankPacing, which only the ctr_native
  executable defines; its two unit tests define a stub (CMakeLists.txt,
  native_arcade_link_host_isolation rule 4).
- Call sites in game/MAIN/MainArcadeRaceLaunch.c: RaceBegin in the
  LAUNCHED branch of MainArcadeRaceLaunch_ArmAndLaunch, after
  MainArcadeRaceSetup_Launch succeeded (:293); RaceEnd in the disarm block
  after MainArcadeRaceSetup_Disarm (:375). After an Arm or Launch failure
  only RaceEnd runs, and it does nothing. RaceBegin returning 0 on a
  LAUNCHED race (unreachable today: the caller runs only in LINK mode) is
  only logged; LR-S9 may want it to count as a launch failure.
- Default runs are unchanged. With no arcade-link option the host is OFF,
  RaceBegin is never reached (MainArcadeRaceLaunch_Frame returns first),
  and neither Configure nor Shutdown calls the setter. The arcade link
  excludes replay and the roster proof, and main.c's proof call is
  unchanged.
- Tests: native_arcade_link_host_unit (a counting pacing stub: RaceBegin,
  RaceEnd, and Shutdown switch it; a replacing Configure turns it off;
  OFF and PREVIEW never touch it; a RaceEnd with no RaceBegin, the Arm or
  Launch failure path, leaves it off; a pacing the host did not turn on is
  never touched; the stub resets at the test's start, so it does not
  depend on test order); native_vblank_pacing_isolation (the setter outside
  every conditional; exactly two caller files; the host's three call
  bodies); native_arcade_link_host_isolation (the <platform.h> include, and
  Platform_SetFixedVBlankPacing as the only platform name); and
  main_arcade_link_hook_isolation rule 16i (the caller's two call sites;
  of game/, platform/, include/, tools/, and main.c, only the caller and
  the host's own .c and .h name either call).
  main_arcade_link_view_layout_test gets a no-op setter stub to link.
- arcade_link_launch ran non-skipped and passed (78.25 s, two races,
  both validated). The run came from a clean-tree build of this change in
  a throwaway clone, with the local memcards/ folder copied in. The same
  clone at 1ed6b4daf also passed (77.16 s). Without memcards/ it failed
  in the same way at 1ed6b4daf and with this change: Launch PRECONDITION,
  "the game options are not loaded yet".

(b) result (LR-8, the root-counter pin):

- Pins. MainArcadeRaceSetupCore_OnFinalizeInitBegin in LAUNCHED pushes
  two new ops right after TIMER and FRAME_TIMER_CONFETTI and before the
  seeds: RCNT_TOTAL_UNITS 0 and CLOCK_FRAME_START -200
  (game/MAIN/MainArcadeRaceSetupCore.c:319-330; the values are
  MAIN_ARCADE_RACE_SETUP_CORE_PIN_RCNT_TOTAL_UNITS and _PIN_CLOCK_FRAME_START,
  MainArcadeRaceSetupCore.h:243-246). PIN_OP_COUNT is 4 and BEGIN_OP_COUNT
  13 (MAX_OPS 16). The adapter maps them to sdata->rcntTotalUnits and
  gGT->clockFrameStart in MainArcadeRaceSetup_Apply
  (game/MAIN/MainArcadeRaceSetup.c:184-189), with 32-bit static asserts
  (:50-51). The MainInit.c hook comment names them.
- Readback. struct MainArcadeRaceSetupPins gains rcntTotalUnits and
  clockFrameStart. The adapter reads both back with the RS-17 pins
  (MainArcadeRaceSetup_ReadPins) and logs "pinned rcntTotalUnits 0
  clockFrameStart -200; read back 0 -200" (:348). The roster proof copies
  them into NativeArcadeRosterProofPins, and NativeArcadeRosterProof_PinsMatch
  compares all four.
- Audit comment. The RS-17 bullet on clockFrameStart and
  clockDurationStall is replaced (MainArcadeRaceSetupCore.h:178-207).
  rcntTotalUnits and clockFrameStart are now PIN, for the wrap and with
  the evidence below. clockDurationStall stays LEAVE: it is snapshot and
  turned into a delta within one frame, and nothing else in game/ reads
  it. game/Timer.c is unchanged.
- Report v10. The proof report's "seeded" line appends "rcntTotalUnits <n>
  clockFrameStart <n>" before "match". tools/arcade-roster-proof-check.ps1
  requires the v10 header and a seeded line whose pin readback is exactly
  "timer 0 frameTimerConfetti 0 rcntTotalUnits 0 clockFrameStart -200 match
  1" ($seededPattern). The counters lines, and with them the four compared
  counters, are unchanged.
- elapsedTimeMS log (read-only). The roster proof logs "race tick <n>
  elapsedTimeMS <v> (timer <n+1>) rcntTotalUnits <u> clockFrameStart <c>"
  for its race ticks 0..2: elapsedTimeMS and timer from the V1 control
  snapshot the digests use, rcntTotalUnits and clockFrameStart read from
  the game at MainArcadeRosterProof_EndFrame, after the frame's RenderVSYNC
  (game/MAIN/MainArcadeRosterProof.c:946). The race caller logs "arcade
  link: race <launch> LR-8 race tick <n> elapsedTimeMS <v> (timer <n+1>)
  rcntTotalUnits <u> clockFrameStart <c>" from RenderFrame, after
  GameLogic and before that pass's RenderVSYNC
  (MainArcadeRaceLaunch_LogElapsed, game/MAIN/MainArcadeRaceLaunch.c:401).
  LR-8 race tick n is the race's n-th GameLogic pass, the frame whose
  gGT->timer is n + 1 after the RS-17 pin with the setup VALIDATED. That
  is the proof's race tick numbering. The launch core's race tick 0 (LR-2)
  is LR-8 race tick 1: the caller logs "race N tick 0" between LR-8 race
  ticks 0 and 1.
- Observed elapsedTimeMS of race ticks 0..2: 32 32 32 on both cabinets,
  for both races of arcade_link_launch, and 32 32 32 in all eleven roster
  proof runs. With them, at the caller's log point, rcntTotalUnits 0, 526,
  1052 and clockFrameStart 0, 100, 200 at race ticks 0, 1, 2, identical on
  both cabinets for both races: no VBlank falls between the pin and race
  tick 0, as the plan assumed, so the crossing tick stays 8,166. The
  proof's log point is one RenderVSYNC later: rcntTotalUnits 526, 1052,
  1578 and clockFrameStart 0, 100, 200 in all eleven proof runs of
  arcade_roster_determinism (the same phase; that run, from the same
  throwaway clone, passed in 266.02 s). Every run's setup logged "pinned
  rcntTotalUnits 0 clockFrameStart -200; read back 0 -200". Without the
  pin, 32 32 32 is inferred, not logged (the pre-pin build had no such
  line): the pre-pin and post-pin A and F control digests are identical,
  and the control digest encodes elapsedTimeMS
  (platform/native_canonical_state.c:40).
  The plan expected 64 for race tick 0, and that was wrong.
  The first race GameLogic does compute 64, from the pinned 200 ms delta
  (the wrap test checks this). Retail then overrides it: the load's stage
  sets gGT->gameMode1_prevFrame = 1, a PAUSE_ALL bit
  (game/LOAD/LOAD_TenStages.c:499), and MainFrame.c:200-203 stores 32
  after a paused previous frame. The first GameLogic's snapshot then
  replaces clockFrameStart. So the clockFrameStart pin only keeps that one
  overridden delta phase-free. From race tick 1 on, rcntTotalUnits alone
  carries the pinned phase.
- Pre-pin vs post-pin roster proof, tick by tick. Both sides were built
  from a dirty tree, so they share the unknown build identity. Pre-pin
  was HEAD f180c9d29 plus one trailing-whitespace line in this document,
  reverted afterwards. Post-pin was this change. Both exes were copied to
  build-msvc-x86/prepin and postpin. Each ran A's options (TWO_CAB, seed
  0x5EED, dwell 0, 900 ticks) and F's options (ONE_CAB, the same). All
  four exited 0. The 900 tick lines of A, and the 900 of F, are
  byte-identical pre vs post: 1,800 lines, 9,000 digests (control,
  rcontrol, rng, input, drivers) compared, and none differ. The reports
  differ only in the header version (v9 vs v10) and the seeded line's two
  new fields.
- arcade_roster_determinism passed with this change (266.46 s). A's
  launch and race tick 0 counters equal the pre-change report's (timer
  696 / 1, frameCounter 733 / 763, frameTimer 2315 / 2377,
  frameTimerConfetti 2315 / 2). The printed offsets are unchanged: C - A
  and I - F at launch timer +5329, E - A and J - F +37, and 0 for timer
  and frameTimerConfetti at race tick 0.
- The wrap test, tests/game_timer_wrap_test.c (ctest game_timer_wrap_unit),
  includes the real game/Timer.c through the real game headers. It stubs
  sdata_static and GetRCnt/SetRCnt/StartRCnt/StopRCnt, and copies only
  MainFrame.c:188-203's scale, clamps, and paused-frame override. Its
  expectations are computed without Timer.c: a tick is a crossing when
  units * 1000 modulo 2^32 goes down. It shows that:
  - unpinned, every non-32 race tick is a crossing tick with
    elapsedTimeMS 31, and the tick moves with the boot phase: boot units
    625151 give race ticks 6947 and 15113, and boot units 1420743 give
    5435 and 13600;
  - pinned, with the observed 0 VBlanks between the pin and race tick 0,
    the first crossing is at race tick 8166 (then 16331) for every one of
    7 boot unit phases times 3 pre-pin gaps;
  - N VBlanks between the pin and race tick 0 shift the crossing (the
    assumption behind 8166): N = 1 and 2 give race tick 8165, N = 3 and 4
    give 8164, each still the same for every boot phase;
  - race tick 0 is 64 before the override and 32 after it.
  Only MSVC x86 behaviour is evidence: signed overflow is UB in C17, so
  CMakeLists.txt registers game_timer_wrap_unit only for MSVC with 4-byte
  pointers (the reference toolchain).
- Tests. main_arcade_race_setup_core_unit covers the op order and values,
  the four pins, and the readback of all four fields. The PIN_OP_COUNT and
  BEGIN_OP_COUNT constants are 4 and 13, and the four targets are pinned
  only when seeding. main_arcade_race_setup_isolation covers 17 write
  targets, the two new Apply cases, the pins named once and in order in
  the seeding step, and their #defines. A new LR-8 rule (rule 13 of its
  list) covers game/, platform/, include/, and main.c, on code with
  comments removed and string literals blanked:
  - rcntTotalUnits may be named only by regionsEXE.h, MainDrawCb.c,
    Timer.c, and the adapter;
  - clockFrameStart may be named only by namespace_Main.h, MainFrame.c,
    and the adapter;
  - the race caller and the roster proof each read both exactly once, as
    the cast rvalues (long)sdata->rcntTotalUnits and
    (long)gGT->clockFrameStart of their LR-8 log lines, which no store can
    take;
  - the pin plumbing may name them otherwise only as pins-struct members:
    the "int32_t <field>;" declaration, or an access through pins,
    produced, stored, setupProduced, setupStored, or pinStored;
  - any other use fails, sdata_static.<field> and any -> or . member
    access included.
  The rule was probed with violating lines: sdata_static.rcntTotalUnits
  and a pointer's ->rcntTotalUnits in the roster proof, a pointer's
  ->clockFrameStart in the platform proof module, a store to
  sdata->rcntTotalUnits and a second (long)gGT->clockFrameStart read in the
  caller, a struct's .clockFrameStart in MainMain.c, and
  sdata->rcntTotalUnits in the core; each failed, and the probes were
  reverted. main_arcade_race_setup_core_isolation's clock token now
  exempts only the pins-struct member clockFrameStart's declaration and
  its pins.clockFrameStart uses; a probe with clock() and one with a
  pointer's ->clockFrameStart in the core each still tripped it.
  native_arcade_roster_proof_unit covers v10, the seeded line, and
  PinsMatch on the new fields. game_timer_wrap_unit is new.
- arcade_link_launch ran non-skipped and passed (88.96 s; runs 78.1 s
  each). The check now also requires the three LR-8 elapsedTimeMS lines
  (timer n + 1) of every race on each cabinet's stdout, equal across cab1
  and cab2. It printed "race 1: LR-8 elapsedTimeMS race ticks 0..2: cab1
  32 32 32, cab2 32 32 32" and the same for race 2. After review, the
  lines also carry rcntTotalUnits and clockFrameStart, and the check
  requires all three values of each race tick equal across cab1 and cab2
  (a pinned race and an unpinned one both give 32 32 32; the root-counter
  phase tells them apart). It also requires exactly one setup pin readback
  line, "pinned rcntTotalUnits 0 clockFrameStart -200; read back 0 -200",
  per race on each cabinet, before that race's validated line. A rerun in
  a fresh throwaway clone passed (80.05 s) with both races at
  32/0/0 32/526/100 32/1052/200 (elapsedTimeMS/rcntTotalUnits/
  clockFrameStart, race ticks 0..2) on both cabinets. Run on tampered
  copies of a cabinet's stdout, the check's parser reported a changed
  readback and a removed pin line, and returned a changed rcntTotalUnits
  as a triple that differs from the other cabinet's. The first run came from a
  clean-tree build of this change in a throwaway clone at a local commit,
  on a subst drive, with assets\ linked and memcards\ copied. The clone,
  the link, and the drive were removed afterwards, and the repository's
  refs were not touched. Only comment edits followed that build (the
  MainInit.c hook comment and the core header's "Writes" paragraph,
  reflowed to keep the documented line numbers).
- Normal boot and replay are unchanged: the pins are written only by the
  seeding step of a LAUNCHED setup, the race caller returns first without
  the link, and the proof log runs only while the proof is active.

Plan: LR-7 and LR-8.

- The pacing setter leaves CTR_INTERNAL. New host calls
  NativeArcadeLinkHost_RaceBegin and NativeArcadeLinkHost_RaceEnd turn
  pacing on and off; the caller calls them on the Launch frame and the
  Disarm frame, and NativeArcadeLinkHost_Shutdown turns it off. LR-S9
  extends the two calls with the drive.
- The setup pins rcntTotalUnits and clockFrameStart in LAUNCHED, reads
  them back, and the RS-17 audit comment is corrected.
- elapsedTimeMS of the first three race GameLogic passes is logged.

Tests:

- native_vblank_pacing_isolation: the two caller files and their call
  sites (LR-7).
- The setup core test: the new pins and their readback.
- main_arcade_race_setup_isolation.
- native_arcade_link_host_unit: RaceBegin, RaceEnd, and Shutdown switch
  pacing; an Arm or Launch failure leaves it off.
- A unit test of the wrap: it compiles and runs the real game/Timer.c
  Timer_GetTime_Total and Timer_GetTime_Elapsed under MSVC, with stubbed
  sdata and root counter, not a re-derivation of their arithmetic. The
  wrap relies on signed overflow, which C17 leaves undefined, so only the
  reference toolchain's behaviour is evidence. It shows the crossing and
  that the pin fixes its race tick.
- A pre-pin and a post-pin roster proof run compared tick by tick, with
  the result recorded (LR-8), and the proof check updated for the
  readback (the next report version).
- Run 1's fresh non-skipped arcade_link_launch and
  arcade_roster_determinism.

### LR-S4 -- live V4 projection

Status: done. Review required (canonical state, the runtime's
authorization). This is the slice docs/GAME_LOOP_UI_MILESTONE.md Task 8
points to. Run 2. New defaults LR-19..LR-27 (section 4).

Result:

- New module game/MAIN/MainArcadeRaceDigest.{c,h}, unity-included after
  MainArcadeRaceSetup.c (game/game_unity.h:139). API:
  MainArcadeRaceDigest_Project (one race tick: sources in, a pointer-free
  MainArcadeRaceDigestTick out), _EndRace, _Failure, _FailureName,
  _RuntimeFailure, and the pure helpers _CaptureBase, _ProjectControl, and
  _ProjectRetailRng (LR-20). It runs Reset on race tick 0, then on every
  tick the world extractors, the unavailable topology summary,
  BeginFrame, PrepareV4 with the caller's bank, ViewV4, and ReleaseV4,
  and checks the view's topology (LR-24). EndRace invalidates the
  topology context.
- The runtime request carries the bank (LR-19):
  MainCanonicalRuntimeV4Request.bank (MainCanonicalRuntime.h:57),
  staged by MainCanonicalRuntime_StageBankV4 (MainCanonicalRuntime.c:49)
  and part of the token (:43).
- The world extractors are compiled into ctr_native through the unity
  chain (game/game_unity.h:127-128), not linked (LR-22).
- LR-17 (a) comments: the lease header now says "the lease's only API"
  and names MainCanonicalDrivers_BotNavIndex
  (MainCanonicalTopologyLeaseAuthority.h:72-82). The runtime's V4
  comments name the check-only read (MainCanonicalRuntime.h:150,
  MainCanonicalRuntime.c:329).
- The roster proof projects V4 on every logged race tick
  (MainArcadeRosterProof_ProjectV4, game/MAIN/MainArcadeRosterProof.c:711,
  called at :955) and ends the race on the last tick (:1022). The report
  is v11 (LR-26). tools/arcade-roster-proof-check.ps1 requires v11, the
  unavailable topology digest on every tick, and the V4 combined and
  domain digests equal wherever it requires rng, input, and drivers
  equal (K = A already compared every line).
- NativePerf bucket arcade_race_digest_ms (LR-25). Per-tick cost, Debug
  (unoptimized) MSVC x86: 900 race ticks of a run A (two-cab, seed
  0x5EED, dwell 0, 900 ticks, --perf --perf-dir under build-msvc-x86),
  from frame_times.csv's arcade_race_digest_ms column: mean 1.295 ms,
  median 1.268 ms, p99 1.895 ms, max 2.346 ms, total 1165.6 ms. That is
  about 21% of those frames' mean work (6.23 ms), far inside the 33.3 ms
  tick. Its report was byte-identical to the ctest run's A.
- Memory. The roster proof's tickLines array keeps its 6000 entries
  (NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS, unchanged); each entry
  grew from 72 to 128 bytes with the seven v4 fields, so the array grew
  from 432,000 to 768,000 bytes (336,000 more). It is file-scope static
  storage (s_nativeArcadeRosterProof), not heap.
- Tests. New main_arcade_race_digest_unit (race-relative control and its
  wrap, four ticks against an independent projection, the unavailable
  topology, a different boot phase giving identical ticks, a pacing fault
  changing only control, the SEQUENCE, ARGUMENT, CONFIG, WORLD, PREPARE,
  BEGIN_FRAME, and END failures and their latch, EndRace, and a clean race 2
  after a race 1 poisoned with MainCanonicalRuntime_TestForceFailure).
  New main_arcade_race_digest_isolation (read-only, lease-free, the
  include allow-list, the lifecycle order, the callers, the unity chain,
  the LR-17 pin, the corrected comments, and the topology constant).
  main_canonical_drivers_binding_unit gains the request-bank cases and
  the converted human (LR-18): slot 0 with ACTION_BOT and
  ACTION_RACE_FINISHED on a nav path projects as a BOT with its nav
  index, and a botNavFrame outside the path or a wrong last is refused.
  native_arcade_roster_proof_unit covers v11, V4_FAILED, and
  NativeArcadeRosterProof_SetTickLineV4. main_canonical_runtime_isolation
  now requires exactly one live caller file, MainArcadeRaceDigest.c, free
  of lease, replay, and MainMain tokens. arcade_sound_identity_isolation
  lists the digest module. main_canonical_runtime_stack_budget_v4 holds
  on fresh Release listings: 1308 of 1536 bytes (PrepareV3 1288).
- Probes, each reverted. With the lease header restored to its pre-slice
  "sole API" comment, main_arcade_race_digest_isolation failed ("forbidden
  token 'sole API'"). A probe function reading a NavHeader's last,
  appended to MainCanonicalState.c, MainArcadeRaceSetup.c, and
  MainCanonicalDrivers.c in turn, tripped the LR-17 pin each time. A
  runtime and digest call added to MainArcadeRaceLaunch.c tripped both
  caller rules.
- arcade_roster_determinism ran non-skipped and passed (267.20 s). In its
  reports every tick's v4topology is d75d92ae427cd357, B, C, E, and K
  equal A and G, I, and J equal F in every V4 field of all 900 ticks,
  and D, H, and F differ from their bases. v4world stays constant in
  these runs (no weapon is fired).
- Normal boot and replay are unchanged: only the roster proof
  (CTR_INTERNAL) calls the module until LR-S10.
- Review follow-ups. A VIEW failure now releases the prepared state (or
  resets the runtime when there is no view) and a refused Release resets
  it, before the failure latches, so no failure leaves the workspace
  prepared or frame-active; main_arcade_race_digest_isolation pins the
  order. Neither failure is reachable from outside the module, so the
  unit test covers the reachable END failure instead. The LR-17 pin also
  scans units that name NavPath_ptrHeader or LevNavTable (LR-27). The
  mine registry's file-scope helpers carry the
  MainCanonicalWorldMineRegistry_ prefix, since the file is now in the
  unity chain.

Plan: LR-10, and LR-17's ruling (a).

- game/MAIN/MainArcadeRaceDigest.{c,h}, in the unity chain.
- The per-tick runtime lifecycle of LR-10: BeginFrame, PrepareV4,
  ViewV4, ReleaseV4.
- The PrepareV4 request carries the bank.
- The unavailable TOPOLOGY summary on every tick; no topology reader.
- The world extractors are compiled into ctr_native through the unity
  chain, not linked (LR-22).
- The race-relative control base is captured, and the runtime reset, on
  race tick 0.
- LR-17's ruling (a): the corrected "sole API" comment
  (MainCanonicalTopologyLeaseAuthority.h:72-75) and runtime comments
  (MainCanonicalRuntime.h:137-140, MainCanonicalRuntime.c:309-311), and
  the NavHeader last reader pin.

The roster proof also projects V4 each proof tick, through
MainArcadeRaceDigest, and logs its combined and domain digests (the
next report version). tools/arcade-roster-proof-check.ps1 requires them equal wherever it
requires rng, input, and drivers equal: A = B, C and E equal A, I and J
equal F. The proof also checks that every tick's topology domain digest
is the unavailable summary's, so a live topology cannot arrive unnoticed.

Tests:

- main_canonical_runtime_isolation: one live caller file,
  MainArcadeRaceDigest.c, still lease-, replay-, and MainMain-free. Its
  callers are the roster proof now and the race caller from LR-S10.
- main_arcade_race_digest_isolation (section 5), including the LR-17 pin
  and the ptr_restart_points ban (LR-16). It must fail on the tree
  before this slice, whose lease header still says "sole API".
- main_canonical_runtime_stack_budget_v4 still holds.
- A new main_arcade_race_digest_unit: the race-relative control
  projection, the unavailable topology summary, and a clean race 2 after
  a race 1 poisoned by a forced runtime failure
  (MainCanonicalRuntime_TestForceFailure, MainCanonicalRuntime.h:158-160).
- A converted human projects cleanly (LR-18). This needs no live race:
  the fixture of main_canonical_drivers_binding_unit
  (tests/main_canonical_drivers_binding_test.c:103-126) already builds
  bots on nav paths. The new case gives slot 0, a human slot
  (numPlyrCurrGame 1), what BOTS_Driver_Convert (game/BOTS.c:3112)
  leaves: ACTION_BOT with ACTION_RACE_FINISHED, a botPath, a botNavFrame
  inside that path, its navBotList entry, and BOTS_ThTick_Drive.
  MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta
  must project it as a BOT with its nav index. That extractor calls
  ExtractMetaAndBot (game/MAIN/MainCanonicalDrivers.c:860), as the live
  path does (:953). LR-S13's race 1 covers it again live: the first
  human to finish is projected converted until the race ends. LR-S10's
  gate, with its short cap, may end before any human finishes.
- The per-tick cost recorded with NativePerf.

### LR-S5 -- lockstep session: early peer digests

Status: done. Review required (the session, which decides what a
digest on the wire means). Run 2. New defaults LR-28..LR-32 (section 4).

Result:

- The park (LR-28): struct NativeLockstepSessionParkedDigest and
  NativeLockstepSession.parked[8][NATIVE_LOCKSTEP_SESSION_PARK_CAPACITY]
  (= NATIVE_LOCKSTEP_MAX_INPUT_DELAY = 6), indexed by verified frame
  modulo 6, in include/platform/native_lockstep_session.h.
- platform/native_lockstep_session.c: Verify now classifies an ACCEPTED
  record's digest (LR-29): VERIFY_AHEAD with nothing recorded or after
  r + D, parked from r + 1 to r + D, compared on arrival at or below r.
  One Compare helper serves both paths, so a parked comparison writes the
  on-arrival report byte for byte. RecordLocalDigests records, then
  settles the parked frames up to the recorded one (LR-30) and returns 1.
- New cause NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD = 15, appended and
  documented as session-local (include/platform/native_lockstep_protocol.h;
  LR-32). Its detail is r + 1, or 0 with nothing recorded; frameIndex and
  senderSlot are the bundle's. Nothing on the wire, the bundle, the
  window, the handshake, the config, canonical state, or replay changed.
- Header comments updated: FRAME_UNAVAILABLE (retired, or a parked frame
  that recording skipped; never-simulated beyond the bound is
  VERIFY_AHEAD), the fault report's VERIFY_AHEAD detail, the
  RecordLocalDigests latch and the caller's duty to read the mode, and
  AcceptBundle's on-arrival, park, and fault ranges and latch rules.
- Tests, native_lockstep_session_unit, in the drive order of LR-2 (record
  k, submit and send k + D, take k):
  TestLeadParksAndComparesClean (every D 1..6, consumed frame r and
  r + 1, every lead the window admits: 0 to D parked for D 1..3, up to
  6 - D and 7 - D for D 4..6, each settled clean at its record, and the
  next lead a WINDOW_OVERRUN with detail c for D 4..6);
  TestLeadParkedDivergence (34 lead and flipped-frame cases: the record
  of the flipped frame returns 1, the mode is DIVERGED, and the report
  equals, by memcmp, the one an on-arrival oracle copy latches);
  TestVerifyAheadFault, which replaces TestFrameUnavailableNeverSimulated
  (nothing recorded: detail 0; D = 2 at r and D = 3 at r + 1: the bound
  r + D parks and r + D + 1 faults with detail r + 1; D = 3 at r:
  WINDOW_OVERRUN); TestParkedLatchRules (once-only divergence and fault
  latches with parked entries, a skipped parked frame FRAME_UNAVAILABLE
  outranking a later mismatch, and a fault first leaving the parked
  digest uncompared). TestFrameUnavailableRetired stays.
  native_lockstep_protocol_unit asserts VERIFY_AHEAD is 15 and not a
  decoder cause. native_lockstep_isolation gains section 10 (LR-32).
- Probes, each reverted: dropping the parked compare, moving the bound to
  r + D + 1, comparing skipped frames against the record, and not parking
  each failed native_lockstep_session_unit; renumbering VERIFY_AHEAD or
  naming it in the window source failed native_lockstep_isolation.
- All other lockstep, peer-link, netplay, and lobby tests pass unchanged.
  Fast suite (-LE live): 151 of 151 passed.
- Review follow-ups: the headers and LR-29 state the send order the r + D
  bound needs (LR-S8 keeps it; TestSendOrderBound shows an early send
  trips VERIFY_AHEAD); FRAME_UNAVAILABLE also names a skipped frame at or
  below r on arrival (TestFrameUnavailableSkippedOnArrival); the
  VERIFY_AHEAD detail is worded as the first frame not yet recorded;
  TestThreeSlotParkOrder covers slot order and a re-open with three
  slots; section 10 counts every fault-cause enumerator and requires
  explicit values.

Plan: LR-11's park, in platform/native_lockstep_session.{c,h}:

- a per-peer park of NATIVE_LOCKSTEP_MAX_INPUT_DELAY entries (verified
  frame, sender, domain and combined digests);
- AcceptBundle parks an ACCEPTED record whose verified frame is after the
  last recorded frame r and at most r + D (none while nothing is
  recorded), and compares frames <= r on arrival as today;
- RecordLocalDigests compares a parked entry for the frame it records
  and latches the same divergence the on-arrival path would. A record
  whose parked comparison diverges still records the frame and returns
  1; the divergence is a latch, not a failed record, and the caller reads
  it from the session mode (the drive does so after every record, LR-9);
- a verified frame after r + D latches FAULT with the appended local
  cause NATIVE_LOCKSTEP_FAULT_VERIFY_AHEAD. Its fault report holds the
  bundle's frameIndex and senderSlot, as every decoded fault does, and
  detail holds r + 1, the first frame not yet recorded (0 while nothing
  is recorded);
- a retired frame, a frame at or below r that recording skipped, or a
  parked frame that recording skips, stays FRAME_UNAVAILABLE.

The session header comments that this contradicts are updated in the
same slice (include/platform/native_lockstep_session.h):

- :64-71, FRAME_UNAVAILABLE: "never simulated" becomes a parked frame
  that recording skipped; a frame never simulated beyond the bound is now
  the VERIFY_AHEAD fault;
- :245-283, AcceptBundle: an ACCEPTED record is compared on arrival only
  for frames <= r, parked for r + 1 to r + D, and a FAULT beyond that;
- :215-224, RecordLocalDigests: it can now latch a divergence, returns 1
  when it does, and the caller must read the mode;
- the fault report comment (:102-110) documents VERIFY_AHEAD's detail.

Tests, in native_lockstep_session_unit:

- for every D from 1 to 3, a peer leading by 1 to D + 1 ticks, parking 0
  to D digests, compares clean at each record, with the receiver's
  consumed frame both at r and at r + 1;
- for every D from 4 to 6, which the session accepts and the drive
  refuses, only the leads the window admits: up to 7 - D parked digests
  after the take of r and 6 - D before it compare clean, and the next
  lead's bundle is asserted WINDOW_OVERRUN (LR-11);
- the same leads with one digest flipped latch DIVERGED at the record of
  that frame, with the frame, sender, and masks the on-arrival path gives,
  and RecordLocalDigests returns 1 with the mode DIVERGED;
- a digest for frame r + D + 1 (a forged bundle the window accepts)
  latches FAULT VERIFY_AHEAD with the detail above. The test pins D and
  the consumed frame: D = 2 at consumed frame r, and D = 3 at consumed
  frame r + 1 (after the take of r). D = 3 at consumed frame r is a
  WINDOW_OVERRUN instead, and so asserted;
- TestFrameUnavailableNeverSimulated
  (tests/native_lockstep_session_test.c:449) becomes that FAULT case, and
  TestFrameUnavailableRetired stays;
- the divergence and fault latches keep their once-only and priority
  rules with parked entries.

Any test that lists the fault causes gains the new one.

### LR-S6 -- peer link: drop foreign-identity records

Status: done. Review required (the peer link). Run 2. New defaults
LR-33..LR-36 (section 4).

Result:

- The drop is in platform/native_lockstep_peer_link.c (LR-33). A new
  helper, NativeLockstepPeerLink_AcceptOrDropBundle, is the only path
  from a bundle to AcceptBundle, and both drop points use it: the replay
  of staged records (NativeLockstepPeerLink_ReplayEarlyBundles) and the
  RUNNING bundle route (NativeLockstepPeerLink_HandleBundleDatagram).
  NativeLockstepPeerLink_IsForeignBundle decodes a record with the
  unchanged NativeLockstepBundleV1_Decode, against the session's
  matchIdentity, protocolVersion, and inputDelay. A record whose first
  failure is MATCH_IDENTITY is dropped and counted. Every other record
  goes to AcceptBundle unchanged. The wire, the decoder and its check
  order, the session, the handshake, and the config are unchanged.
- The counter is droppedForeignBundleCount, and its accessor is
  NativeLockstepPeerLink_DroppedForeignBundleCount (LR-34), in
  include/platform/native_lockstep_peer_link.h. The header's Open, Poll,
  and struct comments describe the drop.
- The adapter, platform/native_arcade_netplay.{c,h}, adds up the drops
  over its links (LR-35). It latches one end-of-race record per race
  (struct NativeArcadeNetplayRaceEnd, NativeArcadeNetplay_TakeRaceEnd;
  LR-36). The host passes the record on
  (NativeArcadeLinkHost_TakeRaceEnd, struct NativeArcadeLinkHostRaceEnd).
  game/MAIN/MainArcadeLink.c logs it once per race:
  "[CTR Native] arcade link: race <n> ended (reason <r>); foreign bundles
  dropped <k>".
- Tests, native_lockstep_peer_link_unit:
  - TestForeignIdentityDroppedWhileStaging: two foreign records
    interleaved with two of the peer's own, staged unscreened. On replay
    both foreign records are dropped and counted, and the two own frames
    are taken.
  - TestForeignIdentityDroppedWhileRunning: three foreign records (one a
    resend) are dropped and counted while RUNNING. There is no fault, the
    next own frame is taken, and a new Open zeroes the counter.
  - TestForeignIdentityCorruptStillFaults: three records each fault
    BAD_DIGEST and are not dropped: a foreign record with one corrupted
    pad byte while RUNNING; a current record with its identity bytes
    flipped and its digest stale; and the corrupt foreign record staged,
    which faults on replay.
  - TestCurrentIdentityBadDelayOrSlotStillFaults: a current-identity
    record with D + 1 faults INPUT_DELAY, and one from the receiver's own
    slot faults BAD_SLOT.
- Tests, native_arcade_netplay_unit, each a loopback pair in which B
  resends its old match's frames 0..D to A's new rematch link:
  - TestRematchAfterDesyncDropsStaleBundles: after a real DESYNC. Both
    reach READY with no fault, and A dropped exactly 3. Race 2's
    end-of-race record on A still carries the 3 after RELINK closed that
    link. The record is latched once per race, and Shutdown clears it.
  - TestRematchAfterPreRaceFailureDropsStaleBundles: A's race fails
    locally (RL-11) while B is held on RACING, with A's rematch wait at
    the production 300 ticks. B's sending ends after 3 ticks, and its race
    fails too. Both reach READY with A's rematch wait unexpired (asserted
    every tick, and in total).
  - TestRematchDuringFinishLingerDropsStaleBundles: B keeps sending on
    RESULTS after a clean finish. The drops happen on replay, and a late
    copy is dropped while RUNNING.
- Also: native_arcade_link_host_unit checks the host take (inert when
  OFF or PREVIEW, once per race, none for an aborted race, and race 1
  again after AbortToTitle). main_arcade_link_hook_isolation gains 12b:
  one TakeRaceEnd call, after the tick and AfterTick, with the log
  format pinned. The peer-link isolation rules are unchanged, and
  native_lockstep_peer_link_process_unit still passes.
- Probes, each reverted:
  - Never dropping failed both drop tests in native_lockstep_peer_link_unit
    and all three new netplay tests (each run alone).
  - Dropping BAD_DIGEST too failed TestForeignIdentityCorruptStillFaults
    (and the existing TestInRaceFaultFromPoll).
  - Dropping every decode failure failed
    TestCurrentIdentityBadDelayOrSlotStillFaults at INPUT_DELAY (and the
    existing TestAuxKeptAfterTerminal).
  - Removing the adapter's drop reads failed race 2's end-of-race
    record.
  - Latching on every RESULTS tick failed the once-per-race check.
  - Removing the log call failed main_arcade_link_hook_isolation.
- Fast suite (-LE live): 151 of 151 passed.
- Review follow-ups: LR-36 and the log comment call <k> a lower bound
  (staging overflow is not counted), and LR-35 says Init and each race end
  reset the total. native_arcade_link_host_isolation pins the host take
  and its record's three fields. TestForeignIdentityCorruptStillFaults
  stages [foreign, corrupt, own frame 0]: 1 drop, BAD_DIGEST, and frame 0
  never reaches the session. TestRematchLinkLostKeepsStaleDrop carries a
  RUNNING-link drop through PEER_LOST and RESTART_LOBBY to race 2's
  record. Removing only the restart-path read does not fail it, because
  the post-poll read in the same Tick sees the drop first. Removing both
  reads fails it.

Plan: LR-14's decision in platform/native_lockstep_peer_link.{c,h}: the
identity check on replayed staged records and on RUNNING records, the
foreign-bundle drop counter, and its accessor. The adapter adds up the
drops over its links (LR-35) and latches one end-of-race record per race;
the game hook, MainArcadeLink, logs it (LR-36).

Tests: the four peer-link cases and the three netplay loopback cases
named in LR-14, plus the host take checks and hook isolation 12b;
native_lockstep_peer_link_process_unit still passes.

### LR-S7 -- local sample seam and normalization

Status: done. Review required (the bytes that enter the simulation and
the wire; section 5). Run 3. New defaults LR-37..LR-40 (section 4).

Result:

- The sample is `int Platform_InputSampleLocalPad(struct
  PlatformInputPadSnapshot *dst)`, declared in
  include/platform/native_input.h and defined in platform/native_input.c
  (LR-37..LR-39). NativeInput_ApplyController, NativeInput_ApplyG29, and
  NativeInput_ApplyKeyboard now take the target snapshot as a pointer.
  The mutable state is passed explicitly and may be NULL: the analog
  toggle pair and the active slot for the gamepad; the hysteresis state,
  the active slot, and a diagnostic flag for the G29. The slot-0 reset is
  NativeInput_MakeResetSnapshot. Platform_InputUpdate passes each slot's
  own snapshot and state, so its behaviour is unchanged, and its
  installed-pads path is untouched. The sample passes a local snapshot,
  NULL, and the sample-only hysteresis s_sampleG29State (LR-37). The
  re-arm flag is set by Init, Shutdown, an inactive-to-active install, and
  the clear.
- The normalization is a new pure core, platform/native_arcade_race_drive.c
  and include/platform/native_arcade_race_drive.h, library
  ctr_native_arcade_race_drive (C17, no extensions, links nothing yet;
  LR-S8 grows it into the drive core). It provides
  NativeArcadeRaceDrive_NormalizePad, NativeArcadeRaceDrive_NeutralPad
  (for LR-S8's zero-frame mapping), and the constants
  NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL (0x41u), _PAD_ID_ANALOG (0x73u),
  _START_MASK (0x0008u), _NEUTRAL_BUTTONS, and _NEUTRAL_ANALOG (LR-40).
  Neither ctr_native nor any other library links it yet (LR-S9).
- native_input_sample_unit (tests/native_input_sample_test.c) includes
  platform/native_input.c as main.c does, with CTR_NATIVE, SDL3 (built
  static, so there is no DLL to find), ctr_native_g29_input, and the host
  SDL assertion handler. It runs headless: Platform_InputInit, then every
  host device is closed, with a test-owned keyboard array and two
  sentinel-filled pad-bus buffers. SDL virtual joysticks stand in for a
  gamepad and a G29. Every sample is taken between two observations that
  must be byte-identical: Platform_InputCapturePadSnapshots' output, both
  pad-bus buffers, Platform_InputCaptureState's bytes, and the G29
  diagnostic. A self-check shows that the observations do see a change
  to the slot snapshot, the bus, the active slot, the analog mode, or the
  g29State. The cases:
  - not initialized: 0 with the neutral snapshot, and NULL returns 0;
  - four distinctive installed pads (multitap bus): the neutral slot-0
    snapshot;
  - keyboard CROSS and START (unnormalized 0xbff7); LALT and RALT
    suppress it; a keyboard on slot 1 is not sampled; SELECT and START
    from the keyboard are not a chord;
  - installed pads cleared: the sample equals Update's slot 0, with and
    without g_padCommEnable;
  - gamepad: id 0x73, buttons and axes, START unnormalized, the chord
    suppressed with no toggle over three samples, and the active slot
    untouched. Update still toggles once per chord, and after that the
    sample reads id 0x41;
  - G29 with diagnostics on: the sample's own pedal hysteresis (wake,
    press below 22500, hold at 24000, release), steering, and Options,
    with slot 0's g29State unchanged. After a clear the first sample
    matches Update. An inactive-to-active install re-seeds, and a repeat
    install does not;
  - after Platform_InputShutdown: 0 with the neutral snapshot.
- native_arcade_race_drive_unit covers every status byte and every id
  byte, each with connected 0, 1, 2, and 0xff, and every 16-bit button
  word (only 0x0008 changes). It also covers each button bit alone, every
  value of each analog byte, status 0xff with id 0xff still connected,
  the all-zero pad, in-place use, NULL arguments, and the neutral pad.
- native_arcade_race_drive_isolation
  (tests/native_arcade_race_drive_isolation_test.cmake) checks the
  include allow-list on both files and the token ban on comment-free
  code, with the two allowed canonical type names stripped first. The
  scan asserts that it found both files and the API, and a self-check
  shows that the stripping still catches other NativeCanonical tokens.
  It also checks the C17 properties in order and that the library links
  nothing. The allow-lists are variables for LR-S8 to extend.
- Probes, each reverted:
  - the sample writing s_controllers[0].snapshot failed on the pad
    snapshots;
  - the sample recording the active slot failed on the state bytes;
  - the sample toggling analog mode failed on the state bytes;
  - the sample using slot 0's live g29State failed on the state bytes;
  - the sample logging the G29 diagnostic failed on the diagnostic;
  - no seeding on re-arm failed the install re-seed check;
  - re-arming on a repeat install failed the no-re-arm check;
  - Update without its toggle failed the toggle check;
  - Update without its G29 active-slot record failed the active-slot
    check;
  - normalization: no START release, status 0xff as disconnected,
    connected != 1 as disconnected, the id passed through, and the status
    kept each failed native_arcade_race_drive_unit;
  - isolation: a stdio include, malloc in code, a NativeCanonicalStateV1
    declaration, a Platform_ call, a link to the session library, and the
    C17 properties out of order each failed
    native_arcade_race_drive_isolation, while malloc, SDL, and replay in
    a comment passed.
- Fast suite (-LE live): 154 of 154 passed. arcade_roster_determinism
  (live, run alone because it uses the installed pads): passed.
- Review follow-ups: the sample now re-arms on every slot-0 device change
  (NativeInput_CloseController and NativeInput_OpenController on slot 0,
  NativeInput_SwapControllerSlots involving slot 0) and on a successful
  Platform_InputRestoreState, and LR-37 says so and says a (non-repeat)
  Platform_InputInit; a re-enumerated G29 with raw 0 pedals sampled
  0x3fff (stale awake state) without the fix and now samples 0xffff, as
  Update does. native_input_sample_unit adds that case, swap and restore
  re-arm checks, a slot-1 gamepad case (Update records slot 1 as active
  and toggles slot 1's analog mode, not slot 0's), and a gamepad-plus-key
  sample case, and has TIMEOUT 60. The isolation test allows only
  NativeCanonicalInputPadV1 (LR-S8 adds the state type under its own
  review) and requires every target that links the core to be in
  drive_allowed_linkers (the unit test only). Probes, each reverted: no
  Close/Open re-arm read 0x3fff and failed; no swap re-arm, no restore
  re-arm, and Update recording slot 0 each failed the sample unit; a
  ctr_native link, a generator-expression link, a missing unit-test link,
  and NativeCanonicalStateV4 in the header each failed the isolation test.

Plan: LR-4. Platform_InputSampleLocalPad (name settled here) in
platform/native_input.c reads host slot 0 into caller scratch while
installed pads are active; the Apply steps take a snapshot pointer. The
normalization (connected, status 0, an allowed id, START released,
disconnected to neutral) is a pure function in the drive core.

Tests: native_input.c has no unit harness today (it is compiled only into
ctr_native). A new native_input_sample_unit links it with SDL initialized
headless and no device. With installed pads active, a sample must leave
Platform_InputCapturePadSnapshots' output, the pad-bus bytes (the buffers
registered with Platform_InputPadInit), and Platform_InputCaptureState's
bytes byte-identical, and must return the neutral slot-0 snapshot.
Normalization cases in native_arcade_race_drive_unit cover every status
and id byte, including 0xff.

### LR-S8 -- pure drive core

Status: done. Review required (the bytes that enter the simulation and
the wire; section 5). Run 3. New defaults LR-41..LR-48 (section 4).

Result:

- The API is in include/platform/native_arcade_race_drive.h
  (LR-41..LR-47). The constants: NATIVE_ARCADE_RACE_DRIVE_MAX_INPUT_DELAY
  3u, _START_GRACE_PERIODS 810u, _HOLD_GRACE_PERIODS 10u,
  _FINISH_GRACE_TICKS 900u, _FINISH_LINGER_TICKS 15u, _RACE_TICK_LIMIT
  18000u, _KEPT_CAPACITY (NATIVE_LOCKSTEP_RING_CAPACITY), _PAD_COUNT 4u,
  _NO_TICK, and the disconnected pad's _DISCONNECTED_STATUS and
  _DISCONNECTED_ID 0xffu. The types: struct NativeArcadeRaceDrive,
  NativeArcadeRaceDriveKept (and its KeptBundle), NativeArcadeRaceDriveCallbacks,
  and NativeArcadeRaceDriveFacts, and the enums NativeArcadeRaceDriveStatus
  (GO 1, HOLD 2, END 3), NativeArcadeRaceDriveEndKind (NONE, END_OF_RACE,
  FINISH_GRACE, RACE_TICK_LIMIT, OUTCOME, LOCAL_FAILURE), and
  NativeArcadeRaceDriveFailure (NONE 0 and 19 named failure reasons,
  1..19, append-only; the review follow-ups appended LOCAL_SLOT 18 and
  PERIODS 19). The calls:

      void NativeArcadeRaceDrive_Init(struct NativeArcadeRaceDrive *drive);
      int NativeArcadeRaceDrive_Begin(drive, session, kept, callbacks, raceTickLimit);
      enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Step(drive, raceTick,
          const struct NativeCanonicalStateV4 *state, localSample, facts, padsOut[4]);
      enum NativeArcadeRaceDriveStatus NativeArcadeRaceDrive_Hold(drive, uint32_t periods,
          int newPeriod, padsOut[4]);
      uint32_t NativeArcadeRaceDrive_LingerTick(drive, onResults);
      int NativeArcadeRaceDrive_BannerDue(uint32_t periods);
      void NativeArcadeRaceDrive_DisconnectedPad(struct NativeCanonicalInputPadV1 *out);

  They come with the accessors and log names of LR-41.
  NativeArcadeRaceDrive_NormalizePad and _NeutralPad (LR-S7) are
  unchanged.
- The files: platform/native_arcade_race_drive.c and its header (the core,
  C17, no extensions, no heap, clock, stdio, or I/O);
  CMakeLists.txt (the core links ctr_native_lockstep_session; the unit test
  also links ctr_native_lockstep_match_outcome, and
  ctr_native_arcade_roster_proof for the RL-10 pad bytes);
  tests/native_arcade_race_drive_test.c; and
  tests/native_arcade_race_drive_isolation_test.cmake (LR-48). Nothing
  but the unit test links or calls the core; no session, peer-link,
  adapter, host, or game code changed.
- The decisions are LR-41..LR-48. In brief: the callback I/O and a
  caller-owned ring (LR-41); the limit tick itself ends, and the
  override may only lower the bound (LR-42); the disconnected pad bytes
  of the RL-10 install (LR-43); wall-time periods counted from the hold
  loop's periods, and the hold window including k + D (LR-44); REJECTED passed to
  OnTakeResult after a record latch, and a latched OnTakeResult ranking
  above a local failure (LR-45); the linger's stop rules (LR-46); the
  argument checks (LR-47); and the isolation rules (LR-48).
- native_arcade_race_drive_unit keeps the LR-S7 normalization cases and
  adds two cores, A (CAB1) and B (CAB2), over real sessions opened on one
  config and exchanging bundles in memory. Its sendBundle enqueues into
  the other side's inbox (with drop, refuse, and freeze switches), its
  poll drains the inbox into AcceptBundle, and its onTakeResult feeds a
  real NativeLockstepMatchOutcomeTracker initialized with 90. The test
  maps the tracker's cause to an end reason: DIVERGED to DESYNC, FAULTED
  to LINK_ERROR, STALL_TIMEOUT to PEER_TIMEOUT. The states are valid V4
  states per frame, with a per-side WORLD knob. On every send and resend
  the harness checks the send order (RUNNING, after the first record,
  frame f only after the sender recorded f - D), that the bytes decode as
  the sender's bundle of that frame, and byte identity with the frame's
  first send. On every call it checks the exact set of frames sent: the
  Step window, the newPeriod Hold window, and nothing on other
  iterations. Every GO's four pads are checked against the expected
  mapping, and every HOLD and END against the untouched 0xa5 fill of
  padsOut. Every Hold call is driven with the hold loop's periods, and
  HeldPeriods is checked against them. The disconnected pad is checked
  against NativeArcadeRosterProof_ScriptedPads(TWO_CAB) pads 2 and 3,
  not literal bytes. The cases:
  - Begin: every refusal and its reason; D 1..3 run, and 0 (forced) and
    4..6 are refused (D = 4 sends nothing); the limit (0 is 18000, 1 and
    18000 are accepted, 18001 is refused); a one-cab config (ROLE_SLOT);
    a localSlot forced to each non-role slot (LOCAL_SLOT); an IDLE, a
    FAULTED, and an already-recording session; the reinitialization;
    never begun; NULL.
  - Equal inputs at D = 1, 2, 3 over 300 ticks: both sides' committed
    pads are identical and equal to the normalized sample submitted D
    frames earlier, neutral for frames 0..D-1, with pads 2 and 3
    disconnected; ComposedCount is D + ticks.
  - The send order on race tick 0 (Begin sends nothing; frames 0, 1, 2 in
    order after the record) and on the tick after a stall.
  - Hold iterations that are not a new period: poll only, with no
    period, send, service, or STALL report.
  - Stall and resume: 45 periods with 3 extra iterations each, exactly 45
    STALL reports; a retry iteration resumes, OK resets the count, and the
    race runs on to 150 in step.
  - Stall timeout (the peer drop): END at exactly 90 counted periods (270
    extra iterations not counted), OUTCOME with PEER_TIMEOUT, then silent.
  - The start wait: no report through period 810, the first at 811, END
    at exactly 900; service 900 times, sends 3 + 900 x 3. A peer that
    starts at period 850 resumes with no end.
  - Skipped periods: periods 1 -> 5 in one call report 4 stalls with one
    resend and one service; a late call over 2 periods that takes OK
    reports none. The timeout with jumps of 7 (84 -> 91 reports 85..90
    and stops at the latch) and with one jump of 1000 is exactly 90
    reports. The start wait with jumps across 810 (805 -> 815 reports
    811..815) ends at exactly 900 (895 -> 905), as do 810 -> 899 -> 900
    and 0 -> 899 -> 900.
  - A latched onTakeResult after an OK take, from Step and from Hold
    (both a retry and a new period): OUTCOME, padsOut untouched.
  - A REJECTED take while RUNNING from Hold (both kinds of call):
    LOCAL_FAILURE (TAKE), after OnTakeResult.
  - A Step that ends at the record after a 5-period hold leaves
    HeldPeriods 0.
  - A lead of 1, 2, and 3 (D + 1) ticks, either side leading: no fault,
    no divergence, identical pads to tick 120.
  - The worst-case lead at D = 2 and 3 (A's sends to B lost): B stalls at
    t = 30 + D, A composes t + 2D + 1 (lead 5, then 7, below 8), and A
    leads by D + 1 recorded ticks. With the sends restored the race runs
    on with no WINDOW_OVERRUN or other fault.
  - A protocol fault drained in the step's poll: the REJECTED take is the
    outcome (LINK_ERROR), and OnTakeResult is called before the end.
  - A forced desync on each side (frame 40's WORLD digest): both sides
    end OUTCOME (DESYNC, frame 40, mask WORLD) through one REJECTED take,
    not a local failure, then send nothing.
  - A parked digest that mismatches at the record: B leads by 2, A parks
    B's digest of 40 at tick 39, and A's record of 40 ends as OUTCOME
    (DESYNC) on tick 40. Nothing is composed, sent, or taken on that
    tick, and afterwards Step, Hold, and 20 LingerTick calls send
    nothing.
  - A divergence latched by an earlier drain: the record is refused,
    OnTakeResult(REJECTED) is called, and the drive ends OUTCOME with
    nothing sent.
  - A REJECTED take while RUNNING: LOCAL_FAILURE (TAKE), after
    OnTakeResult.
  - Local failures: record, submit, compose, race tick, state frame,
    facts (three variants), NULL arguments (four), Step while held, Hold
    while not held, Hold without pads, inconsistent hold periods (four
    variants: backwards with and without newPeriod, newPeriod with no new
    period, a new period without newPeriod; no poll), and a missing role
    pad (after OnTakeResult(OK)). None sends anything, before or after.
  - Pad mapping of crafted raw peer bytes (all-zero, connected 2 with id
    0xff and START, disconnected with axes, id 0x73 with status 0xff):
    each normalized, and pads 2 and 3 disconnected byte for byte.
  - The finish list, each on both sides with the same end tick and kind,
    the end tick recorded, nothing composed, sent, or taken on it, then
    15 linger ticks of the 5-frame window and nothing after. With 2
    humans (either human finishing at 100) the grace ends at 1000 as
    FINISH_GRACE. With 3 humans G is 200 (not 100), end 1100; with 4
    humans G is 300, end 1200; with 1 human G is 100. END_OF_RACE at 500
    ends as the natural finish, and so does END_OF_RACE at 1000, the
    grace-end tick. With no human finishing the race runs to race tick
    18000 (RACE_TICK_LIMIT). The tie with the bound lowered to 1000 ends
    as FINISH_GRACE, and as END_OF_RACE when END_OF_RACE also comes at
    1000. A lowered bound of 50 alone gives RACE_TICK_LIMIT, END_OF_RACE
    on 50 wins over it, and a grace past a bound of 600 ends at the
    bound. END_OF_RACE on the grace start tick 100 ends as END_OF_RACE
    with G 100 logged.
  - The linger: off RESULTS before RESULTS was seen waits (0, 0, 0 then
    1 sends, 15 ticks kept); 1, 1, 0 stops it for good and a later 1
    sends nothing; a session faulted after the end stops it, seen or
    not; refused sends are ignored (the window still offered, the count
    still falls, the next ticks send); an end on race tick 0 has nothing
    to send.
- native_arcade_race_drive_isolation: LR-48.
- Probes, each reverted. Each failed native_arcade_race_drive_unit unless
  noted:
  - sending k + D before the record with the guard removed (the harness's
    order check);
  - the record moved after the sends with the guard kept (the guard
    turns it into SEND_ORDER local failures);
  - stalls counted per iteration;
  - the start grace dropped (the start wait ended before 900);
  - a local failure before OnTakeResult;
  - the linger armed after every end kind;
  - the linger after a desync with the session-mode gates removed;
  - the tie order swapped (limit before grace);
  - the grace starting on the first finish for 3 and 4 humans;
  - the resend window starting at k - D;
  - the CAB2 pad mapped without re-normalization;
  - the limit tick not ending;
  - pad 3 neutral instead of disconnected;
  - a record latch ending without OnTakeResult;
  - the hold resending on every iteration;
  - the hold window without frame k + D.
  The guard without its first-record check passed the unit (the step
  order never sends before the first record) and failed the isolation
  test. Isolation probes, each failing native_arcade_race_drive_isolation:
  a non-const state in the header, a peer-link include, the core linking
  the outcome library, a second unguarded sendBundle call (it also failed
  the unit), SendKept without the guard (the unit passed), a
  NativeCanonicalStateV4_Validate call, a NativeLockstepMatchOutcome
  token, the guard without D (it also failed the unit), and
  MAX_INPUT_DELAY 4 (it also failed the unit).
- Fast suite (-LE live): 154 of 154 passed (native_arcade_race_drive_unit
  about 5 s). No live test runs the core: nothing on the live path
  links it.
- Review follow-ups: the linger waits for RESULTS before an off-RESULTS
  stop and ignores refused sends (LR-46); Hold takes the hold loop's
  periods and reports one stall per elapsed wall-time period, refusing
  inconsistent periods as FAILURE_PERIODS (LR-44); Begin refuses a
  localSlot outside the two roles (FAILURE_LOCAL_SLOT) and names D
  outside 1..3; Step resets HeldPeriods right after its checks and
  latches G before END_OF_RACE (LR-47); tests for a latched OK take,
  untouched padsOut on every HOLD and END, and a REJECTED take from
  Hold; the disconnected pad checked against the roster proof; the
  LR-S9 notes. Probes, each reverted and each failing
  native_arcade_race_drive_unit: an early onResults 0 stopping the
  linger; 1, 0, 1 still sending after the 0; a refused send stopping it;
  one stall report per newPeriod call; the start grace judged on the new
  periods, not each period; the periods check removed; stall reports
  continuing past the latch; the service or the resend once per period;
  the HeldPeriods reset moved back before the take; G not latched on an
  END_OF_RACE tick; no localSlot check; D 0 accepted; padsOut written on
  HOLD; a latched OK take ignored; a REJECTED take from Hold treated as
  the outcome. The isolation test still fails on a stdio include and a
  NativeLockstepMatchOutcome token. Fast suite (-LE live): 154 of 154
  passed (native_arcade_race_drive_unit about 7 s).

Plan: LR-2, LR-3, LR-5, LR-9 accounting and take classification, LR-12,
LR-13, LR-14, LR-18. platform/native_arcade_race_drive.{c,h}, library
ctr_native_arcade_race_drive. It covers:

- per-tick step order. The drive core must keep LR-2's record, submit,
  send order: the bundle for frame f only after recording f - D, and no
  bundle before race tick 0's record, because the session's r + D lead
  bound depends on it and ComposeBundle does not enforce it (LR-29);
- the zero frames at race tick 0;
- the resend window and the kept-bundle ring;
- the refusal of D above 3;
- pad mapping by role, and normalization;
- hold accounting in periods, the start grace, and the hold grace;
- OnTakeResult first, and REJECTED as a local failure only while RUNNING;
- a record that leaves the session non-RUNNING: OnTakeResult at once,
  then END with nothing sent or taken (LR-9);
- finish detection, the finish grace (LR-18), the linger, the
  race-length bound (with an internal override that lowers it), and end
  mapping.

Tests: native_arcade_race_drive_unit, with two cores exchanging bundles
through the real session library in memory. It covers:

- equal inputs, stall and resume, stall timeout at 90, the start wait at
  900;
- the send order (LR-29): on every tick, including race tick 0 and a
  tick after a stall, no bundle for frame f is composed or sent before
  frame f - D is recorded, and none at all before the first record;
- a lead of 1 to D + 1 ticks with no divergence;
- a peer drop and a forced desync on both sides, and a REJECTED take
  after each classified as the outcome, not a local failure;
- a parked digest that mismatches at the record: END_DESYNC on that tick,
  and nothing sent afterwards, neither a new bundle nor a resend nor the
  finish linger;
- WINDOW_OVERRUN impossible at D = 2 and refused at D = 4;
- no send after the linger or off RESULTS;
- the finish grace (LR-18): it starts on the first finish with 2
  humans, and on all but one finished (not the first finish) with 3 and
  4 humans; END_OF_RACE before the grace end is the natural finish;
  END_OF_RACE on the grace-end tick is the natural finish; the grace
  ends at exactly G + 900 as FINISHED with the "finish grace" reason; no
  human finishing runs to the race-length bound; the grace end records,
  composes and takes nothing, then lingers as F does; the tie order: the
  grace end and the race-length bound on the same tick (with the bound
  lowered by the internal override) end as "finish grace", and
  END_OF_RACE on that tick as the natural finish.

native_arcade_race_drive_isolation covers purity, the token ban, and C17.

### LR-S9 -- drive glue in the host

Status: in progress (part 1 of 2 done: the peer-link verbatim send and the
adapter race service; the host glue follows). Review required (the wire
and the link host). Run 3. New defaults LR-49..LR-50 (section 4).

Result, part 1:

- The API, two lower-layer calls the host glue will back the drive's
  callbacks with:

      int NativeLockstepPeerLink_SendBundleVerbatim(struct NativeLockstepPeerLink *link,
          const uint8_t *bytes, size_t size);
      void NativeArcadeNetplay_RaceService(struct NativeArcadeNetplay *netplay, int launchPeriod);

  SendBundleVerbatim is meant to back sendBundle (the kept-bundle ring,
  LR-3); RaceService with launchPeriod 0 the poll, and with 1 (once per
  held period) servicePeriod's launch intake and linger (LR-9). Nothing calls
  either yet: the host glue, the drive core, and game/ are unchanged.
- The files: platform/native_lockstep_peer_link.c and its header (the send
  and its doc comment); platform/native_arcade_netplay.c and its header
  (RaceService; Tick's step 2 moved into the static helper
  NativeArcadeNetplay_PollLobby, which both call, with no change to what
  Tick does; the header's "Platform-only hooks" paragraph names it);
  tests/native_lockstep_peer_link_test.c; tests/native_arcade_netplay_test.c;
  tests/native_arcade_netplay_isolation_test.cmake (section 3's comment and
  a new section 8). tests/native_lockstep_isolation_test.cmake lists no
  peer-link API or send, so it is unchanged, and so is the adapter's
  include allow-list. No change to the wire format, the bundle, the
  handshake, NativeMatchConfigV1, the session, the launch record, or any
  canonical, checkpoint, or replay state; no lease call; no heap.
- The decisions: the decode guard, beyond the plan's two mode checks, and
  the two findings about link mode (LR-49); RaceService's scope, its shared
  poll helper, and its no-op outside RACING (LR-50).
- native_lockstep_peer_link_unit (ports 48249-48256):
  - TestVerbatimSendDelivers: a RUNNING pair; the peer receives exactly the
    bytes, from A's address, and they equal what ComposeAndSendBundle sends
    for the frame; B's session takes them (OK); a resend is a DUPLICATE,
    both handed to the session and drained by B's own Poll, with no fault,
    and B then takes the frame.
  - TestVerbatimSendRefusals: a NULL and a never-opened link, NULL bytes,
    sizes 0, 127, and 129, a record of another match for A's own role, B's
    own bundle and a scratch CAB2 bundle (sender not the local slot), a
    current-identity record with D + 1, and a corrupt copy of A's bundle
    are all refused, and a marker datagram shows none reached B; the
    positive control then does; a closed link refuses.
  - TestVerbatimSendRefusedWhileHandshaking: B held HANDSHAKING refuses its
    own frame-0 bytes (nothing reaches A) and sends them once RUNNING; B
    closed and reopened on the same struct is HANDSHAKING over its old,
    still RUNNING session and refuses them (LR-49).
  - TestVerbatimSendRefusedWhenSessionDiverged, the plan's case: at D = 1,
    B's frame-3 bundle parks its frame-1 digest at A; A's record of a
    drifted frame 1 latches DIVERGED inside RecordLocalDigests while the
    link mode stays RUNNING; the kept frame-0 and frame-1 bundles are
    refused (sent fine just before), ComposeAndSendBundle refuses too, and
    nothing reaches B. A Poll that reads nothing leaves the link RUNNING,
    still refused; B's next bundle mirrors DIVERGED into the link.
  - native_lockstep_peer_link_process_unit is unchanged and passes.
- native_arcade_netplay_unit (ports 48540-48541; the 48400-48499 band is
  full):
  - 36, TestRaceServiceHold, over the loopback pair with 33c's setup (A
    commits and races while every launch record it sends from Tick is
    dropped, so B is PENDING on SELECT_RESULT and A's linger still sends);
    from then on A only runs RaceService. (a) RaceService(A, 0) drains B's
    frame-0 bundle into A's session (taken OK) and drops and counts one
    record of another match (link count 1, the adapter's tally 1), changes
    no flow, menu, select, outcome, roster, config, race-end, or flag state
    and not the launch agreement, and a marker shows no launch record
    reached B. (b) Two RaceService(A, 0) calls change nothing in the
    agreement; each RaceService(A, 1) sends one record and counts one
    linger tick; B commits and starts its race on the first one (its only
    ACCEPTED record); A hears B's HEARD through RaceService, and the call
    that takes it already sends nothing; five more launch periods send
    nothing and count five ticks, so ticksSinceCommit grew by exactly the
    number of launch periods. RaceService on B while B is still on
    SELECT_RESULT, with a launch record waiting in its inbox, leaves B
    byte-identical. A's next Tick finishes the race, and both end-of-race
    records are right: A's carries the one drop, counted once.
  - 37, TestRaceServiceNoOps: NULL, an uninitialized adapter (0xA5 fill,
    initialized 0), and an initialized adapter on screen OFF, with
    launchPeriod 0 and 1, are left byte-identical.
- native_arcade_netplay_isolation: LR-50 (section 8).
- Probes, each reverted:
  - the session mode check removed: native_lockstep_peer_link_unit failed
    (the DIVERGED case sent the kept bundle);
  - the decode guard removed: it failed (the foreign record went out);
  - only the senderSlot check removed: it failed (B's own bundle went out);
  - the link mode check removed: it passed until the reopened-link case
    was added, then failed (that case is why it was added);
  - RaceService polling without reading the drops: native_arcade_netplay_unit
    failed (linkForeignDropsSeen) and so did the isolation test;
  - a flow tick inside RaceService, and the whole Tick inside it: the unit
    failed (the flow changed) and so did the isolation test;
  - launch intake and send without launchPeriod: the unit failed (the
    agreement changed in (a));
  - no RACING gate: the unit failed (B changed) and so did the isolation
    test;
  - the send without the intake: the unit failed (A never took B's
    records) and so did the isolation test;
  - a menu-input update inside RaceService: the unit passed (no buttons,
    already armed) and the isolation test failed;
  - Tick polling the lobby inline instead of through the helper: the unit
    passed and the isolation test failed.
- Fast suite (-LE live): 154 of 154 passed. No live test runs the new
  calls: nothing on the live path calls them yet.

Plan: LR-1, LR-3, LR-9 host work.

- The host API: RaceStep and RaceHold, and the drive added to RaceBegin
  and RaceEnd.
- The glue over the netplay adapter: the kept-bundle ring, OnTakeResult,
  ReportRaceFailure, and the one new adapter call the hold uses for the
  launch intake and linger (LR-9).
- The peer-link verbatim bundle send. It sends only while both the link
  mode and the session mode are RUNNING (LR-3).
- The glue calls OnTakeResult right after a record that leaves the
  session non-RUNNING, as the drive core directs (LR-9).
- Nothing in game/ calls the new API yet.

Note for LR-S9: the glue must re-initialize the drive
(NativeArcadeRaceDrive_Init) at RaceEnd and when the flow leaves
RESULTS. The drive holds a raw pointer to the link's session, which
REMATCH re-initializes, so a stale drive must never step, hold, or
linger over it.

Note for LR-S9: NativeArcadeNetplay_OnTakeResult returns void, so the
onTakeResult callback must compute its "latched" return from the
adapter's pending link failure after the call: nonzero when the
pendingLinkFailure field of struct NativeArcadeNetplay
(include/platform/native_arcade_netplay.h:354, public in the adapter
struct; NativeArcadeNetplayView does not carry it) is not
NATIVE_ARCADE_FLOW_END_NONE.

Tests:

- native_arcade_link_host_unit: step, hold, and end over a loopback
  pair, the hold's launch-linger send, and the take classification; and
  a parked digest that mismatches inside RecordLocalDigests: the outcome
  is END_DESYNC on that tick, and the peer receives nothing from this
  side afterwards.
- native_lockstep_peer_link_unit, for the new send, including a refused
  send when the session is DIVERGED while the link mode is still RUNNING.
- native_arcade_netplay_unit, for the new adapter call.
- native_arcade_link_host_isolation: the include allow-list is unchanged.

### LR-S10 -- the caller: the rehearsal's replacement

Status: planned. Review required (simulation identity, the launch and
race path). Run 4.

Plan:

- The caller:
  - it projects (LR-S4), samples (LR-S7), steps, and installs committed
    pads on race ticks;
  - it passes END_OF_RACE, the finished-human count and the human count
    (LR-18), and, in CTR_INTERNAL builds, the autopilot's steering facts
    to RaceStep;
  - it runs the blocking hold through MainArcadeRaceHold, without the
    banner;
  - it reports the finish from the drive.
- The launch core's REHEARSAL phase becomes the drive phase. It takes the
  drive's end and failure as inputs, and a new failure code DRIVE_FAILED
  is appended.
- An internal autopilot option lowers the race-length bound to a short
  cap, so today's two-race gate stays short until LR-S13.

Tests:

- main_arcade_race_launch_core_unit and its isolation.
- A unit case for the finished-human count (LR-18). The count is a pure
  launch-core function over the slots' flag words and numPlyrCurrGame,
  which the caller copies in. The core names no retail header, so the
  finished bit is mirrored, as the setup status is, and the caller
  static-asserts the mirror. The case, in
  main_arcade_race_launch_core_unit: only slots 0 to numPlyrCurrGame - 1
  count; a finished bot in a higher slot does not; a finished human whose
  flags also carry ACTION_BOT (converted, game/PlayLevel.c:226) does.
- main_arcade_link_hook_isolation: the install only through the mapping,
  the dormant return, and ptr_restart_points named in the race caller
  only inside its CTR_INTERNAL block (LR-16). It also pins LR-18's "the
  grace writes no retail state": the race caller never writes
  actionsFlagSet or gameMode1, never names MainGameEnd_Initialize, and
  names ACTION_RACE_FINISHED only in a read.
- main_canonical_runtime_isolation: the race caller reaches the runtime
  only through MainArcadeRaceDigest.
- arcade_sound_identity_isolation, which lists the new files.

### LR-S11 -- hold presentation

Status: planned. Run 4.

Plan: the LR-9 grace and banner from the LR-S2 result: the
MainArcadeRaceHold draw path and the banner as a host overlay
(Platform_PresentVRAMDisplayBanner), not an arcade-link layout item.
LR-S2 (a) found that the arcade-link layout's DecalFont path writes
render-pass state (game/DecalFont.c:169, :196-205), so the final style
is settled on the host overlay.

Tests: a layout unit case; the preview capture checker gains the hold
banner if LR-S2 found it capturable. Captures stay under build-msvc-x86
and are never committed.

### LR-S12 -- failure wiring to RESULTS

Status: planned. Review required. Run 5.

Plan: LR-11 to LR-14 and LR-18 end to end:

- the start grace, with the launch linger kept alive through it;
- stall timeout, fault, and desync through OnTakeResult;
- local failure through ReportRaceFailure;
- the finish grace (LR-18);
- the race-length bound;
- the finish linger;
- the divergence log line;
- neutral pads from the end frame.

Tests: host and core cases for every row of the LR-12 table, the finish
grace's row included, and a start wait whose peer commits only from our
lingering launch records.

### LR-S13 -- one-machine race gate

Status: planned. Review required. Run 5.

Plan: LR-16.

- The autopilot's steering sample.
- Race 1's expectation: the same end tick and end kind on both cabinets,
  and RACE COMPLETE. END_OF_RACE is expected; the check also accepts a
  finish-grace end on both cabinets and records which end happened
  (LR-16, LR-18). A race tick limit end fails it.
- The freeze and digest-XOR injections and the per-tick report lines.
- RESULTS decisions for DESYNC and PEER_TIMEOUT: REMATCH after race 2,
  EXIT after race 3.
- tools/arcade-link-launch-check.ps1 runs three races, kills cab2 in
  race 3, and does the new comparisons; -TimeoutSeconds goes up to fit.
- One frame capture per cabinet in race 1, for criterion 9, kept under
  build-msvc-x86.
- CMakeLists.txt comments.

Tests: native_arcade_link_autopilot_unit and its isolation (the new
options are rejected outside CTR_INTERNAL and with replay options). A
recorded non-skipped PASS of the full suite from a clean tree, with the
measured gate time.

### LR-S14 -- docs close-out

Status: planned. Run 6.

Plan: this document; GAME_LOOP_UI Task 8 and risks 1, 2, 3, and 10;
RACE_LAUNCH risks 3, 4, 5, and 8; ROSTER risk 7, RS-13, and RS-18;
docs/LOCKSTEP_MILESTONE.md's 60 Hz figures and its FRAME_UNAVAILABLE rule
(LR-11); and docs/HANDOFF.md sections other than "Next work".

## 7. Risks and open questions

1. The scripted finish. The closed-loop autopilot may not finish 3 laps
   reliably: walls, jumps, or items can hit it, and both players must
   finish. LR-S2 (b) decides. On failure, the gate uses an internal
   race-tick cap. The natural END_OF_RACE path is then proven only by the
   drive core test and a recorded manual run. LR-S2 (b) decided: the
   autopilot passed. Both players finished and END_OF_RACE was reached in
   5 of 5 seeds, by race tick 3753 of 6000 (LR-S2, "(b) result"), so the
   gate keeps the natural finish and the race-tick cap fallback is not
   needed. The risk remains for the gate's own races, whose seeds come
   from the select. Since LR-18 a stuck autopilot is bounded: once the
   other human has finished, the finish grace ends the race 900 ticks
   later, as RACE COMPLETE on both cabinets. Such a run passes race 1
   (LR-16), but the natural END_OF_RACE path is then unproven live in
   that run; LR-S13 records which end happened.
2. The hold mechanism and audio. A blocking hold in the hook is new, and
   so is drawing a banner onto the displayed frame. SDL or GL presentation
   behaviour during a long hold is host-local but visible. Audio can
   underrun whenever no VBlank steps the mixer: during a hold, and also
   after any host hitch under fixed pacing, which never emits the missed
   VBlanks (LR-7). LR-S2 (a) decided the hold: it works, and the banner
   is drawn as a host overlay in a built-in block font instead of the
   arcade-link font (LR-9, "LR-S2 (a) result"), so the frozen-frame
   fallback was not needed. Audio during the hold was not observed or
   recorded, so the underrun risk stays open. At a render scale above 1
   the banner frame shows the 1x VRAM image (the pinned present reads
   VRAM), so the picture visibly drops resolution while held and returns
   with the next rendered frame.
3. Per-tick V4 cost. Drivers extraction and assembly, the
   MainCanonicalTopology context validation, and the world extractors now
   run every tick, in Debug too. LR-S4 measures the cost. If it threatens
   the 33 ms frame, the drive projects every tick but the slice reports it
   for a budget decision; frames are never skipped.
4. A persistent lead. Fixed pacing never recovers a slow frame, so a host
   hitch of x ms puts its cabinet x ms behind for good. The other cabinet
   then leads by x ms, up to D + 1 ticks; beyond that it holds once for
   the rest. The lead does not decay: both cabinets pace at the same
   nominal rate, and neither catches up. Clock drift of a few ppm adds to
   it until the leader reaches D + 1 ticks and then holds briefly now and
   then. So a lead of 1 to D + 1 ticks is the normal state of a linked
   race, not a fault. The session parks the leader's early digests
   (LR-11), and the race-1 freeze proves it live. Stalls stay short: holds
   are polled at 1 ms and the timeout counts only full periods. A slow
   host still makes its peer's race hitch.
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
   simulation. The render-scale sweep supports this for rendering, and
   section 4.1 checks the three presentation and sound counters. Physics
   and WORLD have never been compared live before (RS-13); TOPOLOGY is
   still not compared (risk 17). LR-S4's V4 proof runs and the live
   gate's per-tick comparison are the checks. A leak shows as a desync
   naming the domain.
9. The root-counter wrap (section 2.2). Without LR-8, linked races
   desync, on one tick, at about 273 s of either cabinet's uptime.
10. A new race-tick-0 load gap. Two loads on one machine usually finish
    within seconds of each other. A slow real cabinet could exceed the
    30 s start bound; the bound is a default for review.
11. Stale bundles after a rematch (LR-14). The peer link drops
    foreign-identity records, so no timing assumption is left. What
    remains: a peer whose config really differed would now show as a
    stall or a rematch timeout rather than LINK ERROR. The lobby
    handshake and the launch digest make that impossible for a race that
    started. A flood of stale records can crowd the 8-entry staging
    buffer; the drops are counted and logged, and the resend recovers.
12. The live gate needs a build from a clean tree (GAME_LOOP_UI risk 5),
    and a skip does not count.
13. The 30 Hz assumption. Every bound here is in 30 Hz ticks (RS-14
    enforces 30/1). The failure-handling layer's own constants and
    docs/LOCKSTEP_MILESTONE.md are written for 60 Hz: the stall default is
    180 frames and D = 2 is quoted as 33 ms. The adapter already passes 90
    (UX-9). LR-S14 fixes the prose.
14. Suite time. The extended gate adds about 190 s. If that is too slow,
    races 2 and 3 can use the internal race-tick cap, but race 1 cannot.
15. D is fixed at 2 and at most 3, because of the window lead of LR-3.
    The ring capacity is frozen at 8 by tests/native_lockstep_isolation_test.cmake,
    and raising it is out of scope. The owner accepted D + 1 = 3 ticks
    pending a feel test, which only the physical two-cabinet validation
    (HANDOFF steps 6-7, out of scope in section 1) can run. If it finds
    the delay too long, only D = 1 is left below today's D; if it asks
    for more, D = 3 is the ceiling.
16. Open for the owner. The owner answered all but one on 2026-09-25:
    - The lease ruling on the bot nav-index read, LR-17: (a) or (b).
      Answered: ruled (a) (LR-17). It no longer blocks LR-S4 or the
      slices after it that need live Drivers digests (risk 19).
    - Should the race-length bound end as RACE COMPLETE (LR-12) or as
      another reason? Answered: LR-1..LR-16 stand, so it ends as RACE
      COMPLETE.
    - Does RESULTS show standings, as the GAME_LOOP_UI Task 8 sketch said?
      Still open. Not planned here.
    - A human who never finishes keeps the retail race running, because
      an ARCADE_MODE race ends only when every human has finished
      (game/PlayLevel.c:435-437). Answered by the finish grace (LR-18):
      the race ends 900 ticks (30 s) after the first human finish, and
      the 18000-tick bound remains the backstop for a race in which no
      human finishes.
17. TOPOLOGY is not compared (LR-10). The unavailable summary is the same
    constant on both cabinets, so the TOPOLOGY domain detects nothing.
    Left undetected directly: a difference in the race level's quad
    checkpoints, restart points, or nav paths, from different level data
    or a corruption of that data in memory. The content identity the link
    requires covers different disc data. A runtime corruption shows only
    if it changes the simulation, later and in the DRIVERS or WORLD
    domain, so the report names the wrong domain and a later frame. Live
    TOPOLOGY needs the lease-activation milestone and its AGENTS.md gate.
18. Single-view presentation (a later stretch milestone, not planned).
    The owner ruled on 2026-09-25 that the split screen ships; a
    full-screen per-cabinet view is a later stretch milestone, out of
    Task 8's scope. Each cabinet shows the two-player split screen
    (criterion 9). A later view of only the cabinet's own player must
    not change numPlyrCurrGame or any camera state the simulation can
    see (cameras read the gamepads, CAM.c:1053, and the per-player
    render state is sized by numPlyrCurrGame, MainInit.c:247). It would
    change the HUD's rand() draw count (section 4.1), which is harmless
    only while nothing in the simulation reads psxRandSeed; the stretch
    milestone re-checks that. The larger problem is the particles. They
    draw from the item RNG, MixRNG (game/Particle.c:76, :137, :217,
    :288), which the simulation also reads, so a per-cabinet viewport
    that draws different particles would desync the simulation, not
    only the graphics. docs/HANDOFF.md stretch goal 13
    (docs/HANDOFF.md:101-109) records this. A precondition of that later
    milestone is to split the particle draws onto a presentation-only
    RNG, with the lockstep digest proving the simulation still matches.
19. The bot nav-index read and the lease rule (LR-17, ruled (a) by the
    owner on 2026-09-25). The live Drivers extraction dereferences
    NavHeader.last for every bot (MainCanonicalDrivers.c:726), against the
    lease authority's "sole API" comment
    (MainCanonicalTopologyLeaseAuthority.h:72-75), and the roster proof
    already does so live (MainArcadeRosterProof.c:579). AGENTS.md makes
    the lease the owner's call. Until the owner ruled, LR-S4, LR-S10,
    LR-S12, and LR-S13 were blocked, and with them the live gate and Task
    8's done criteria 2, 3, and 8. The ruling is (a), so this is no longer
    blocking. Ruling (a), the recommended one, costs a comment fix and an
    isolation pin (LR-S4). Ruling (b), not taken, would have re-planned
    the Drivers projection without the bot nav index and lost direct
    detection of a bot's nav-path divergence.
