# Game-loop / UI wiring milestone

Design-and-status record for wiring the two existing, tested backend policy
layers -- the failure-handling layer (docs/FAILURE_HANDLING_MILESTONE.md) and
the lobby/connect stack (docs/LOBBY_MILESTONE.md) -- into the running game
loop and a small set of arcade-link screens, on branch `arcade`. Read
AGENTS.md and docs/HANDOFF.md first. This document follows the same pattern
as docs/LOCKSTEP_MILESTONE.md and docs/LOBBY_MILESTONE.md: a prospective plan
with a task list, updated to record status as tasks land.

It expands the docs/HANDOFF.md "Next work" section as it stood when the
milestone was queued (commit 52976808c) and adds no scope beyond it:

> Owner direction: wire both existing, tested backend policy layers into the
> actual game loop and UI. [...] This milestone needs a menu/UI pass
> (lobby/waiting, results, rematch, exit screens) and wheel/G29 input wiring
> for them, following the existing decompiled menu code's own patterns [...]
> rather than inventing a new UI framework. This is a design-and-build
> milestone with real UX decisions (screen layout, wheel navigation feel);
> the operator should review the resulting flow once it's built.

Real two-cabinet, physical-hardware validation (real wire, real switch, real
G29 input) is not part of this milestone; it stays gated behind step 6 (CAB1
G29/kiosk gate) and step 7 (two-cabinet fleet acceptance).

## 1. Starting point (do not modify)

- Failure handling: platform/native_lockstep_match_outcome.{c,h},
  native_lockstep_match_roster.{c,h}, native_lockstep_rematch.{c,h}.
- Lobby/connect: platform/native_udp_transport.{c,h},
  native_lockstep_handshake.{c,h}, native_lockstep_peer_link.{c,h},
  native_lobby_state.{c,h}.
- Underneath both: native_lockstep_protocol/_input_window/_session and
  native_match_config. This milestone calls into all of these and edits none
  of them.
- Existing structural rules this milestone keeps, not relaxes:
  tests/native_lockstep_isolation_test.cmake forbids the tokens `lockstep`,
  `Lockstep`, `LOCKSTEP` anywhere under game/;
  tests/native_lockstep_failure_handling_isolation_test.cmake forbids
  `MatchOutcome`, `MatchRoster`, `LockstepRematch` anywhere under game/.
  Both remain true after this milestone: game code talks only to the new
  arcade-link adapter API (section 2.3), never to the lockstep or
  failure-handling modules by name.

Two facts about the running game shape the task split:

- The game loop runs at 30 Hz (MainFrame_RenderFrame sets
  `sdata->vsyncTillFlip = 2`, two 60 Hz VBlanks per game tick). Every
  duration below is in game-loop ticks at 30 Hz. The existing
  NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES (180) was documented as "3 s
  at 60 Hz"; at the real 30 Hz loop it would be 6 s, so the adapter passes an
  explicit 90 ticks (3 s) instead (UX-9), which is inside the frozen
  [30, 600] range; the frozen constants themselves are not changed.
- Live per-frame V4 canonical projection (MainCanonicalRuntime_PrepareV4) and
  the arcade stable-slot roster/bot setup (MainArcadeRoster,
  MainArcadeBotSetup) are dormant: no live game path calls them (integration
  step 3 is still in progress). A lockstep session cannot compose bundles
  past its first inputDelay + 1 frames without recorded V4 digests
  (NativeLockstepSession_ComposeBundle), and a networked race cannot be
  launched without the two-human-plus-bot roster. The in-race tasks (7 and 8
  below) therefore depend on that step-3 work and are listed here so the
  seam is designed end to end, but they are gated on it.

## 2. Decided design

Four layers, seam first. Each layer depends only on the layers above it in
this list, and only layer 2.4 lives under game/.

### 2.1 Menu input: native_arcade_menu_input (pure)

platform/native_arcade_menu_input.c and
include/platform/native_arcade_menu_input.h. Converts one local player's
held-button word, expressed in the module's own logical button bits (UP,
DOWN, LEFT, RIGHT, CROSS, CIRCLE, SQUARE, TRIANGLE, START, SELECT, R1), into
at most one navigation event per tick: PREV, NEXT, CONFIRM, BACK, or NONE.
The game-side caller translates the retail `BTN_*` held bits from
`sdata->gGamepads` into these logical bits, so the G29 needs no new
mapping: it already produces a PS1 pad word (docs/G29_INPUT.md) that the
retail GAMEPAD code turns into `BTN_*` bits like any other controller.
Pure, allocation-free, with no game, socket, clock, or lockstep dependency;
the logical bits keep it independent of both the raw pad format and game
headers.

Rules (UX-1 to UX-4): release-to-arm on every reset (no event fires until
every mapped button has been observed released at least once), rising-edge
only (holding a button never repeats), and ambiguous same-tick edges
(CONFIRM with BACK, or PREV with NEXT) produce NONE.

### 2.2 Screen flow: native_arcade_flow (pure)

platform/native_arcade_flow.c and include/platform/native_arcade_flow.h. A
deterministic state machine for the arcade-link screens with its own small
enums, fed once per tick with a navigation event (2.1) and an observation
(lobby status, race-finished flag, in-race link-failure reason), returning
at most one action per tick. It owns no socket and names no lockstep type,
so it is exhaustively unit-testable.

Screens: OFF, LOBBY, MATCH_FOUND, RACING, RESULTS, REMATCH_WAIT, EXIT.
Lobby status (mirrors native_lobby_state without naming it): WAITING,
CONNECTING, READY, REJECTED, LOST. End reason: NONE, FINISHED, PEER_TIMEOUT,
DESYNC, LINK_ERROR, OPPONENT_LEFT. Actions: NONE, BEGIN_LOBBY,
RESTART_LOBBY, START_RACE, BEGIN_REMATCH, CLOSE_LINK, RETURN_TO_TITLE.

Transitions:

- OFF: inert. Enter moves to LOBBY and returns BEGIN_LOBBY.
- LOBBY: WAITING or LOST returns RESTART_LOBBY after lobbyRetryPauseTicks
  (automatic, indefinite retry); CONNECTING stays; READY moves to
  MATCH_FOUND; REJECTED stays and shows the rejection, and CONFIRM returns
  RESTART_LOBBY (no automatic retry, matching native_lobby_state's own rule
  of never auto-retrying a rejection); BACK in any status moves to EXIT and
  returns CLOSE_LINK.
- MATCH_FOUND: input ignored; after matchFoundHoldTicks moves to SELECT and
  returns BEGIN_SELECT (match select, below); any lobby status other than
  READY during the hold (not only LOST) moves back to LOBBY and returns
  RESTART_LOBBY. MATCH_FOUND never starts a race: the race starts from
  SELECT_RESULT, with START_RACE on READY of the relink once the launch
  agreement has committed (docs/RACE_LAUNCH_MILESTONE.md RL-5).
- RACING: input ignored (race input belongs to the game). A link-failure
  reason moves to RESULTS with that reason; otherwise a LOST lobby status
  moves to RESULTS with LINK_ERROR; otherwise raceFinished moves to RESULTS
  with FINISHED. A failure reason, and a LOST lobby status, each outrank
  FINISHED on the same tick (UX-6).
- RESULTS: rows REMATCH (default focus) and EXIT. Events are ignored until
  resultsDwellTicks have elapsed. PREV/NEXT toggle focus; CONFIRM on REMATCH
  moves to REMATCH_WAIT and returns BEGIN_REMATCH; CONFIRM on EXIT moves to
  EXIT and returns CLOSE_LINK; BACK moves focus to EXIT without confirming;
  resultsIdleTimeoutTicks ticks without an accepted event move to EXIT and
  return CLOSE_LINK. The idle count includes ticks inside the dwell, where
  every event is ignored.
- REMATCH_WAIT, rules checked in this order: BACK moves to EXIT (end reason
  NONE) and returns CLOSE_LINK; READY moves to MATCH_FOUND; REJECTED, or
  rematchWaitTimeoutTicks elapsed, moves to EXIT with reason OPPONENT_LEFT
  and returns CLOSE_LINK; WAITING or LOST returns RESTART_LOBBY after
  lobbyRetryPauseTicks. On the timeout tick the flow therefore exits rather
  than retrying.
- EXIT: after exitHoldTicks (opponentLeftNoticeTicks when the reason is
  OPPONENT_LEFT) moves to OFF and returns RETURN_TO_TITLE.

Match select (docs/MATCH_SELECT_MILESTONE.md section 2.5) appends the
screens SELECT and SELECT_RESULT, the actions BEGIN_SELECT and RELINK, the
observation's selectStatus, and the timings selectResultHoldTicks (60) and
launchTimeoutTicks (300); existing values are unchanged. SELECT_RESULT
returns RELINK after its hold, and READY of the relinked lobby with the
observation's launchStatus COMMITTED (Task 7, RL-5) moves to RACING with
START_RACE. A pre-race failure from SELECT or SELECT_RESULT
moves to RESULTS with LINK_ERROR and returns CLOSE_LINK. That document has
the screens, actions, and timings in full.

### 2.3 Host adapter: native_arcade_netplay

platform/native_arcade_netplay.c and include/platform/native_arcade_netplay.h.
The only production module allowed to name native_lobby_state,
native_lockstep_match_outcome, native_lockstep_match_roster, and
native_lockstep_rematch together. It owns one NativeLobbyState, one outcome
tracker, one roster, one menu-input state, and one flow. Each tick it polls
the lobby, maps NativeLobbyStateMode onto the flow's lobby status, resets
the menu-input arming whenever the flow's screen changes, runs the flow, and
executes the host-side actions itself: BEGIN_LOBBY calls
NativeLobbyState_Begin, RESTART_LOBBY calls NativeLobbyState_RestartCycle
(falling back to NativeLobbyState_Close plus NativeLobbyState_Begin when no
lobby is open, for example after a failed Begin, or when RestartCycle
refuses), CLOSE_LINK calls NativeLobbyState_Close, and BEGIN_REMATCH
closes, builds the rematch config, and begins on it. START_RACE and
RETURN_TO_TITLE are returned to the caller (the game), which owns level
loading. Race-time hooks (task 8) feed each TakeFrameInputs result into the
outcome tracker and the roster, and a latched outcome becomes the flow's
link-failure reason: STALL_TIMEOUT maps to PEER_TIMEOUT, DIVERGED to
DESYNC, FAULTED to LINK_ERROR. The view's localMenuEvent is the menu event
produced from the local buttons on the last Tick, whether or not the screen
acted on it, so consumers must gate on a state change (NONE on every tick
without a new edge, on every Tick that starts on screen OFF, and after Init,
a successful Enter, or Shutdown); it is presentation only (menu sounds),
never a peer's input, and changes nothing the flow, select, lobby, or wire
sees.

The race driver is not the link's only reader: the adapter's own lobby poll
runs every tick, including during RACING, and drains arriving bundles into
the session. When that poll finds the link FAULTED or DIVERGED (lobby
PEER_LOST) while racing with no failure pending, the adapter reads the
session's latched fault or divergence into the outcome tracker and roster
itself, so the flow shows DESYNC or LINK ERROR from the real cause.

The HELLO is retransmitted every tick (UX-5): the peer link requires
Retransmit before Poll on every tick while HANDSHAKING, and a sparser
cadence lets the side that completes first stop sending HELLO before the
other side has seen one, which then never completes. The adapter therefore
only accepts a retransmit interval of 1: Init rejects every other value.

Rematch agreement is implicit, not a new wire message (UX-7): both peers
derive the rematch masterSeed deterministically from the agreed config they
both hold byte-identically: the first little-endian 8-byte word of the
SHA-256 NativeMatchConfigV1_Digest of the previous config that is nonzero
and differs from the previous seed, trying up to four 8-byte words
([0, 8), [8, 16), [16, 24), [24, 32)) in order. They build the config with
NativeLockstepRematch_BuildConfig and re-run the ordinary handshake on it.
Two peers that both chose REMATCH therefore propose byte-identical configs
and reach READY; a peer that chose EXIT has closed its socket, so the other
side's handshake goes unanswered until rematchWaitTimeoutTicks and it shows
OPPONENT LEFT. With match select, the previous config is lastReadyConfig
(the proposal of the most recent lobby that reached READY), and the
rematch config is the base of the next select rather than the next race
config (docs/MATCH_SELECT_MILESTONE.md section 2.6). Every rematch opens a
brand-new peer link and session, per the native_lockstep_rematch.h
contract; nothing from a finished, diverged, or faulted session is
reused. If the seed or the rematch config cannot be
built (defensive only), the adapter begins no lobby and refuses every
RESTART_LOBBY until the rematch wait times out to OPPONENT LEFT; it never
proposes the old config (and old seed) again. Enter, Shutdown, and
RETURN_TO_TITLE clear that block.

The adapter's public API uses only NativeArcadeNetplay_* identifiers, so
game code can call it without tripping the lockstep and failure-handling
isolation rules. Two of those names are platform-only:
NativeArcadeNetplay_OnTakeResult and NativeArcadeNetplay_Link carry
lockstep types (a session result and a peer link) and are hooks for the
task 8 race driver only, which lives under platform/. Game code must not
call them; naming their types there would fail
tests/native_lockstep_isolation_test.cmake.

### 2.4 Screens: game/MAIN/MainArcadeLinkScreens

Split in two, following the existing standalone-testable game/MAIN pattern
(MainArcadeRoster, MainArcadeBotSetup):

- A pure layout builder (standalone static library, unit-tested) that turns
  the adapter's view (screen, lobby status, end reason, focused row, ticks
  in screen, local cabinet role) into a fixed-capacity draw list of text
  lines: ASCII string, x, y, font (FONT_BIG or FONT_SMALL), and flags
  (JUSTIFY_CENTER plus a retail colour). No game globals, no rendering.
- A thin CTR_NATIVE-only drawer, in the game/game_unity.h chain, that walks
  the draw list with the retail DecalFont_DrawLine and frames it with the
  retail menu box (RECTMENU_DrawInnerRect / RECTMENU_DrawOuterRect_*), the
  same primitives VB_EndEvent_DrawMenu (game/225.c) and the RECTMENU code
  use. No new UI framework.

Layout defaults (UX-10), in the 512 x 216 retail screen space: title line
in FONT_BIG, ORANGE, centred at y = 40; status/body lines in FONT_SMALL,
centred, starting at y = 90 with 20 px spacing; menu rows in FONT_BIG,
centred, at y = 120 and y = 145, drawn in ORANGE like retail RECTMENU rows,
with the focused row marked by the retail translucent highlight box, and
drawn GRAY (the retail disabled-row colour) with no highlight until the menu
accepts input (after the results dwell and release-to-arm); footer hint in
FONT_SMALL at y = 186. Strings are ASCII upper case to match the retail
font:

| Screen | Title | Body |
| --- | --- | --- |
| Title (attract, arcade-link mode) | ARCADE LINK | PRESS START (blinking); THIS CABINET: CAB 1 (or CAB 2) |
| LOBBY, WAITING | ARCADE LINK | WAITING FOR OPPONENT; THIS CABINET: CAB 1 (or CAB 2) |
| LOBBY, CONNECTING | ARCADE LINK | CONNECTING |
| LOBBY, REJECTED | ARCADE LINK | LINK REFUSED: SETTINGS DO NOT MATCH; CROSS: RETRY  TRIANGLE: BACK |
| MATCH_FOUND | ARCADE LINK | OPPONENT FOUND; GET READY |
| RESULTS, FINISHED | RACE COMPLETE | rows REMATCH, EXIT |
| RESULTS, PEER_TIMEOUT | OPPONENT DISCONNECTED | rows REMATCH, EXIT |
| RESULTS, DESYNC | RACE OUT OF SYNC | rows REMATCH, EXIT |
| RESULTS, LINK_ERROR | LINK ERROR | rows REMATCH, EXIT |
| REMATCH_WAIT | REMATCH | WAITING FOR OPPONENT; TRIANGLE: CANCEL |
| EXIT | THANKS FOR PLAYING (or OPPONENT LEFT) | none |

A trailing 0-3 dot animation on the WAITING and CONNECTING lines is driven
by ticksInScreen, so it is deterministic and frame-capture friendly.

### 2.5 Activation and scope of the live hook

Everything is dormant unless a host-local option is given, so retail,
replay, and canonical-state behaviour is unchanged by default:

- `--arcade-link cab1|cab2`, `--arcade-link-port <port>`, and one or more
  `--arcade-link-peer <ipv4>:<port>` enable the adapter. Like the display
  options, these are host-local launch configuration, not match identity.
- The fixture both cabinets propose to the first lobby is fixed by the
  build, not chosen per cabinet in a menu, because the handshake is
  validate-and-reject, not negotiation (docs/LOBBY_MILESTONE.md section
  2.2). It is the lobby base config; the race config is the one match
  select resolves from it and re-validates with a relink handshake
  (docs/MATCH_SELECT_MILESTONE.md).
- `--arcade-link-preview <screen>` (internal builds only) drives the flow
  through scripted observations with no socket, so every screen can be
  captured with the existing `--capture-frame` and `--exit-after-frame`
  options for operator review. The screen names are `title`, `lobby`,
  `lobby-connecting`, `lobby-rejected`, `match-found`, `results`,
  `results-timeout`, `results-desync`, `results-link-error`, `rematch`,
  `exit`, and `exit-opponent-left`, and the five match-select previews
  `select-character`, `select-track`, `select-laps`, `select-wait`, and
  `select-result` (docs/MATCH_SELECT_MILESTONE.md section 2.7). A preview
  is exclusive with `--arcade-link`, and a port or peer without
  `--arcade-link` is an error.

Exact command lines (Task 6b-2), run from the build directory:

```sh
# Cabinet 1 on port 7001, trying cabinet 2 at 192.168.1.12:7002
ctr_native.exe --arcade-link cab1 --arcade-link-port 7001 --arcade-link-peer 192.168.1.12:7002
# Cabinet 2, the mirror image
ctr_native.exe --arcade-link cab2 --arcade-link-port 7002 --arcade-link-peer 192.168.1.11:7001
# Operator review capture of one screen (internal builds)
ctr_native.exe --arcade-link-preview results --capture-frame 1800=results.bmp --exit-after-frame 1810
```

main.c parses these right after the display options. A malformed
arcade-link option is fatal with a one-line usage message, the same posture
as the frame-capture options, and `--arcade-link-preview` is rejected in a
build without CTR_INTERNAL. `--arcade-link` or `--arcade-link-preview`
combined with any replay record, playback, or report option
(`--record`, `--record-v2`, `--record-v3`, `--replay`, `--replay-v2`,
`--replay-v3`, `--replay-bypass-header`, `--toggle`, `--detailed`, matched by
name in argv) is fatal with "--arcade-link and --arcade-link-preview cannot
be combined with replay record or playback options.": the layer hides the
retail main-menu box and clears menu input and pad taps, all of which a
checkpointed recording captures (checkpoints capture the D230 and sdata
regions), so a recording made with the option would not play back without
it. The check runs right after the arcade-link parser and before the replay
parsers, so no report folder or recording file is created first. The
quick-state hotkeys (F5 save state, F8 load state) are disabled in link and
preview mode: while the host mode is not OFF each press does nothing but log
"[CTR Native] quick states are disabled in arcade-link mode" once. A quick
state captures the retail main-menu box the layer hides (checkpoints include
the main-menu overlay data) but not the layer's own state (the hidden-box
flag, the previous held word, or the host's link), so a state saved in link
or preview mode would leave the retail box invisible in a later normal run,
and loading one could leave the link out of step. With no arcade-link option
both hotkeys behave as before. With `--arcade-link`, main.c reads the
build and content identity once with NativeIdentity_Get after asset and
replay initialisation (the disc image caches the content digest the replay
scheduler also reads, so nothing is hashed per frame) and fails with
"arcade link requires a known build and content identity" if it is not
known. It then calls NativeArcadeLinkHost_Configure (failure is fatal), logs
one line (`arcade link: off`, `arcade link: cabN port P, K peers`, or
`arcade link: preview <name>`), and shuts the host down before
Platform_Shutdown on every exit path after Configure, including the host's
quit and window-close paths, which leave through exit() (an atexit
registration made after Platform_Init's own runs first).

The live hook is game/MAIN/MainArcadeLink.c, called once per frame from
MainFrame_RenderFrame at the retail menu seam, just before
RECTMENU_CollectInput. With the host mode OFF it returns 0 as its first
statement and changes nothing, so the default path is retail. Otherwise it
gathers one frame's facts, hands them to the pure decision policy
game/MAIN/MainArcadeLinkPolicy.c (MainArcadeLinkPolicy_Decide, unit-tested
branch by branch; its mirrored retail button bits, title states, menu-ready
frame, and host modes are static-asserted in MainArcadeLink.c), applies the
outputs, ticks the host, and draws:

- Ownership. The arcade-link layer owns a frame's menu layer whenever the
  retail main-menu box could be visible or take input: the main-menu level
  is idle (levelID MAIN_MENU_LEVEL, no load in progress), the retail
  top-level main menu is the box the retail menu system processes this frame
  (the pending ptrDesiredMenu if one is set, else ptrActiveMenu),
  mainMenuState is MAIN_MENU_TITLE, and the title is anywhere but the intro
  before the retail menu-ready frame. That is, it owns INTRO once
  MM_TITLE_INTRO_FRAME (read as the title code reads it, as s16) has reached
  TITLE_INTRO_MENU_READY_FRAME (230), which is when MM_Title_MenuUpdate
  clears DISABLE_INPUT_ALLOW_FUNCPTRS and slides the box in for 12 frames
  while the state is still INTRO, and also when the intro is skipped to
  frame 1000; IN_MENU; EXITING (the box still takes input while it slides
  out, which could replace the demo exit route with a retail one); and
  RETURNING. The hook and the funcPtr read the same intro frame and title
  state in the same frame, so no frame falls between the box becoming
  interactive (or IN_MENU being set inside the funcPtr) and the layer owning
  it. An open submenu never relaxes ownership: the retail menu hierarchy is
  unreachable in link and preview mode, and if a submenu is somehow open the
  layer owns the frame anyway and keeps the whole box, submenu included,
  hidden. Before the menu-ready frame the box is input-less and undrawn in
  retail, so the layer leaves the frame alone and the retail intro skip
  keeps working. In LINK mode the layer also owns every frame on which a
  link screen other than OFF is active (NativeArcadeLinkHost_ScreenActive),
  on any level, except race frames: with the flow on RACING and the frame
  not on the idle main-menu level, the policy sets only tickOnly and the
  held buttons, and the hook ticks the host and returns 0 without owning
  the frame (docs/RACE_LAUNCH_MILESTONE.md RL-8). PREVIEW mode follows the same title-window rule and never
  owns outside it (the host's ScreenActive reports 1 in preview mode; the
  policy does not use it there), so from boot to the menu-ready frame a
  preview run shows the retail boot and intro.
- LINK mode, when it owns the frame: on the attract screen (screen OFF) a
  rising edge of START or CROSS on local player 0 calls
  NativeArcadeLinkHost_Enter, except while the title is EXITING (the demo is
  already on its way); NativeArcadeLinkHost_Tick runs every owned frame with
  player 0's held buttons mapped to the logical menu bits (BTN_CROSS_one and
  BTN_SQUARE_one, not the combined bits). A button already held when the
  layer takes the frame is not a press. START_RACE first logs the agreed
  match ("arcade link: agreed match track .. laps .. seed 0x.. slots ..",
  from NativeArcadeLinkHost_GetAgreedMatch), then hands the launch to the
  live race caller (MainArcadeRaceLaunch_StartRace; Task 7,
  docs/RACE_LAUNCH_MILESTONE.md RL-S8b), which MainFrame_RenderFrame steps
  right after the hook on the same frame. It no longer aborts to the
  title. RETURN_TO_TITLE off the main-menu level uses the retail
  demo-mode exit (boolDemoMode 0, numPlyrNextGame 1, mainMenuState
  MAIN_MENU_TITLE, MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL)); on the
  main-menu level it needs nothing.
- PREVIEW mode: the same ownership rule and a Tick every owned frame, never
  Enter; it draws the scripted preview screen and resets the demo countdown
  on every owned frame, so a capture holds steady.
- Drawing: the host view goes field for field into the layout builder
  (2.4), and the draw list is walked in order with the retail primitives:
  TEXT with DecalFont_DrawLine, HIGHLIGHT with the RECTMENU_DrawSelf row
  highlight (CTR_Box_DrawClearBox with menuRowHighlight_Normal), PANEL with
  RECTMENU_DrawInnerRect in style 0, the retail main menu's drawStyle.
- Title, demo, and input. While the layer owns the frame, the retail
  main-menu box neither receives input nor draws, but the retail title scene
  keeps running. RECTMENU_ProcessState still runs, so the box's funcPtr
  (MM_MenuProc_Main) keeps driving the title camera and trophy animation
  (MM_Title_*). Before the retail menu code runs, the hook clears every
  pad's buttonsTapped (all eight GamepadBuffer entries) and anyoneTapped,
  because MM_ParseCheatCodes, called from that funcPtr, reads
  gamepad[0].buttonsTapped straight from the pad; so no cheat code and no
  intro-skip tap can fire on an owned frame. Held bits are kept: the layer
  reads them and the retail demo countdown reads anyoneHeldCurr. The
  per-player menu input RECTMENU_CollectInput gathers (buttonTapPerPlayer,
  which the title thread also reads as its tap, and buttonHeldPerPlayer) is
  cleared with RECTMENU_ClearInput in the same frame, so
  RECTMENU_ProcessInput sees no button and the title thread sees no tap the
  next frame; nothing collected while the layer owns the frame can reach a
  retail menu later. The hook sets INVISIBLE on the retail main-menu box, so
  RECTMENU_ProcessState skips RECTMENU_DrawSelf (which also draws any open
  submenu), and clears it again on the first frame the layer no longer owns
  (retail never sets INVISIBLE on that box). If the title slides out on the
  main-menu level (the demo countdown fired from the attract screen) the
  layer keeps owning the frame until the demo exit closes the box, so the
  box does not flash in only to slide away. While any arcade-link screen
  other than OFF is up, or a preview is shown, the hook resets the retail
  title demo countdown (demoCountdownTimer) every owned frame, so the demo
  never fires; on the LINK attract screen the countdown runs as retail,
  reset by any held button, and the demo attract loop plays. Skipping
  RECTMENU_ProcessState would stop the title funcPtr, and so the title
  scene. Setting DISABLE_INPUT_ALLOW_FUNCPTRS would also hide and mute the
  box, but retail title code writes that bit itself
  (MM_JumpTo_Title_FirstTime sets it and the intro handler clears it), so
  restoring it could fight retail; and it would leave the collected taps in
  the per-player buffers for the title thread and later menus. INVISIBLE is
  written by no retail code on this box, so the hook can own and restore it
  without conflict.
- Residual retail window. Before the menu-ready frame the layer leaves the
  intro to retail so the intro skip works, and the retail funcPtr already
  runs there (DISABLE_INPUT_ALLOW_FUNCPTRS still runs funcPtrs), so a player
  holding L1 and R1 can still enter a retail cheat code during the first
  seconds of the title intro. Clearing taps there would also kill the intro
  skip, which reads the same taps. A cheat entered there does not reach a
  linked race launched through the race setup seam: the setup pins every
  non-transient gameMode1 and gameMode2 bit (docs/ROSTER_MILESTONE.md
  RS-2), cheat bits included, so Task 7 need not reset them itself.

The fixture (native_arcade_link_options, Task 6a, on the real bot rules
since roster task R-3) is profile ARCADE_TWO_CAB on track 3 (CRASH_COVE),
3 laps, a 30/1 tick rate, master seed 0x4354524e41524331 ("CTRNARC1"),
CAB1 Crash (character 0) in slot 0 and CAB2 Cortex (character 1) in slot 1
at difficulty 0, and in slots 2..5 the retail 2P AI set for those two
(Polar 6, N. Gin 4, Tiny 2, Coco 3; RS-4) at the global medium bot
difficulty 0xA0 (RS-3). gameMode1, gameMode2, and rules are all 0, which
under RS-2 (docs/ROSTER_MILESTONE.md) means a retail arcade single race
with no cheats and no cup; the race setup, not the config, writes the
retail mode bits. Build and content identity come from the caller's
identity. botRulesDigest is the real bot-rules digest,
NativeArcadeBotRules_DigestV1 (docs/ROSTER_MILESTONE.md section 3.1), no
longer the placeholder SHA-256 of "CTRN arcade-link fixture bot rules v1",
and the builder fails unless NativeArcadeBotRules_ValidateConfigV1 accepts
the result. Two cabinets on the same build and disc build byte-identical
fixtures; different builds or discs build configs the handshake rejects
with CONFIG_MISMATCH, which is the intended "LINK REFUSED: SETTINGS DO NOT
MATCH" path.

The fixture is the lobby base config of a first match, not the race
config. Match select starts its cursors on the fixture's characters,
track, and laps, and the race runs on the config it resolves: the picked
track, laps, and characters, the retail 2P bot set, and a masterSeed
derived from both cabinets' nonces. UX-8's fixed first seed is therefore
superseded by SEL-12 (docs/MATCH_SELECT_MILESTONE.md section 4); the
fixture seed "CTRNARC1" seeds only the lobby base.

### 2.6 Host glue

platform/native_arcade_link_host.c and include/platform/native_arcade_link_host.h
(Task 6b-1) are a process-wide singleton that owns one host adapter in LINK
mode or a scripted preview in PREVIEW mode, and is OFF (every call inert, no
socket) unless NativeArcadeLinkHost_Configure is given enabled or preview
options and, for a link, the caller's identity. Game code calls only
Configure, Mode, ScreenActive, Enter, Tick (held NATIVE_ARCADE_MENU_BUTTON_*
bits and a race-finished flag in, a flow action out), GetView (a flat view
with the local cabinet, whether the results rows accept input, whether the
title attract layout applies, the event produced from the local buttons on
the last Tick, whether or not the screen acted on it, so consumers must gate
on a state change (localMenuEvent: LINK only, NONE in PREVIEW, local input
only, for menu sounds), and the select view of the match-select screens),
GetAgreedMatch (track, laps, seed, and slot roles and characters of the
agreed race config, for the START_RACE log), and, since Task 7
(docs/RACE_LAUNCH_MILESTONE.md RL-S6), Racing (LINK mode with the flow on
RACING), GetAgreedConfig (the exact agreed NativeMatchConfigV1 bytes, for
Arm), ReportRaceFailure (a local race failure ends RACING as RESULTS
LINK ERROR), and Shutdown (Configure and Shutdown from main.c). The host
also has AbortToTitle (close the link and return to screen OFF), which no
game code calls since Task 7. docs/MATCH_SELECT_MILESTONE.md section 2.7 lists the select view
fields and the host value names that go with them. Its header includes no adapter header and names no lockstep, failure-handling,
or lobby token, which tests/native_arcade_link_host_isolation_test.cmake
enforces.

## 3. Owner-accepted UX defaults

Each was chosen as a default for a two-cabinet, wheel-only kiosk and flagged
for the operator to confirm or change after seeing the built flow. The
owner has accepted all of them, UX-1..UX-11, as decisions; UX-8 stays
superseded by match select, as its entry says.

1. UX-1: Confirm is Cross or Start. On the G29 the throttle pedal is also
   Cross (docs/G29_INPUT.md); that is retail behaviour and is kept, made
   safe by UX-3.
2. UX-2: Back is Triangle only. Square is excluded on these screens because
   the brake pedal is Square: a foot resting on the brake at race end must
   never exit the session.
3. UX-3: Release-to-arm on every screen entry, rising edge only, and a
   30-tick (1 s) results dwell, so a throttle still held across the finish
   line cannot auto-confirm the results screen.
4. UX-4: Navigation is D-pad up/left or the left paddle (R1) for previous,
   and D-pad down/right or the right paddle (Circle) for next. Steering never
   navigates, so an off-centre wheel cannot drift the focus. No auto-repeat:
   every menu here has at most two rows.
5. UX-5: The lobby retries forever, pausing lobbyRetryPauseTicks = 30 (1 s)
   between candidate-list passes, with a per-candidate attempt budget of 150
   ticks (5 s) and a HELLO retransmit every tick, as the peer-link contract
   requires (Retransmit before Poll on every tick while HANDSHAKING); the
   adapter only accepts a retransmit interval of 1. A REJECTED handshake is
   never retried automatically.
6. UX-6: An in-race link failure outranks a same-tick race finish, because a
   desynced or dropped race's standings cannot be trusted.
7. UX-7: A rematch needs both players to choose REMATCH, and REMATCH is the
   default focus on the results screen. The rematch wait times out after
   rematchWaitTimeoutTicks = 300 (10 s) and shows OPPONENT LEFT for
   opponentLeftNoticeTicks = 90 (3 s).
8. UX-8 (superseded by match select, docs/MATCH_SELECT_MILESTONE.md; the
   fixture below is now only the lobby base config, and SEL-12 replaces
   the fixed first seed): One fixed fixture per build (track, laps,
   characters, bot difficulty), not a per-cabinet selection menu. The
   fixture is track 3
   (CRASH_COVE), 3 laps, a 30/1 tick rate, CAB1 Crash (character 0), CAB2
   Cortex (character 1), and bots decided by the roster milestone
   (docs/ROSTER_MILESTONE.md): the retail 2P AI set for the two human
   characters (RS-4; Polar, N. Gin, Tiny, and Coco for Crash and Cortex)
   at one global difficulty, medium 0xA0 (RS-3). Match select keeps that
   rule and that difficulty for the characters it resolves. The first
   match always uses the fixed seed 0x4354524e41524331 ("CTRNARC1");
   each rematch derives a new seed (UX-7).
9. UX-9: The in-race stall timeout is 90 ticks (3 s at the 30 Hz loop); a
   WAITING FOR OPPONENT overlay appears after 15 stalled ticks (0.5 s).
10. UX-10: Screen layout and strings as in section 2.4; a results idle
    timeout of resultsIdleTimeoutTicks = 900 (30 s) returns an abandoned
    cabinet to the title/attract loop; matchFoundHoldTicks = 45 (1.5 s);
    exitHoldTicks = 60 (2 s).
11. UX-11: In arcade-link mode the retail main-menu box is replaced by the
    attract prompt from the title intro's menu-ready frame on, so the retail
    box never slides in and no retail menu or submenu is reachable; START or
    CROSS enters the lobby; the retail title scene and demo attract loop keep
    running while the screen is OFF.

### How to review the flow

Run these from the repository root in a Windows command prompt, with
assets/ctr-u.bin in place (the executable finds assets/ from its own
directory), from a connected desktop session (with no display the game exits
1; see risk 12). Captures go to the gitignored debug\captures folder. Frame
1320 is past the title intro's menu-ready frame, so the preview screen is up,
and the title preview's blinking PRESS START is visible on it.

The capture BMPs are 32-bit, and their alpha byte is the PS1 mask bit, not
opacity (docs/TEXTURE_FILTER_MILESTONE.md section 5). Most pixels have alpha
0, and the translucent panel writes mask bit 0, so a viewer or converter that
honours BMP alpha (for example System.Drawing Image.FromFile saved as PNG)
shows most of the screen, including the whole panel rectangle, as white or
transparent with only fragments of the title art. That is a viewing
artefact, not a rendering bug; review the RGB only.

The recommended path is one command. It renders all 17 previews (the 12
screens of section 2.4 and the five match-select screens) plus the default
path in parallel (measured runs of the 17 previews plus the default took
about 45 to 48 seconds wall time in ctest), captures frame 1320 of
each, checks each capture with the RGB checker, and with -Png writes
alpha-stripped review PNGs. -OutputDirectory must be an absolute path:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\arcade-link-preview-check.ps1 -Executable build-msvc-x86\Debug\ctr_native.exe -Checker build-msvc-x86\Debug\ctr_native_arcade_link_capture_check.exe -OutputDirectory "%CD%\debug\captures" -IncludeDefault -Png
```

Open the `<screen>-review.png` files in debug\captures. Each preview should
show a dark translucent panel with a grey frame behind the text, the section
2.4 strings, and, on the four results screens, the REMATCH highlight. The
five select previews show the widened select panel and the screens of
docs/MATCH_SELECT_MILESTONE.md section 2.8, with the cursor highlight on
select-character, select-track, and select-laps.
`default-review.png` is the default path (no arcade-link option) at the same
frame, showing the retail title and main-menu box, for comparison; the
checker must reject it. The script exits 0 on pass, 1 on fail, and 77 (skip)
when the disc image is absent, no display is available, or the build rejects
the internal-only preview option. -Screens a,b limits the run to named
screens, and -Frame and -ExitFrame move the capture. A single BMP can be
checked with `build-msvc-x86\Debug\ctr_native_arcade_link_capture_check.exe
<capture.bmp> <screen>` (exit 0 pass, 1 fail, 2 usage, IO, or parse error).
The checker's limits are in risk 14.

To capture one screen by hand, each line below writes one BMP and exits
(about 45 seconds each). View the BMPs in a viewer that ignores BMP alpha, or
use the -Png review path above:

```bat
mkdir debug\captures
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview title --capture-frame 1320=debug\captures\link-title.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview lobby --capture-frame 1320=debug\captures\link-lobby.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview lobby-connecting --capture-frame 1320=debug\captures\link-lobby-connecting.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview lobby-rejected --capture-frame 1320=debug\captures\link-lobby-rejected.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview match-found --capture-frame 1320=debug\captures\link-match-found.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview results --capture-frame 1320=debug\captures\link-results.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview results-timeout --capture-frame 1320=debug\captures\link-results-timeout.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview results-desync --capture-frame 1320=debug\captures\link-results-desync.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview results-link-error --capture-frame 1320=debug\captures\link-results-link-error.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview rematch --capture-frame 1320=debug\captures\link-rematch.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview exit --capture-frame 1320=debug\captures\link-exit.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview exit-opponent-left --capture-frame 1320=debug\captures\link-exit-opponent-left.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview select-character --capture-frame 1320=debug\captures\link-select-character.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview select-track --capture-frame 1320=debug\captures\link-select-track.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview select-laps --capture-frame 1320=debug\captures\link-select-laps.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview select-wait --capture-frame 1320=debug\captures\link-select-wait.bmp --exit-after-frame 1330
build-msvc-x86\Debug\ctr_native.exe --arcade-link-preview select-result --capture-frame 1320=debug\captures\link-select-result.bmp --exit-after-frame 1330
```

To try the live lobby on one machine, start two instances of the same build
in two command prompts, each on its own port and pointing at the other over
loopback:

```bat
build-msvc-x86\Debug\ctr_native.exe --arcade-link cab1 --arcade-link-port 7001 --arcade-link-peer 127.0.0.1:7002
build-msvc-x86\Debug\ctr_native.exe --arcade-link cab2 --arcade-link-port 7002 --arcade-link-peer 127.0.0.1:7001
```

The build must come from a clean tree, or `--arcade-link` refuses to start
(risk 5). The screens read only player 1 (local pad 0) of their own instance,
so the second instance needs keyboard or pad input routed to its player 1:
focus its window, and if a connected pad has claimed player 1 and moved the
keyboard off it, press F4 (internal builds) until the log reports "Keyboard
assigned to player 1". The default keys are Enter for START, C for CROSS,
and Z for TRIANGLE. Once both have entered the lobby, each should show
OPPONENT FOUND, then the match-select screens (each item auto-locks after
20 s, so an idle run takes about a minute), MATCH SET, and after the
relink and the launch agreement the linked race: since Task 7 START_RACE
logs the agreed match and launches the race, which until Task 8 is the
undriven launch rehearsal (docs/RACE_LAUNCH_MILESTONE.md RL-10), then
RESULTS (RACE COMPLETE) with REMATCH and EXIT back on the main-menu level.

### 3.1 Arcade-link menu sound defaults

These are new defaults, pending owner review (not yet accepted). The pure
decision is game/MAIN/MainArcadeLinkSound.{c,h}; the hook
(game/MAIN/MainArcadeLink.c) calls it on the view the drawer already reads
and plays the result. A "failure end reason" is PEER_TIMEOUT, DESYNC,
LINK_ERROR, or OPPONENT_LEFT. A "fresh failure entry" is a screen change
onto RESULTS with any failure end reason, or onto any other screen whose
failure end reason differs from the previous frame's; RESULTS always sets
its reason afresh, while EXIT, REMATCH_WAIT, and a rematch's screens can
carry an old one. OPPONENT_LEFT never reaches RESULTS today (the flow sends
it to EXIT, platform/native_arcade_flow.c:360-361), but the decision would
treat it there like the other three.

1. SND-1: Retail IDs, each played as `OtherFX_Play(id, 1)`: MOVE 0
   (game/RECTMENU.c:799, game/230/MM_Characters.c:1156,
   game/230/MM_TrackSelect.c:635), CONFIRM 1 (game/RECTMENU.c:845,
   game/230/MM_Characters.c:1216, game/230/MM_TrackSelect.c:677,
   game/230/MM_Battle.c:500), BACK 2 (game/RECTMENU.c:817,
   game/230/MM_Characters.c:1239 and 1246), ERROR 5, the locked-row womp
   (game/RECTMENU.c:866). At most one cue per frame; a local-input cue
   beats a transition cue.
2. SND-2: Local only. Cues come from the host view's localMenuEvent, the
   policy's enterPressed, and this cabinet's own screen, lobbyStatus,
   endReason, selectedRow, select status, and
   select.humans[select.localHuman]. The decision input carries nothing
   else, so a peer's entry, peerLockedCharacterMask, and peer cursor
   movement never produce a cue.
3. SND-3: MOVE on PREV/NEXT when the focus changed on the same screen: the
   results row, or on SELECT the local cursor value of the item that was
   current. No change, silent (retail RECTMENU plays nothing when the row
   does not change).
4. SND-4: CONFIRM on a local CONFIRM that took effect: the screen changed
   (not to a fresh failure entry), the local lockMask gained a bit, or the
   local currentItem advanced. A CONFIRM on a LOBBY showing REJECTED also
   counts: the flow retries that lobby on CONFIRM without changing screen.
   Also CONFIRM when enterPressed took the attract screen (OFF) into the
   lobby.
5. SND-5: ERROR on a CONFIRM on SELECT (select active, status PICKING) that
   changed nothing, which is the select refusing a peer-locked character.
   A no-effect CONFIRM elsewhere is silent: a connecting lobby, the EXIT
   hold, RESULTS before its rows accept input, MATCH_FOUND, REMATCH_WAIT,
   SELECT_RESULT, or SELECT once the local picks are done.
6. SND-6: BACK on a local BACK that took effect: the screen changed (LOBBY
   or REMATCH_WAIT to EXIT), the RESULTS focus jumped to EXIT, the local
   lockMask lost a bit, or the local currentItem went back. No effect,
   silent (SELECT ignores BACK, SEL-7).
7. SND-7: CONFIRM when the local lockMask gained a bit without an effective
   local CONFIRM (the item timer auto-locked).
8. SND-8: The opponent locking a pick is silent.
9. SND-9: Entering MATCH_FOUND plays CONFIRM. A fresh failure entry, or the
   lobby status changing to REJECTED, plays ERROR. Lobby LOST is silent.
   LOST comes only from a READY link that faulted (lobby PEER_LOST); a
   retry nobody answers reads WAITING or CONNECTING, so LOST does not
   flicker during retries, though it could recur once per 1 s retry cycle
   if each new link reached READY and then faulted. REJECTED cannot
   flicker: it is never retried automatically (UX-5) and ends REMATCH_WAIT
   and SELECT_RESULT; it recurs only after a local CONFIRM retry is
   rejected again, which gives CONFIRM and then ERROR each time. Every other
   peer-driven transition is silent.
10. SND-10: PREVIEW is silent (scripted, no input). The first frame after the
    snapshot was reset is silent, including an attract entry on that frame.
    The hook resets the snapshot on every frame the layer does not own, in
    PREVIEW, when GetView fails, and on RETURN_TO_TITLE (until Task 7 also
    after the START_RACE AbortToTitle; since RL-S8b START_RACE hands the
    launch to the race caller and keeps the snapshot, which goes on
    tracking the RACING flow). With the host mode OFF nothing changes: the OFF early
    return is still the first statement. RL-S8a amendment: race frames under
    RL-8 (tickOnly) are the exception to the not-owned reset; they leave the
    snapshot holding the RACING view stored on the last owned frame, so
    RESULTS entry keeps its SND-9 cue.
11. SND-11: Non-canonical host-local presentation. `OtherFX_Play`
    (game/HOWL/HOWL_OtherFX.c:19) calls OtherFX_Play_LowLevel (:39), which
    reads sdata->boolAudioEnabled, ptrHowlHeader, howl_metaOtherFX,
    howl_spuAddrs, vol_FX or vol_Voice, boolStereoEnabled, and
    gGT->frameTimer_MainFrame_ResetDB. It writes sdata->criticalSectionCount
    (game/HOWL/HOWL_Channel.c:3-31), the channelTaken and channelFree lists,
    ChannelUpdateFlags, channelAttrNew, and one ChannelStats entry
    (HOWL_Channel.c:74-155), and sdata->countSounds (CountSounds,
    HOWL_OtherFX.c:3). With anti-spam flag 1 it may also recycle the same
    sound's channel if that sound started less than 10 frames ago
    (Channel_DestroySelf, HOWL_Channel.c:110). It never touches
    sdata->audioRNG, randomNumber,
    advRng, or any other replay observation field. These are the fields every
    retail menu sound already touches (game/RECTMENU.c:799). The channel
    lists are inside sdata, which checkpoints capture (they are relocated in
    platform/native_checkpoint.c:1442-1443), but link and preview mode reject
    every replay option and disable quick states (section 2.5), so no
    checkpoint or replay is taken there. The host never falls back to mode
    OFF during a run: since Task 7 no game code calls
    NativeArcadeLinkHost_AbortToTitle (whose re-initialization failure was
    the only fallback path), and main.c calls NativeArcadeLinkHost_Shutdown
    only on a startup failure and at exit. So quick states stay disabled for
    the whole link run.
    The arcade-link screens are menus
    outside any race frame, and audio is presentation (RS-8,
    docs/ROSTER_MILESTONE.md).

## 4. Constraints

1. The topology lease is untouched: no acquire, activate, capture, or
   publish; no lease owner in checkpoints, replay, or canonical state; no
   retire hook on LOAD_Hub_ReadFile.
2. No edits to the lockstep protocol, window, session, failure-handling,
   handshake, peer-link, lobby-state, UDP-transport, or match-config
   modules. This milestone calls into them.
3. The existing lockstep and failure-handling isolation rules on game/ stay
   exactly as strict as they are.
4. No dynamic allocation in any new module; portable C17 with extensions off
   on every new target.
5. Every new seam gets a unit test; every structural rule gets an isolation
   test. New game .c files go in game/game_unity.h.
6. Default behaviour (no `--arcade-link` option) is unchanged; the full
   replay and network suites must stay green.
7. A task is done only when `cmake --build build-msvc-x86 --config Debug`
   succeeds and the full `ctest --test-dir build-msvc-x86 -C Debug` suite
   passes. Commit on `arcade` only; no push.

## 5. Task list

Baseline before this milestone: 91 tests, 100% passing (commit 52976808c).
Current state: 135 tests, 100% passing, at the close of Tasks 1-6b-7
(111 at the close of Tasks 1-6b-6; 119 after the match-select milestone,
docs/MATCH_SELECT_MILESTONE.md). Tasks 1-6b-7 are done; Tasks 7 and 8 are
gated (see their entries); this document stays open until they land.

### Task 1 -- this document

Status: done (d41689823).

### Task 2 -- menu input seam (native_arcade_menu_input)

Status: done (079abe00e). Header, implementation, unit test, isolation test
(pure: no game, socket, lockstep, clock, lease, or allocation token), C17
target.
No review required: pure input classification, no identity or replay state.
Landed as include/platform/native_arcade_menu_input.h,
platform/native_arcade_menu_input.c, tests/native_arcade_menu_input_test.c,
and tests/native_arcade_menu_input_isolation_test.cmake (library
ctr_native_arcade_menu_input, tests native_arcade_menu_input_unit and
native_arcade_menu_input_isolation).

### Task 3 -- screen flow seam (native_arcade_flow)

Status: done (7c1dd8dfc). Header, implementation, a unit test covering every
transition in section 2.2 and every timing default, an isolation test
(pure, same token rules as task 2), C17 target. No review required.
Landed as include/platform/native_arcade_flow.h,
platform/native_arcade_flow.c, tests/native_arcade_flow_test.c, and
tests/native_arcade_flow_isolation_test.cmake (library
ctr_native_arcade_flow, tests native_arcade_flow_unit and
native_arcade_flow_isolation).

### Task 4 -- host adapter (native_arcade_netplay)

Status: done (3f8ea1810, review fixes 7ccd2de04 and ffd6a8e73). Header,
implementation, a unit test over real loopback
sockets with two in-process adapters (lobby to READY, rematch agreement to
READY on a new seed, one-sided rematch to OPPONENT LEFT, rejection, backing
out), a pure test of the cause mapping and the rematch seed, a stall-timeout
test that feeds STALL results through the race-time hook on a real loopback
race (two adapters in RACING, no bundles exchanged), and an isolation test
(no lease, canonical-state, or replay write tokens, no allocation, and API
names free of the tokens forbidden under game/). Review required: it
decides which config a rematch runs on.
Landed as include/platform/native_arcade_netplay.h,
platform/native_arcade_netplay.c, tests/native_arcade_netplay_test.c, and
tests/native_arcade_netplay_isolation_test.cmake (library
ctr_native_arcade_netplay, tests native_arcade_netplay_unit and
native_arcade_netplay_isolation).

Task 4b (review fixes): the default HELLO retransmit interval is now every
tick (it was 15 ticks, which hung one cabinet when the two entered, or
confirmed a rematch, at different ticks); the adapter latches a fault or
divergence its own lobby poll finds during RACING, so the flow shows DESYNC
or LINK ERROR from the real cause; a rematch whose config cannot be built
now blocks every restart instead of reusing the old seed; Init rejects a
zero local port, zero candidates, and a zero retransmit interval; and the
platform-only hooks are documented. New loopback tests cover a staggered
Enter and a staggered rematch at the production cadence, a real in-race
fault (a corrupted bundle) and a real in-race divergence (drifting
synthetic digests over the real links) found by the lobby poll, the
blocked rematch, BACK from REMATCH_WAIT, and Begin failure on an occupied
port followed by recovery. The isolation test now requires exactly seven
linked libraries (ctr_native_arcade_menu_input is linked explicitly) and
the retransmit default of 1u.

Review outcome: the Task 4 review found a blocking HELLO-cadence bug, fixed
in Task 4b (7ccd2de04); the re-review was clean, and Task 4c (ffd6a8e73)
closed the remaining should-fix.

Task 4c (last review items): Init now rejects any retransmit interval other
than 1, not only 0, since every other cadence reintroduces the staggered
handshake hang. Two new loopback tests drive the race-time hook
NativeArcadeNetplay_OnTakeResult itself on a latched cause, with the race
driver polling the peer link directly before any adapter Tick: a corrupted
bundle latches FAULTED (LINK ERROR), and a real drifting-digest race
latches DIVERGED (DESYNC) on both sides; each asserts the latched report,
the pending failure, the remote slot dropped and the local slot kept ACTIVE
in the roster, and one Tick to RESULTS.

### Task 5 -- screen layout builder (MainArcadeLinkScreens layout)

Status: done (457a18f96). A pure standalone library under game/MAIN with a
unit test
of every screen's draw list (strings, positions, focus colour, dot
animation), and an isolation test that it names no lockstep or
failure-handling token and touches no game global.
Landed as game/MAIN/MainArcadeLinkLayout.h, game/MAIN/MainArcadeLinkLayout.c,
tests/main_arcade_link_layout_test.c, and
tests/main_arcade_link_layout_isolation_test.cmake (library
ctr_native_arcade_link_layout, tests main_arcade_link_layout_unit and
main_arcade_link_layout_isolation).

### Task 6a -- host options and fixture (native_arcade_link_options)

Status: done (8e49efcc7). Landed as
include/platform/native_arcade_link_options.h,
platform/native_arcade_link_options.c,
tests/native_arcade_link_options_test.c, and
tests/native_arcade_link_options_isolation_test.cmake (library
ctr_native_arcade_link_options, tests native_arcade_link_options_unit and
native_arcade_link_options_isolation); reaches ctr_native through the host
glue (Task 6b-2).

### Task 6b-1 -- host glue (native_arcade_link_host)

Status: done (b06a1ca3f). Landed as
include/platform/native_arcade_link_host.h,
platform/native_arcade_link_host.c, tests/native_arcade_link_host_test.c,
and tests/native_arcade_link_host_isolation_test.cmake (library
ctr_native_arcade_link_host, tests native_arcade_link_host_unit and
native_arcade_link_host_isolation); linked into ctr_native by Task 6b-2.

### Task 6b-2 -- live hook, dormant by default

Status: done (3f89d8e39). The option parser and host glue are hooked into
main.c, the
host glue is linked into ctr_native, and the CTR_NATIVE-only drawer and hook
are in the unity chain: title-screen entry into LOBBY, START_RACE aborting to
the title (replaced in Task 7 by the hand-off to the live race caller,
docs/RACE_LAUNCH_MILESTONE.md RL-S8b), RETURN_TO_TITLE back to the title/attract loop, and
the internal-only preview option (section 2.5). Review required: it touches
the game loop, even though default behaviour is unchanged. Review outcome:
the review found a blocking bug (the retail main-menu box could slide in),
fixed in Task 6b-3 (827b65f29); the re-review was clean.
Landed as game/MAIN/MainArcadeLink.h, game/MAIN/MainArcadeLink.c (with
game/MAIN/MainArcadeLinkLayout.c, both unity-included from
game/game_unity.h after the 230 overlay), the hook in
game/MAIN/MainFrame_RenderFrame.c, the option, identity, configure, and
shutdown wiring in main.c, the ctr_native link of
ctr_native_arcade_link_host in CMakeLists.txt, and
tests/main_arcade_link_hook_isolation_test.cmake (test
main_arcade_link_hook_isolation). ctr_native does not link
ctr_native_arcade_link_layout; that library remains for its unit test.
Linking the host glue adds no duplicate symbol: main.c already
unity-includes platform/native_sha256.c, and ctr_native_sha256 (reached
through the options library, and already through ctr_native_match_config)
is a static library whose members the linker never pulls because main.obj
defines their symbols; the host chain does not link ctr_native_identity.

### Task 6b-3 -- live hook review fixes

Status: done (827b65f29). Fixes the review findings on the Task 6b-2 hook
(section 2.5); the re-review was clean.
The layer now owns the frame whenever the retail main-menu box could be
visible or take input: INTRO from TITLE_INTRO_MENU_READY_FRAME on (the
12-frame slide-in, and the intro skip to frame 1000), IN_MENU, EXITING, and
RETURNING, judged on the menu the retail menu system processes this frame,
so the leaky frame where IN_MENU is set inside the funcPtr is gone and an
open submenu no longer releases the frame (the retail menu hierarchy is
unreachable). Owned frames clear every pad's taps before the retail menu
code runs, so cheat entry and the intro-skip tap cannot fire. If
AbortToTitle falls back to mode OFF the hook restores the box in the same
call (removed with the START_RACE abort in Task 7, RL-S8b). main.c rejects `--arcade-link` or `--arcade-link-preview` with any
replay record, playback, or report option (quick-state hotkeys, then
documented as unsupported in link and preview mode, are disabled there by
Task 6b-4). PREVIEW follows the
link-mode ownership rule instead of owning every frame from boot, and resets
the demo countdown on every owned frame. The START_RACE notice logs through
Platform_Log. The decision logic moved into a pure, unit-tested policy.
Landed as game/MAIN/MainArcadeLinkPolicy.h and
game/MAIN/MainArcadeLinkPolicy.c (unity-included from game/game_unity.h
between the layout and the hook; standalone library
ctr_native_arcade_link_policy, which links nothing and which ctr_native
does not link), tests/main_arcade_link_policy_test.c and
tests/main_arcade_link_policy_isolation_test.cmake (tests
main_arcade_link_policy_unit and main_arcade_link_policy_isolation), the
thinned hook game/MAIN/MainArcadeLink.{c,h}, the seam comment in
game/MAIN/MainFrame_RenderFrame.c, the replay-option rejection in main.c,
and extended checks in tests/main_arcade_link_hook_isolation_test.cmake
(identity read only in the link-enabled branch, the input clear inside the
retail collect block, the policy include and unity order, the replay-option
rejection, and no stdio in the hook).

### Task 6b-4 -- quick states and hook nits

Status: done (dbf9f942c). Closes the last review items on the live hook
(section 2.5).
The F5 and F8 quick-state hotkeys in platform/native_platform.c do nothing
but log one warning per press while the host mode is not OFF, so a state
saved in link or preview mode can no longer carry the hidden retail box
into a later normal run; with no arcade-link option they are unchanged.
The RETURN_TO_TITLE branch now copies the retail demo-mode exit exactly
(boolDemoMode = 0, numPlyrNextGame = 1, mainMenuState = MAIN_MENU_TITLE,
MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL)), and the MainArcadeLink_Frame
comment no longer claims the box is always hidden when it returns 1 (the
AbortToTitle fallback frame gives it back). New isolation checks: every
quoted "--..." option in platform/native_replay_scheduler_seam.c is in
main.c's replay-rejection list, the F5 and F8 cases are gated by
NativeArcadeLinkHost_Mode, and the START_RACE AbortToTitle call is followed
in its branch by the host-mode OFF check that restores the box. (Task 7,
RL-S8b, removed that call and its check; the hook isolation test now
forbids AbortToTitle in the hook and the race caller.)
Landed as platform/native_platform.c, game/MAIN/MainArcadeLink.c,
game/MAIN/MainArcadeLink.h, and
tests/main_arcade_link_hook_isolation_test.cmake (test
main_arcade_link_hook_isolation). native_savestate.c and
native_checkpoint.c are unchanged. Review outcome: the reviewer's verdict
was clean with nothing blocking; its one should-fix and two nits were fixed
in Task 6b-5.

### Task 6b-5 -- review follow-up (docs, comments, isolation test)

Status: done (dd89ff9a5). Section 2.5 now lists boolDemoMode 0 in the
RETURN_TO_TITLE exit, the hook header comments name Tasks 6b-2 to 6b-4, and
the isolation test now fails if NativeSaveState_RequestSave or
NativeSaveState_RequestLoad is named in any platform/, game/, or main.c
source other than native_platform.c (native_savestate.c, which defines
them, is excluded). Docs, comments, and an isolation test only: verified by
the full suite, not re-reviewed.

### Task 6b-6 -- preview render check and review-capture fix

Status: done (59184279d, a09cdd26c). Investigated a report that all 12
`--arcade-link-preview` captures looked like a white screen with only
fragments of the title art. Root cause: not a rendering bug. The capture BMP
is 32-bit and its alpha byte is the PS1 mask bit
(docs/TEXTURE_FILTER_MILESTONE.md section 5); in a frame-1320 preview
capture 471,319 of 480,000 pixels have alpha 0 (the default-path capture:
408,437), so a converter or viewer that honours BMP alpha (for example
System.Drawing Image.FromFile saved as PNG) shows those pixels transparent
or white. The translucent panel writes mask bit 0, which is why the white
area matched the panel rectangle. With alpha forced to 255 the RGB shows
every screen correctly: a dark translucent panel with a grey frame behind
the text, the section 2.4 strings, and the REMATCH highlight on the four
results screens. The default path at frame 1320 shows the retail title and
main-menu box. No game code changed; "How to review the flow" (section 3)
now leads with an alpha-stripped review path.
Landed as include/platform/native_capture_check.h,
platform/native_capture_check.c, tests/native_capture_check_test.c, and
tests/native_capture_check_isolation_test.cmake (library
ctr_native_capture_check, a pure offline RGB-only checker for 24 and 32 bpp
BMP captures: panel frame, dimmed translucent fill that is neither flat nor
white, text in each expected line band, empty unused bands, and the REMATCH
highlight on results screens; tests native_capture_check_unit, on synthetic
frames only, and native_capture_check_isolation, which keeps the library free
of game, SDL, and runtime code and out of ctr_native); the CLI
tools/arcade_link_capture_check.c (executable
ctr_native_arcade_link_capture_check); tools/arcade-link-preview-check.ps1;
and the ctest arcade_link_preview_render in CMakeLists.txt (WIN32 only,
RUN_SERIAL; it runs the script with -IncludeDefault, skips with code 77 when
assets/ctr-u.bin is absent, no display is available, or the build rejects
the internal-only preview option, and writes its captures and logs under
build-msvc-x86\arcade_link_preview_captures\<config>). Checker limits are in
risk 14. No review required: no frame or render order, input, identity,
replay, canonical-state, or lease change.

### Task 6b-7 -- arcade-link menu sounds (MainArcadeLinkSound)

Status: done. The arcade-link and match-select screens now play the retail
menu sounds for navigate, confirm, back, and error under the section 3.1
defaults (SND-1..SND-11, pending owner review). A pure decision picks at most
one cue per frame from this cabinet's own view fields, the view's
localMenuEvent, and the policy's enterPressed. The hook calls it on the view
the drawer already reads and plays the cue with `OtherFX_Play(id, 1)`, the
only sound call. Flow, select, netplay, and the wire are unchanged. With no
arcade-link option nothing changes.
Landed as game/MAIN/MainArcadeLinkSound.h and
game/MAIN/MainArcadeLinkSound.c (unity-included from game/game_unity.h
between the policy and the hook; standalone library
ctr_native_arcade_link_sound, which links nothing and which ctr_native does
not link), tests/main_arcade_link_sound_test.c and
tests/main_arcade_link_sound_isolation_test.cmake (tests
main_arcade_link_sound_unit and main_arcade_link_sound_isolation), the hook
wiring in game/MAIN/MainArcadeLink.c (flow-value mirror static asserts, the
snapshot and its resets, and the sound helper the drawer calls), and a
minimal extension of tests/main_arcade_link_hook_isolation_test.cmake (the
sound header in the include allow-list, the drawer's new signature, and the
file-count floor of the MainArcadeLink* scan). The sound isolation test also
enforces that the retail sound call appears only in the hook, once, after the
decision, and in no platform/native_arcade_* source or header, the policy,
or the layout.
Note for Task 7: `countSounds` (CountSounds, game/HOWL/HOWL_OtherFX.c:3)
feeds the sound IDs OtherFX_Play returns and callers keep (for example
gGT->rainSoundID, game/DropRain.c:29), and it already differs between
cabinets through ordinary retail menu use; Task 7 must keep sound IDs out
of any cross-cabinet identity. Done: RL-14, enforced by
arcade_sound_identity_isolation (docs/RACE_LAUNCH_MILESTONE.md RL-S3).

### Task 7 -- networked race launch

Status: done (RL-S1..RL-S11, 894788d2b through e5279e31d plus the RL-S11
docs close-out). Design, slices, and evidence:
docs/RACE_LAUNCH_MILESTONE.md (defaults RL-1..RL-15; section 3 records how
each done criterion is proven). START_RACE now follows a launch commit
exchanged on the peer-link aux route (the launch record,
platform/native_arcade_launch.c; the flow's launchStatus gate; the netplay
adapter's launch agreement), and the arcade-link hook hands it to the live
race caller game/MAIN/MainArcadeRaceLaunch.{c,h}, stepped by
MainFrame_RenderFrame right after the hook and driven by the pure decision
core game/MAIN/MainArcadeRaceLaunchCore.{c,h}. From the title launch window
the caller arms the live race setup seam game/MAIN/MainArcadeRaceSetup
(docs/ROSTER_MILESTONE.md section 3.2) with the exact agreed
NativeMatchConfigV1 (NativeArcadeLinkHost_GetAgreedConfig), launches it,
and leaves the title; logs one RL-12 line per validated race with the
config, race plan, bot setup plan, and bank digests
(MainArcadeRaceSetup_Digests); ends a FAILED setup, an Arm or Launch
failure, or a bounded-wait expiry as a local RESULTS LINK ERROR through
NativeArcadeLinkHost_ReportRaceFailure (RL-11, closing RS-10); returns to
the main-menu level; and Disarms once per race on the first idle main-menu
frame (at once after an Arm or Launch failure at the title; nothing to
Disarm after a window timeout), so a rematch arms a second race in the
same process. The
tests/main_arcade_race_setup_isolation_test.cmake allow-list names exactly
this one new caller file, and the adapter still never calls _Disarm.
Asymmetric relink completion is answered by the launch agreement
(RL-1..RL-7): a one-sided relink launches neither cabinet, and the residual
two-generals case is fail-safe. Race frames are ticked, not owned (RL-8,
tickOnly). The pause-menu vibration toggle is guarded while a race setup
is not IDLE (RL-13), and sound IDs are kept out of cross-cabinet identity
by an isolation test (RL-14). Retail cheat entry stays possible during the
title intro before the menu-ready frame (section 2.5, residual retail
window); RS-2 resets the gameMode2 cheat bits through the seam. Until Task
8 a linked race is the launch rehearsal (RL-10): it loads and starts on
neutral installed pads, nobody drives it, and after 150 ticks the caller
reports it finished (RESULTS, RACE COMPLETE). The live two-process gate
arcade_link_launch (RL-15, label live) has a recorded, non-skipped PASS at
e5279e31d. MainArcadeRaceSetup_Bank is not consumed yet; projecting it is
Task 8.

### Task 8 -- in-race lockstep drive and failure handling

Status: gated on live V4 canonical projection (Task 7 is done). The race
driver lives under platform/, because it calls the platform-only
NativeArcadeNetplay_OnTakeResult and NativeArcadeNetplay_Link hooks and the
lockstep session API, none of which game code may name. Per tick:
submit the local pad, compose and send, poll, take frame inputs, install
the committed pads with Platform_InputInstallPadSnapshots, record local V4
digests, feed the result to the outcome tracker and roster, hold the
simulation and show the WAITING FOR OPPONENT overlay on a stall, and hand a
latched outcome or the race finish to the flow's RESULTS screen, reusing the
retail standings drawing. Review required.

### Task 9 -- docs close-out

Status: done. Updates this document and docs/HANDOFF.md for Tasks 1-6b-6.

## 6. Risks and open questions

1. Task 7 is done (docs/RACE_LAUNCH_MILESTONE.md): the live hook reaches
   LOBBY, MATCH_FOUND, the match-select screens, a launched and VALIDATED
   linked race, RESULTS, and a rematch. The race is the undriven launch
   rehearsal (RL-10) until Task 8, which is gated on live V4 projection,
   so there is no real networked race yet: no input exchange and no
   in-race stall or desync detection beyond the adapter's lobby poll.
2. Stale bundles from a just-finished session that arrive after a rematch
   link has opened on the same port would be staged by the peer link and
   fault the new session on its match identity. Both peers stop sending
   bundles when they leave RACING and the results dwell is 1 s, so this
   needs a peer still racing more than 1 s after the other finished, which
   lockstep should prevent; task 8 must still test it explicitly.
3. All tick counts assume the retail 30 Hz loop. A future 60 Hz native mode
   would halve every duration and needs these defaults revisited.
4. The G29 menu feel (UX-1 to UX-4) is only unit tested; it is a CAB1
   live-hardware acceptance item at step 6.
5. The arcade link needs a known build and content identity, so a build made
   from a tree with uncommitted or untracked changes refuses `--arcade-link`
   ("arcade link requires a known build and content identity"). This is by
   design: both cabinets must run the identical build.
6. Resolved: the fixture's botRulesDigest is no longer a placeholder. It
   is the real bot-rules digest, NativeArcadeBotRules_DigestV1
   (docs/ROSTER_MILESTONE.md section 3.1), and every config the fixture,
   match select, or a rematch builds from it carries that digest.
7. Link and preview mode exclude replay: `--arcade-link` or
   `--arcade-link-preview` combined with any replay record, playback, or
   report option is fatal at startup. The F5 and F8 quick-state hotkeys are
   disabled in both modes; each press only logs "[CTR Native] quick states
   are disabled in arcade-link mode".
8. The title intro before the menu-ready frame stays retail (intro skip and
   cheat entry both work there). The race setup seam already resets the
   gameMode2 cheat bits: RS-2 pins every non-transient gameMode1 and
   gameMode2 bit at Launch and again at race init, and the post-drivers
   check fails closed if a cheat bit is set (docs/ROSTER_MILESTONE.md). So
   Task 7 need not reset them itself, as long as it launches through
   MainArcadeRaceSetup, which it does (the race caller,
   game/MAIN/MainArcadeRaceLaunch.c).
9. The two-instance loopback run was driven by script on one machine with
   the G29 hidden from SDL. No real two-cabinet or real-wheel run has
   happened; that is the step 6/7 requirement.
10. Stale bundles from a just-finished race arriving after a rematch opens
    could fault the new session (detail in risk 2); Task 8 must test for
    this.
11. Every tick count assumes the 30 Hz game loop (see risk 3).
12. Startup needs a display. With no display in the Windows session (e.g. a
    console session disconnected by fast user switching), SDL video init
    fails with "No displays available"; ctr_native.exe logs the SDL error and
    exits 1, never showing a dialog. The SDL_hid.c:258 assertion seen then is
    a device-notification refcount imbalance in SDL's video error path, not
    the G29; it is logged and ignored. When the exe owns its console window
    (e.g. double-clicked), every early exit waits on "Press Enter to close
    this window...".
13. Preview legibility (observed on the review PNGs, not fixed; a layout and
    colour decision for the owner). On lobby-rejected the red body lines
    (LINK REFUSED, SETTINGS DO NOT MATCH) are legible but low-contrast over
    the dark panel. On the results screens the EXIT row overlaps the title
    art's CTR logo and TM mark behind the translucent panel; it is still
    legible.
14. The preview capture checker covers all 17 preview screens, including
    the five match-select screens. Its limits are unchanged in kind: it
    cannot tell apart screens that share a layout (the four results
    screens, exit and exit-opponent-left, lobby and lobby-connecting, and
    select screens beyond their band structure). It does not check title
    wording or colour. Its thresholds were calibrated on 800x600
    nearest-filter captures only.
15. Every game run, including each run of the preview check script, writes
    `Crash Team Racing.log` into the repository root, because main.c changes
    into the base directory. The file is gitignored.
