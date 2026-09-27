# Solo-cabinet milestone

Design-and-status record for a player-facing solo (ARCADE_ONE_CAB) flow on
the arcade link, on branch `arcade`: a linked cabinet whose peer is silent
offers the player a solo race (one human and seven bots, the retail 1P
arcade race) from its LOBBY, and keeps listening for the peer in the
background. Read AGENTS.md and docs/HANDOFF.md first. This document follows
the pattern of docs/MATCH_SELECT_MILESTONE.md and
docs/GAME_LOOP_UI_MILESTONE.md: a prospective plan with a slice list,
updated to record status as slices land. The plan's line citations
(sections 1 to 5 as first written) were checked against the tree at
6e2c59abe (its code is that of e614b7327) and still cite that tree. The
status (section 6), the defaults SOLO-14 to SOLO-18, and every citation
added or changed with them in SOLO-S5 were checked against the code at
1e399ba1e and the docs as SOLO-S5 left them. Physical two-cabinet
validation stays gated behind HANDOFF steps 6 and 7.

Status: complete. All five slices have landed (section 6), and solo is
live: a LINK cabinet whose peer is not heard offers solo from its LOBBY.

## 1. Why

ONE_CAB is complete at the simulation level through the race setup seam
(game/MAIN/MainArcadeRaceSetup.h: Arm :91, Launch :95, Disarm :153). The
live test `arcade_roster_determinism_one_cab` proves it
(tools/arcade-roster-proof-check.ps1 `-Group one-cab`,
CMakeLists.txt:2095-2109).
That proof (RS-23, docs/ROSTER_MILESTONE.md:704-710) launches from the title
with no lobby and no select.

No player-facing ONE_CAB flow existed before this milestone:

- docs/ROSTER_MILESTONE.md RS-1 (:545-553): "the arcade-link lobby, and match
  select remain TWO_CAB-only [...] a ONE_CAB lobby/UI flow is a follow-up
  (risk 14)"; risk 14 (:1095-1101).
- docs/RACE_LAUNCH_MILESTONE.md risk 9 (:1037): "The ONE_CAB lobby and UI
  flow is a follow-up".

Before this milestone a linked cabinet whose peer was off sat on the LOBBY
forever (UX-5, docs/GAME_LOOP_UI_MILESTONE.md:516); since S4 it offers solo
after SOLO_OFFER_DELAY (SOLO-2). With the default attempt budget of
150 ticks per candidate
(include/platform/native_arcade_netplay.h:201), one cycle is 150 ticks of
CONNECTING (a HELLO every tick) per candidate, so longer with several peer
entries, and then WAITING. After lobbyRetryPauseTicks = 30
(include/platform/native_arcade_flow.h:64) the flow restarts the cycle
(platform/native_arcade_flow.c:141-153, :179-182).
BACK (Triangle) goes to EXIT and the title (:158-163, :380-392).

Owner direction:

- One versus two cabinets is not a static setting. It follows from whether
  the other cabinet is heard, and a continuous background poll checks for it
  waking (docs/HANDOFF.md:841-847).
- RS-1 is itself the owner decision that overrode the TWO_CAB-only
  default: both ONE_CAB and TWO_CAB are supported
  (docs/ROSTER_MILESTONE.md:541-553). It left the ONE_CAB lobby/UI flow as
  a named follow-up (risk 14), and this milestone carries that follow-up
  out.
- Auto-discovery (subnet announce) is a later milestone. For now the peer is
  the static `peer` of `arcade.cfg`, part of the all-or-none link group
  (include/platform/native_arcade_config.h:41-46). The solo flow must let
  discovery plug in later.

## 2. What exists

- Flow core: platform/native_arcade_flow.c, pure
  (NativeArcadeFlow_Tick :394-444).
  - Screens OFF, LOBBY, MATCH_FOUND, RACING, RESULTS, REMATCH_WAIT, EXIT,
    SELECT, SELECT_RESULT (include/platform/native_arcade_flow.h:79-90).
  - Lobby statuses WAITING, CONNECTING, READY, REJECTED, LOST (:93-100).
  - Actions (:112-123).
  - Observation (:158-174). One reserved byte is left (:173), and a static
    assert pins the size at 12 bytes (:177).
  - RESULTS rows REMATCH and EXIT (:75-77). The EXIT row, and the RESULTS
    idle timeout, enter EXIT with CLOSE_LINK (native_arcade_flow.c:328-329,
    :336-345). EXIT returns RETURN_TO_TITLE after its hold (:380-392).
  - In LOBBY, BACK is checked before READY, and CONFIRM acts only on
    REJECTED (:155-186).
  - SELECT treats any lobby status other than READY as LINK ERROR
    (:218-224).
  - The nine default timings (native_arcade_flow.h:64-72) are frozen by
    tests/native_arcade_flow_isolation_test.cmake:117-137. That test also
    limits the flow's includes and links (:69-95).
- Menu input: platform/native_arcade_menu_input.c. It uses rising edges with
  release-to-arm. CONFIRM is CROSS or START, and CROSS is also the G29
  throttle (include/platform/native_arcade_menu_input.h:55-56). BACK is
  TRIANGLE only (:57-58). The adapter re-arms the input on every screen
  entry (platform/native_arcade_netplay.c:745-751). A LOBBY restart is not a
  screen entry.
- Lobby and socket: platform/native_lobby_state.c (NativeLobbyState_Poll
  :181-197; modes in include/platform/native_lobby_state.h:48-55).
  - The UDP socket belongs to NativeLockstepPeerLink
    (include/platform/native_lockstep_peer_link.h:81-84). Opening a link
    sends the first HELLO (:158-174).
  - A candidate whose budget runs out is closed. With no candidate left the
    lobby is WAITING with no socket open (native_lobby_state.c:141-166), and
    Poll does nothing in WAITING (:195-196).
  - The adapter polls the lobby on every tick (native_arcade_netplay.c:
    PollLobby :167-174, called at :707). CLOSE_LINK closes the lobby and its
    socket (CloseLobby :190-198, executed at :833-837).
  - The adapter is the only module that names both the lobby and the flow.
- Match select: NativeMatchSelectSession accepts humanCount 1..4
  (include/platform/native_match_select_session.h:150-160).
  - With one human the peer loops are empty. The session goes CONFIRMED on
    the same tick it resolves (platform/native_match_select_session.c:183-208;
    the header says "vacuously true for one human", :44-46). Peer silence
    never fires (:368-389).
  - The adapter passes localHuman = localRole - 1
    (native_arcade_netplay.c:412-413) and takes the initial cursor from the
    base slot of the local role (:405-407).
- Select rules: NativeMatchSelect_Resolve
  (platform/native_match_select_rules.c:242-367).
  - Two humans with four bots use the LOAD_Robots2P branch (:329-343).
    Any other shape uses the "provisional" branch (:344-361): the base
    characters no human holds, taken in k_matchSelectCharacters order.
  - k_matchSelectCharacters is {0, 1, ..., 7} in ascending order (:16).
  - `taken` marks only the humans' characters (:281-282).
  - The seed is NativeMatchSelect_DeriveSeed over the base digest, the base
    masterSeed, and the humans' nonces (:285-286).
  - NativeMatchSelect_BuildConfig builds a one-human outcome only on a
    one-human (ONE_CAB) base. It keeps the base's profile and botRulesDigest,
    and outside the 2P shape it does not check which characters the bots
    are (include/platform/native_match_select_rules.h:15-21, :158-190).
- Bot rules: include/platform/native_arcade_bot_rules.h.
  - ONE_CAB is the LOAD_Robots1P rule: for human h, the bots are {0..7}
    without h, ascending (:28-44).
  - The API is ExpectedBots1P :253, Digest1PV1 :228, and ValidateConfigV1
    :294.
- Race launch: game/MAIN/MainArcadeRaceLaunch.c (as planned; S4 changed
  the config choice and InstallPads, SOLO-15 and SOLO-16).
  - The race config comes from NativeArcadeLinkHost_GetAgreedConfig
    (:368, 6e2c59abe lines), followed by Arm and Launch (:373-377,
    6e2c59abe lines).
  - Each tick goes through NativeArcadeLinkHost_RaceStep (:893); RaceHold
    (:699) runs only while RaceStep returns HOLD.
  - End kinds are handled at :817-842. Disarm is RL-9 (:999-1008).
  - InstallPads hard-codes PROFILE_TWO_CAB (:258-264).
  - With no config, Arm refuses; the core turns that into an RL-11 local
    failure (game/MAIN/MainArcadeRaceLaunchCore.c:365-368), which the
    caller reports through NativeArcadeLinkHost_ReportRaceFailure
    (MainArcadeRaceLaunch.c:366-376, :950-959).
  - tests/main_arcade_race_setup_isolation_test.cmake:9-14 allows Arm,
    Launch, and Disarm only in MainArcadeRaceSetup.{c,h},
    MainArcadeRosterProof.c, and one Task 7 caller, this file, which names
    each exactly once.
- Roster proof ONE_CAB config: NativeArcadeRosterProof_BuildOneCabConfig
  (platform/native_arcade_roster_proof.c:389-457).
  - The link fixture (NativeArcadeLinkFixture_Build) supplies the identity,
    track, laps, tick rate, CAB1 character, and bot difficulty.
  - It calls NativeMatchConfigV1_InitArcadeOneCab, fills the bots from
    ExpectedBots1P, sets Digest1PV1, and checks the result with
    ValidateConfigV1.
- Link host API (include/platform/native_arcade_link_host.h): Configure
  :235 (LINK mode starts dormant, no socket), Enter :245, Tick :256,
  GetView :259 (view :150), GetAgreedConfig :272, TakeRaceEnd :291, Racing
  :305, RaceBegin :332 (fixed VBlank pacing on, drive begun), RaceEnd :353,
  RaceStep :460, RaceHold :475.
- Link options: `--arcade-link` and `--arcade-link-preview` cannot be
  combined with replay record or playback (main.c:373-376).
- Autopilot: `--arcade-link-autopilot` and `-race-ticks`
  (include/platform/native_arcade_link_autopilot.h:15-31) drive the menus
  and the steering, but only for the fixed two-cabinet, three-race run
  (:51-55). (S4 added the solo mode, SOLO-18.)

## 3. SOLO defaults

The owner delegated these. Where the direction left a choice open, each
default takes the safer option.

1. SOLO-1 (default): Scope. Only a cabinet with a link group (`arcade.cfg`
   or the `--arcade-link` flags) runs the arcade-link flow, and solo is
   offered from its LOBBY. A cabinet without a link group is unchanged.
   Reason: solo is the answer to a silent peer, not a new mode.
2. SOLO-2 (default): Offer. The flow offers solo when the lobby status has
   been WAITING, CONNECTING, or LOST (peer not heard) continuously for
   SOLO_OFFER_DELAY = 90 ticks (3 s).
   - The count starts when the LOBBY is entered and restarts on REJECTED.
     READY always leaves the LOBBY, so a return from MATCH_FOUND is a new
     entry.
   - A LOBBY restart does not re-enter the screen
     (native_arcade_flow.c:179-182), so the count spans the retry restarts.
   - There is no offer on REJECTED: CONFIRM there restarts the lobby
     (:169-178), and a mismatch is an operator fault.
   - "Peer not heard" includes a peer that is powered on but sits on its
     attract title: LINK mode is dormant at boot, on screen OFF with no
     socket open (include/platform/native_arcade_link_host.h:227-233), and
     a cabinet enters its LOBBY only on its own player's START or CROSS
     (game/MAIN/MainArcadeLink.c:356-358, UX-11,
     docs/GAME_LOOP_UI_MILESTONE.md:547-551).
   - Reason: two players who press START on their cabinets within about
     3 s of each other link before either can go solo. The START that
     entered the LOBBY cannot start solo either, since release-to-arm
     already swallows it (netplay.c:745-751); the delay adds margin.
   - Accepted consequence: a second player who arrives more than about 3 s
     later may find the first cabinet already in solo. They can go solo
     too, and the pair links once both players are back in the LOBBY.
3. SOLO-3 (default): Input. CONFIRM (CROSS or START, edge-triggered as all
   flow input) begins solo while the offer stands. BACK still goes to EXIT.
   Priority keeps today's LOBBY order: BACK, then READY, then the solo
   CONFIRM. Race window: our peer can complete its handshake just as we
   leave. If its READY drops during MATCH_FOUND (45 ticks) it returns to
   its LOBBY (native_arcade_flow.c:188-194). Otherwise it enters SELECT,
   which fails on select peer silence (SEL-9, 90 ticks,
   docs/MATCH_SELECT_MILESTONE.md:611) or on a lost lobby status
   (native_arcade_flow.c:218-228). It then shows RESULTS LINK ERROR with
   the link closed, and its rows lead to REMATCH_WAIT (OPPONENT LEFT) or
   EXIT, then the title, not to its LOBBY.
4. SOLO-4 (default): Link during solo is listen-only. The handshake stops
   and the lobby link closes. The adapter keeps one socket bound on the
   link port.
   - Today a WAITING lobby holds no socket (native_lobby_state.c:144, :163-166),
     so this is an open without the first HELLO that peer-link Open sends.
   - On every tick the adapter drains incoming datagrams without replying.
     It latches `peerHeard` when a well-formed handshake HELLO
     (NativeLockstepHandshakeMessageV1_Decode,
     include/platform/native_lockstep_handshake.h:146) arrives from a
     configured peer.
   - `peerHeard` means exactly that: a configured peer sent a well-formed
     handshake HELLO, so that peer's player is in its LOBBY (a cabinet
     on its attract title sends nothing, SOLO-2).
   - Nothing is sent during solo, so a woken peer stays in its own LOBBY,
     where it can go solo too. A peer never interrupts solo select, race,
     or results.
   - This is the owner's "continuous background poll" for the static
     peer. It is fully met only with discovery (docs/HANDOFF.md:841-847):
     until then a peer is heard only once its player enters its LOBBY.
5. SOLO-5 (default): Select. Solo uses the same SELECT and SELECT_RESULT
   screens and rules for one human (humanCount 1, localHuman 0 on either
   seat) on a ONE_CAB base: the OD-1 per-item timer and automatic pick
   (docs/MATCH_SELECT_MILESTONE.md:560-562), the SEL-5 order (:600-603),
   the SEL-6 automatic pick (:604-606), SEL-7 BACK ignored (:607-608), and
   the SEL-8 result hold (:609-610). It drops the opponent footer and the
   peer-locked markers, SEL-9 peer silence, the relink, and the launch
   agreement. Characters are the 8 base characters only (RS-20,
   docs/ROSTER_MILESTONE.md:679-686). Tracks and laps are the linked lists
   (SEL-2, SEL-3: no Oxide Station). The bot rule is RS-20's LOAD_Robots1P,
   which replaces SEL-4 (the 2P AI set bots,
   docs/MATCH_SELECT_MILESTONE.md:598-599) in solo. The initial cursors of
   a fresh solo select are the base's, except the character cursor on the
   cab2 seat (SOLO-14); RACE AGAIN starts on the previous picks (SOLO-8).
6. SOLO-6 (default): Config. The solo race config is a ONE_CAB
   NativeMatchConfigV1, built in two steps.
   - The ONE_CAB base is built like the roster proof's
     (native_arcade_roster_proof.c:389-457): the fixture identity, tick
     rate, and bot difficulty; NativeMatchConfigV1_InitArcadeOneCab and
     Digest1PV1; bots ExpectedBots1P for the base human. The human is
     CAB1_HUMAN in slot 0 on either seat.
   - The race config is NativeMatchSelect_BuildConfig on that base and the
     one-human select's resolved outcome, as the linked relink does
     (native_arcade_netplay.c:443), so the SELECT_RESULT CPU list and the
     config agree: track, laps, and the human character from the outcome;
     bots from its provisional branch (risk 2). masterSeed comes from the
     select's existing derivation (DeriveSeed with humanCount 1: this
     cabinet's nonce alone).
   - NativeArcadeBotRules_ValidateConfigV1 is then a hard check that fails
     closed: a config that fails it is never handed to the race caller, so
     Arm refuses and no solo race launches (the RL-11 path in section 2
     shows solo RESULTS instead).
7. SOLO-7 (default): Race.
   - The race launches locally through MainArcadeRaceSetup, using the one
     existing Arm/Launch call (the RL-9 Disarm rule is unchanged).
   - The local pad drives slot 0 on every tick. There is no lockstep
     session, bundle, digest exchange, or hold.
   - Pacing is the same fixed VBlank pacing as a linked race, and so are
     the end rules: END_OF_RACE, finish grace, race tick limit, local
     failure.
   - The race then goes to RESULTS. A local failure (RL-11) reaches the
     flow only as observation.linkFailure = LINK_ERROR
     (platform/native_arcade_netplay.c:765-777), so solo RACING still acts
     on LINK_ERROR (to solo RESULTS) while it ignores the lobby status and
     the PEER_TIMEOUT and DESYNC link failures.
8. SOLO-8 (default): Results.
   - Solo RESULTS has rows RACE AGAIN and LOBBY.
   - RACE AGAIN goes to solo SELECT with the cursors on the previous picks,
     as OD-3 (docs/MATCH_SELECT_MILESTONE.md:576-582), and a new masterSeed.
   - LOBBY goes to LOBBY and the handshake restarts on the TWO_CAB fixture.
   - The idle timeout (resultsIdleTimeoutTicks = 900) goes to EXIT with
     CLOSE_LINK (which also closes the listen socket) and then the
     title/attract loop, exactly as the owner-accepted UX-10
     (docs/GAME_LOOP_UI_MILESTONE.md:543-546) and today's RESULTS timeout
     (native_arcade_flow.c:336-345, :380-392). It does not go to LOBBY.
   - While `peerHeard` is latched the screen adds "OTHER CABINET IS READY".
     The default cursor stays on RACE AGAIN.
   - Back in the LOBBY, a peer that is heard links as it does today.
9. SOLO-9 (default): Isolation. Solo state is host-local. Nothing about
   solo enters simulation identity, replay, or canonical state beyond the
   ONE_CAB profile of the config itself. Solo cannot be combined with
   replay record or playback, as the link cannot (main.c:373-376). The
   topology lease stays retire-only. Nothing new reads `NavHeader.last`
   (LR-17 unchanged).
10. SOLO-10 (default): Discovery seam. The offer depends only on the
    flow's lobby status (peer not heard), never on the peer list or its
    size. A later subnet announce replaces the lobby's candidate source and
    the listen-only socket's filter ("a configured peer") without touching
    the solo branch of the flow.
11. SOLO-11 (default): Dark until complete. The flow offers solo only when
    the adapter's observation says `soloAvailable` (the last reserved byte,
    native_arcade_flow.h:173, so the observation stays 12 bytes). It stays
    0 until the race launch slice and its live proof land, so a partial
    tree never strands a player on a solo screen that cannot race. The
    gate is on since S4 (1e399ba1e): NATIVE_ARCADE_LINK_HOST_SOLO_ENABLED_DEFAULT
    is 1u (platform/native_arcade_link_host.c:229), pinned literally by
    host isolation rule 3j
    (tests/native_arcade_link_host_isolation_test.cmake:825-828), with the
    live proof ctest `arcade_solo_race` (section 6). Only the unit tests'
    NativeArcadeLinkHost_InternalSetSoloEnabled changes it. If the gate is
    on but the solo base does not build, Configure logs "the solo base did
    not build; solo stays off" and the cabinet runs linked only.
12. SOLO-12 (default): TWO_CAB is unchanged: no existing assertion or
    live-gate outcome changes for TWO_CAB. Every linked transition and
    timing, and the linked LOBBY's text while solo is not offered, stay as
    they are. (S1, S2, and S3 do edit isolation tests and add preview
    captures, for the solo parts only.)
13. SOLO-13 (default): Prompt text. While offered, the LOBBY shows
    "WAITING FOR OTHER CABINET" and "PRESS START TO RACE SOLO". The prompt
    names START, but CROSS confirms too
    (include/platform/native_arcade_menu_input.h:55-56; risk 8).
    Before the offer the lobby text stays as it is today
    (game/MAIN/MainArcadeLinkLayout.c:338, :362).
    - Every string uses only the glyphs the layout test accepts: A-Z,
      0-9, space, `.`, `:`, `,`, `-`, `'`
      (tests/main_arcade_link_layout_test.c:1516-1518).
      No new glyph is needed; "PRESS START" is already the attract line
      (MainArcadeLinkLayout.c:321).
    - The strings must fit the 400-wide panel (MainArcadeLinkLayout.c:14-17),
      which the same test checks (:1525-1531).
    - Those glyph and panel checks live in CheckSelectLayout
      (main_arcade_link_layout_test.c:1447), which runs only on SELECT and
      SELECT_RESULT inputs (:1597, :1623, :1640) and expects a HIGHLIGHT
      item. LOBBY and RESULTS strings are not checked today, so S3 extends
      or generalises the check to cover the solo LOBBY and RESULTS
      strings.

SOLO-14 to SOLO-18 record what S4 chose where the plan left the choice
open. Their citations are to the code at 1e399ba1e.

14. SOLO-14 (default, S4 part 1): The cab2 seat's initial solo character
    cursor. A fresh solo select (no solo config built yet) on the cab2
    seat starts the character cursor on the TWO_CAB fixture's CAB2_HUMAN
    character, so each seat starts on its own linked-race character. On
    the cab1 seat it starts on the base's CAB1_HUMAN character, as
    planned. The track and laps cursors start on the base's values on
    either seat (platform/native_arcade_netplay.c:485-531, the seat rule
    at :509-516). The config's one human is still CAB1_HUMAN in slot 0 on
    either seat (SOLO-6), whatever character it picks. RACE AGAIN keeps
    the previous picks on both seats (OD-3, SOLO-8). Covered by
    TestSoloCab2RaceAgain (tests/native_arcade_netplay_test.c:6927-7008).
15. SOLO-15 (default, S4 parts 1 and 2): The local drive's pads. A solo
    race runs the race drive's local mode, NativeArcadeRaceDrive_BeginLocal
    (include/platform/native_arcade_race_drive.h:234-245,
    platform/native_arcade_race_drive.c:404-441). It has no session, kept
    ring, or callbacks, so it records, submits, composes, sends, resends,
    polls, and takes nothing, and calls no callback. Each Step keeps the
    linked argument checks and the linked end checks in the same order
    (END_OF_RACE, finish grace, race tick limit), then GOes on the race
    tick itself, with no input delay (:443-460, :547-551):
    - pad 0: the local sample, normalized;
    - pad 1: the neutral connected pad, as in the roster proof's ONE_CAB
      pads;
    - pads 2 and 3: disconnected.
    It never returns HOLD (Hold on a local drive is FAILURE_SEQUENCE), and
    a finish arms no linger (:124-126). Outside the race ticks the race
    caller's RL-10 neutral pads follow the armed config's profile through
    MainArcadeRaceLaunchCore_PadProfile
    (game/MAIN/MainArcadeRaceLaunchCore.c:474-481, called at
    game/MAIN/MainArcadeRaceLaunch.c:278), ONE_CAB for a solo race.
16. SOLO-16 (default, S4 parts 1 and 2): A missing or invalid solo config
    is a local failure. The race caller arms the agreed config or, with
    none, the solo config, through the one Arm and Launch
    (game/MAIN/MainArcadeRaceLaunch.c:371-435). With neither, Arm refuses
    (RL-11). The host's BeginDrive begins the local drive on the solo
    query's checked config (platform/native_arcade_link_host.c:347-388);
    with none it passes NULL. BeginLocal refuses NULL as FAILURE_ARGUMENT,
    and a config that is not ONE_CAB with CAB1_HUMAN in slot 0 as the new
    FAILURE_LOCAL_CONFIG = 20 (include/platform/native_arcade_race_drive.h:116).
    Every such refusal is reported as a local race failure, which ends the
    solo race on solo RESULTS as RACE ERROR (SOLO-7, S3).
17. SOLO-17 (default, S4 parts 1 and 2): A solo race still needs the
    per-tick V4 projection. The race caller projects the live V4 state on
    every race tick of a solo race as for a linked one
    (game/MAIN/MainArcadeRaceLaunch.c:912-918), and the local drive checks
    the state's frame number against the race tick
    (platform/native_arcade_race_drive.c:495-498). A projection failure is
    reported as a local drive failure (MainArcadeRaceLaunch.c:824-831), so
    it ends a solo race as RACE ERROR, although solo exchanges no digests.
18. SOLO-18 (default, S4 part 3): The autopilot's solo mode, for the live
    gate only (internal builds).
    - `--arcade-link-autopilot-solo` is a flag, given once; it needs
      `--arcade-link-autopilot` and is rejected with
      `--arcade-link-autopilot-freeze` or `-desync`.
    - The run: START on the attract screen, CROSS on the solo offer, CROSS
      through the solo select, one solo race, the LOBBY row on solo
      RESULTS, and PASS on the LOBBY after that RETURN_TO_LOBBY
      (include/platform/native_arcade_link_autopilot.h:110-136). The race
      start is recorded by RecordSoloStart (no agreed match), and the race
      must still be RL-12 validated. Its RESULTS end must be FINISHED (the
      natural finish, the finish grace, or the race tick cap).
    - The failure codes (:177-198): UNEXPECTED_RACE (43) for any part of
      the linked session (a START_RACE, MATCH_FOUND, REMATCH_WAIT, or a
      select, race, or results screen that is not solo) or a second
      RESULTS entry; SESSION_LOST (42) for a RETURN_TO_LOBBY that is not
      its own, or the title (the RESULTS idle timeout); RACE_FAILED (41)
      for any other end; EVIDENCE_MISSING (44) for an end without the
      RL-12 validation; TIMEOUT (40) and REPORT_WRITE_FAILED (45) as in
      the linked run.
    - The report adds "mode solo" after the cab line and has no agreed
      match line; the linked report is byte-identical to before.
    - The steering reads driver slot 0's kart in a solo race on either
      seat, since the human is CAB1_HUMAN in slot 0 (SOLO-6); in a linked
      race it reads the cabinet's own slot
      (game/MAIN/MainArcadeRaceLaunch.c:592-597).

## 4. Slices

S1, S2, and S3 land dark (SOLO-11): solo is reachable only in tests, and
S3's solo screens only through `--arcade-link-preview`, until S4 switches
`soloAvailable` on. Order: S1, S2, S3, S4, S5. All five have landed in
that order; section 6 records the commits.

### SOLO-S1 -- flow core (pure)

- Files: include/platform/native_arcade_flow.h,
  platform/native_arcade_flow.c, tests/native_arcade_flow_test.c,
  tests/native_arcade_flow_isolation_test.cmake, and
  tests/native_arcade_netplay_test.c. The last one only because
  SmallTimings (:237-248) sets the nine timing fields one by one on stack
  structs (for example TestPure's at :672), so the tenth timing must be set
  there too or Init fails. The flow test does the same
  (tests/native_arcade_flow_test.c:1459-1468).
- Changes:
  - a solo mode bit in the flow, and `soloOffered` behind an accessor;
  - SOLO_OFFER_DELAY as a tenth frozen default timing (90u) with its own
    counter;
  - `soloAvailable` in the observation's reserved byte;
  - new actions (for example BEGIN_SOLO_SELECT and START_SOLO_RACE);
  - the solo SELECT and SELECT_RESULT transitions, which read no lobby
    status or link failure;
  - solo RACING, which ignores the lobby status and the PEER_TIMEOUT and
    DESYNC link failures but still acts on LINK_ERROR: in solo that is the
    local race failure (RL-11, platform/native_arcade_netplay.c:765-777),
    and it goes to solo RESULTS;
  - the solo RESULTS rows (RACE AGAIN, LOBBY) and the idle timeout to EXIT
    with CLOSE_LINK, then RETURN_TO_TITLE (SOLO-8, UX-10).
- tests/native_arcade_flow_test.c cases: offer timing across restarts;
  READY beats CONFIRM, and BACK beats both; REJECTED never offers and
  restarts the count; with the gate off there is never an offer; solo
  SELECT and SELECT_RESULT ignore every lobby status and link failure; solo
  RACING ignores LOST, PEER_TIMEOUT, and DESYNC, and LINK_ERROR there goes
  to solo RESULTS; the RACE AGAIN and LOBBY rows; the idle timeout to EXIT
  and then RETURN_TO_TITLE, never to LOBBY.
- Also update the isolation test's frozen defaults (ten).
- Scope: fast suite.

### SOLO-S2 -- adapter and host (pure)

- Files: platform/native_arcade_netplay.c, platform/native_arcade_link_host.c,
  their headers, and the smallest lobby/peer-link addition needed.
- Changes:
  - the listen-only link (SOLO-4), as a listen mode in the lobby or the
    peer link (risk 1);
  - a one-human select session on the ONE_CAB base (localHuman 0, the
    initial cursor from the CAB1_HUMAN slot; since S4 part 1 the cab2
    seat's character cursor starts on the fixture's CAB2_HUMAN character,
    SOLO-14);
  - the SOLO-6 config;
  - solo launch without agreement;
  - the race caller's config query (GetAgreedConfig answers in solo, or a
    solo query beside it);
  - `soloOffered` and `peerHeard` in the views.
- Where the SOLO-6 config is built. The link host builds the ONE_CAB base,
  beside the TWO_CAB fixture it already builds in Configure
  (platform/native_arcade_link_host.c:718, NativeArcadeLinkFixture_Build),
  and hands it to the adapter in its config beside `fixture`. The adapter
  runs the one-human session and BuildConfig on the outcome, as its relink
  does (native_arcade_netplay.c:443; it already links the select rules
  through ctr_native_match_select_session). The host's solo config query
  runs ValidateConfigV1 and fails closed (SOLO-6).
  - Why the host: it already links ctr_native_arcade_link_options
    (CMakeLists.txt:528-529), which links ctr_native_arcade_bot_rules
    PUBLIC (:409), so its three-library pin
    (tests/native_arcade_link_host_isolation_test.cmake rule 4, :749-783)
    is unchanged. Only the host .c include allow-list (rule 3, :148-149)
    gains the bot rules header.
  - The other choice, building it in the adapter, would widen the netplay
    adapter's pin (tests/native_arcade_netplay_isolation_test.cmake rule 4,
    :108-146: exactly nine libraries) and its include allow-list (:74) to
    the bot rules and the link options, so it is not taken. The netplay
    link list and include allow-list stay as they are.
- View growth.
  - The netplay view packs the new flags as bits in its one reserved byte
    (include/platform/native_arcade_netplay.h:324), so it does not grow.
    tests/native_arcade_netplay_test.c:3790 checks that byte is 0; outside
    solo it still is, so that check keeps its outcome under the byte's new
    name.
  - The host view (include/platform/native_arcade_link_host.h:150) has no
    spare byte: its four uint8_t fields fill the word after
    ticksInScreen. It grows by appending one 4-byte group after `select`
    (for example `solo`, `soloOffered`, `peerHeard`, and a reserved byte).
    No test names its size or offsets today and it is never serialised;
    tests/main_arcade_link_view_layout_test.c gains the solo views.
- Isolation pins to update in
  tests/native_arcade_link_host_isolation_test.cmake, which pins the
  host's declarations literally (:187-188, :260-263, :301-302): a solo
  query and the view's new solo fields add pinned literals there, as the
  select view did (:187), and rule 3 adds the bot rules include.
- Unit tests.
  - The listen mode gets its own cases in tests/native_lobby_state_test.c
    (ports 48300-48399) or tests/native_lockstep_peer_link_test.c,
    whichever module gains it: it never sends; it filters decodes (a
    well-formed handshake from a configured peer latches, a malformed one
    or one from any other address does not); and it closes and reopens on
    the same local port.
  - tests/native_arcade_netplay_test.c, which uses real loopback UDP
    sockets in its own port band (:31-35), not a fake transport: a probe
    socket on the peer address receives nothing in solo; a HELLO sent in
    solo sets `peerHeard` and changes no screen; LOBBY after solo links
    with a peer.
  - tests/native_arcade_link_host_test.c (ports 48500-48519, :31-34): the
    solo config passes ValidateConfigV1 and its bots equal ExpectedBots1P
    for all 8 human characters; a config that fails the check is never
    returned by the solo query.
- Reviewer pass (link flow).
- Scope: fast suite plus `-L live-link` (`arcade_link_launch` must stay
  green).

### SOLO-S3 -- layout and sound

- Files: game/MAIN/MainArcadeLinkLayout.c, game/MAIN/MainArcadeLinkSound.c,
  platform/native_arcade_link_options.c (preview names, :19-38).
- Changes:
  - the SOLO-13 prompt;
  - the solo select without opponent parts;
  - the solo RESULTS rows and the SOLO-8 notice;
  - the solo RESULTS title for a LINK_ERROR end, which in solo is only the
    local race failure (SOLO-7): "RACE ERROR", not the linked "LINK ERROR"
    (MainArcadeLinkLayout.c:398), so no "link" wording reaches a solo
    player. Its glyphs are in the accepted set (SOLO-13);
  - preview screens (for example `lobby-solo` and `results-solo`) for
    `--arcade-link-preview`, with their captures in the preview check.
- Tests: layout unit tests. The glyph and panel checks today run only on
  SELECT and SELECT_RESULT (CheckSelectLayout,
  tests/main_arcade_link_layout_test.c:1447, which expects a HIGHLIGHT
  item), so S3 extends or generalises them to the solo LOBBY and RESULTS
  layouts, including "RACE ERROR" and "OTHER CABINET IS READY" (SOLO-13).
- Game code still names no match-select or lockstep module
  (docs/HANDOFF.md:795-797).
- Scope: fast suite plus `-L live-render`.

### SOLO-S4 -- solo race launch and live proof

- Files: game/MAIN/MainArcadeRaceLaunch.c, the link host, the race drive,
  the autopilot, main.c, CMakeLists.txt, and a new checker under tools/.
- Changes:
  - the solo branch (SOLO-7) through the one existing Arm/Launch call, so
    the race setup caller allow-list is unchanged;
  - InstallPads by the config's profile (:264);
  - the host begins and ends the solo race with the linked pacing, and the
    drive runs local only;
  - `soloAvailable` switched on.
- Autopilot solo mode: press CONFIRM at the offer, pick, steer, choose
  LOBBY at RESULTS, and PASS on reaching LOBBY again.
- Unit tests, where the code is pure, and isolation pins (AGENTS.md: every
  new seam):
  - the local-only drive: cases in tests/native_arcade_race_drive_test.c
    (it never composes or sends a bundle); and
    tests/native_arcade_race_drive_isolation_test.cmake updated for it,
    both the allowed linkers (drive_allowed_linkers, :117) and the send
    pins (rule 1c, :253-283);
  - the host's solo RaceBegin and RaceEnd: cases in
    tests/native_arcade_link_host_test.c (pacing on and off, refused
    outside solo RACING); and the host isolation pins for the pacing
    switch (rule 3f, :295-322) and the drive glue (rule 3g, :323-563)
    updated for the solo path;
  - profile-driven InstallPads: the choice of scripted-pad profile from
    the config's profile as a pure helper (for example in
    game/MAIN/MainArcadeRaceLaunchCore.c), with cases in
    tests/main_arcade_race_launch_core_test.c for both profiles;
  - the autopilot's solo mode in tests/native_arcade_link_autopilot_test.c.
- New live test `arcade_solo_race` (as landed: two independent
  one-cabinet processes at once, not one process; section 6):
  - labels `live;live-link`;
  - a race tick cap (`--arcade-link-autopilot-race-ticks`, 900);
  - each process's own local port and the peer it points at are unused
    loopback ports outside 7101/7102, 7001/7002, and 48000-48600: cab1 on
    7201 pointing at 7202, cab2 on 7203 pointing at 7204, with nothing
    listening on 7202 or 7204 (CMakeLists.txt:2151-2176);
  - its own checker, tools/arcade-solo-race-check.ps1.
- Add the test to the orchestrator's live test map
  (`.claude/agents/milestone.md`, which is local and excluded from the
  repo).
- Reviewer pass (race setup seam).
- Scope: fast suite plus `-L live-link` and `-L live-roster`.

### SOLO-S5 -- docs and operator notes

- docs/GAME_LOOP_UI_MILESTONE.md (flow), docs/MATCH_SELECT_MILESTONE.md
  (one-human select), docs/ROSTER_MILESTONE.md risk 14 and RS-1,
  docs/RACE_LAUNCH_MILESTONE.md risk 9.
- tools/package/README.txt and docs/PACKAGING.md operator notes.
- docs/HANDOFF.md state (not its Next work), and a link to this document
  outside "## Next work", in the Key files list of related docs
  (docs/HANDOFF.md:821-826, as SOLO-S5 left it).
- This document's status.

## 5. Risks and open items

1. Listen-only needs a socket without a handshake. Today the socket exists
   only inside a peer link, whose Open sends a HELLO
   (native_lockstep_peer_link.h:158-174), and a WAITING lobby holds none.
   S2 picks the smallest change: a lobby or peer-link listen mode that
   decodes without replying. A bare transport the adapter owns is not
   allowed: the adapter reaches the transport only through the lobby layer
   and may never link ctr_native_udp_transport
   (tests/native_arcade_netplay_isolation_test.cmake rule 4, :108-146).
   So the change edits the lobby or the peer link. The match-select
   milestone left native_lobby_state unedited
   (docs/MATCH_SELECT_MILESTONE.md:71-74) and relaxed GAME_LOOP_UI
   constraint 2 only for the peer-link aux channel (:63-65). This document records the same kind of relaxation for the
   listen mode. Resolved in S2: the listen mode is in the peer link,
   wrapped by the lobby (section 6).
2. Provisional bot branch versus LOAD_Robots1P. For one human on a ONE_CAB
   base (7 bots) the provisional branch
   (native_match_select_rules.c:344-361) gives {0..7} without the human,
   ascending. That is exactly LOAD_Robots1P (native_arcade_bot_rules.h:28-44),
   because k_matchSelectCharacters is the identity order (:16) and `taken`
   marks only the human (:281-282). The branch is still labelled
   provisional, and BuildConfig does not check the bots outside the 2P
   shape. SOLO-6 therefore requires ValidateConfigV1 on every solo config,
   and S2's all-8-characters test pins the equality.
3. In the SOLO-3 race window the peer reaches RESULTS LINK ERROR up to
   about 135 ticks (45 + 90) after we leave. No live test covers it.
4. Prompt text versus the font: the layout glyph and panel checks
   (SOLO-13) catch a bad string in the unit test, not on the cabinet, and
   only once S3 extends them from SELECT and SELECT_RESULT to the solo
   LOBBY and RESULTS layouts (they did not check LOBBY or RESULTS when
   this plan was written). S3 extended them, solo strings included.
5. `peerHeard` checks only that the HELLO is well formed and comes from a
   configured peer. A peer on a different build shows "OTHER CABINET IS
   READY" and then REJECTS when the player returns to the LOBBY: an
   operator fault, as today.
6. An abandoned solo cabinet returns to the title/attract loop through the
   RESULTS idle timeout, as the owner-accepted UX-10 does for a linked
   cabinet (SOLO-8). An abandoned LOBBY still waits forever, as today
   (UX-5); a LOBBY idle timeout is out of scope.
7. A local race failure in solo reaches the flow as LINK_ERROR, the only
   failure end reason (RL-11); the flow's end reasons do not change. S3
   shows it to a solo player as "RACE ERROR" (not "LINK ERROR").
8. CROSS is also the G29 throttle, so pressing the throttle at the offer
   starts solo. Release-to-arm makes a throttle held across the screen
   change harmless.
9. "Peer wakes during solo, then links from the LOBBY": covered live by
   `arcade_solo_wake_link` (labels `live;live-link`,
   tools/arcade-solo-wake-link-check.ps1). `arcade_solo_race` runs two
   independent solo cabinets whose peers stay silent, so it never covers
   the wake. The wake gate runs two processes over loopback:
   - cab1 (port 7401, peer 7402) starts alone with the autopilot's
     solo-then-link mode (`--arcade-link-autopilot-solo-then-link`,
     include/platform/native_arcade_link_autopilot.h): race 1 is the solo
     mode's solo race (SOLO-18's rules); after its own RETURN_TO_LOBBY the
     LOBBY presses nothing, a second solo race is UNEXPECTED_RACE, exactly
     one linked START_RACE is race 2, which must end FINISHED
     (RunEndAccepted: EndAccepted's race 1 end, not the LR-16 race 2
     table), and the run passes on the title after its own EXIT. The
     report adds "mode solo-then-link" and "peer heard <SCREEN>", the
     screen of the first solo view with `peerHeard` set; FaultAt stays
     NONE.
   - The check starts cab2 (port 7402, peer 7401, the one-race mode) only
     once cab1's stdout shows its solo race tick 0. cab2 boots to its
     LOBBY and sends its HELLO; cab1's listen-only link hears it during
     the solo race ("peer heard RACING", and a stdout line among the solo
     race's per-tick lines). After the solo RESULTS -> LOBBY the two link
     and race one linked race; cab1's race 2 and cab2's race 1 must have
     the same agreed match, config, plan, bots, and bank digests, drive
     end, and per-tick V4 digests.
   - Both use a 1200-race-tick cap. At 900 the measured margin was only
     1.67x (Debug: cab2 started at cab1's solo race tick 9 or later and
     was heard at race tick 542, so its start to LOBBY took at most 17.8
     s against 29.7 s of solo race left); at 1200 cab2 was heard at race
     tick 555 (at most 18.2 s against 39.7 s: 2.18x). The test took
     about 122 s.
   Unit coverage, over real loopback sockets:
   - tests/native_arcade_netplay_test.c TestSoloListenOnly (:6765-6925):
     nothing is sent from solo SELECT on; a stranger's HELLO is not heard;
     the configured peer's HELLO latches peerHeard and moves no screen.
   - tests/native_arcade_netplay_test.c TestSoloThenLobbyLinks
     (:7010-7085): the other cabinet wakes during solo SELECT and stays in
     its own LOBBY; after the solo race, LOBBY on solo RESULTS links the
     two through the linked select to START_RACE.
   - tests/native_arcade_link_host_test.c TestSoloRace (:5187-5202, its
     body RunSoloRace :4962-5185): through the host race API, a real
     handshake HELLO from the configured peer at solo race tick 12
     (SOLO_RACE_HELLO_TICK, :4899-4901) latches peerHeard, which solo
     RESULTS still shows; RESULTS -> LOBBY then links with the real peer
     and races a linked race on the linked drive.

## 6. Status

All five slices have landed on `arcade`:

- Plan: 6e2c59abe (defaults SOLO-1 to SOLO-13, slices, risks) and
  fca963d56 (plan review findings).
- SOLO-S1, flow core: 478082055.
- SOLO-S2, adapter and host: c45464aba, with the review follow-up
  cf54d94f2 (listen-only sends nothing, a linked session with solo
  enabled, only a HELLO latches peerHeard). The listen-only link is a
  peer-link mode (NativeLockstepPeerLink_OpenListen and _PollListen)
  wrapped by the lobby (NativeLobbyState_BeginListen and _PeerHeard)
  (risk 1).
- SOLO-S3, layout and sound: cb008152d. Previews `lobby-solo`,
  `select-solo`, `results-solo`, and `results-solo-error`; the layout
  glyph and panel checks run on the LOBBY and RESULTS layouts too (risk
  4).
- SOLO-S4, solo race launch and live proof, in three parts:
  - dfc5f9137: the race drive's local mode
    (NativeArcadeRaceDrive_BeginLocal, SOLO-15, SOLO-16); the host begins
    it in RaceBegin on solo RACING, with the linked pacing, and latches no
    divergence in solo; the cab2 seat's cursor (SOLO-14).
  - cafa3acf5: the arcade-link hook hands START_SOLO_RACE to the race
    caller in the same branch as START_RACE
    (game/MAIN/MainArcadeLink.c:382-399; the agreed-match log stays
    START_RACE-only); RETURN_TO_LOBBY is a documented no-op branch there
    (:409-418); the race caller arms the agreed config or else the solo
    config through the one Arm and Launch (SOLO-16), so the race setup
    allow-list is unchanged; profile-driven InstallPads (SOLO-15).
  - 1e399ba1e: the gate on (SOLO-11); the autopilot's solo mode
    (SOLO-18); the live test `arcade_solo_race` (labels `live;live-link`,
    tools/arcade-solo-race-check.ps1): two independent one-cabinet
    processes, cab1 on 7201 pointing at 7202 and cab2 on 7203 pointing at
    7204, both peers silent, a 900-race-tick cap. Each process ends its
    race FINISHED by the cap and returns to the LOBBY. It passed in about
    87 s.
- SOLO-S5, docs and operator notes: this document's status, the related
  milestone documents, tools/package/README.txt, docs/PACKAGING.md, and
  docs/HANDOFF.md.

- Risk 9, a peer that wakes during solo and then links from the LOBBY:
  covered live by `arcade_solo_wake_link` (labels `live;live-link`,
  tools/arcade-solo-wake-link-check.ps1) with the autopilot's
  solo-then-link mode (section 5, risk 9). cab1 on 7401 races solo, cab2
  on 7402 is started mid-race and heard on solo RACING, and after cab1's
  solo RESULTS -> LOBBY the two race one linked race, paired tick by tick,
  under a 1200-race-tick cap. It passed in about 122 s.

Open: risk 3 (no live test of the race window), risk 6 (an abandoned
LOBBY still waits forever), and physical two-cabinet validation (HANDOFF
steps 6 and 7).

## 7. Boot-intro skip (arcade link)

Owner request: a cabinet configured for the arcade link boots straight to
the title. In host mode LINK (`--arcade-link` with static seats or
`seat = auto`, solo included) the cabinet skips the SCEA logo and its
"Start your engines" XA, the copyright page, and the Naughty Dog crate
cutscene. Every other launch keeps the retail intro unchanged: no link
option, PREVIEW (`--arcade-link-preview`), `--arcade-roster-proof`, and the
replay and determinism fixtures.

The decision is the pure `MainArcadeLinkPolicy_SkipBootIntro(hostMode)`
(game/MAIN/MainArcadeLinkPolicy.c, 1 for LINK only; unit-tested in
tests/main_arcade_link_policy_test.c). The game reads it only through the
thin accessor `MainArcadeLink_SkipBootIntro()` (game/MAIN/MainArcadeLink.c).
main.c configures the host, which fixes the mode for the run, before
`CTR_Main` and so before any StateZero. The accessor has three call sites,
each inside `#if defined(CTR_NATIVE)`, with the retail code in place for
every other mode:

1. StateZero (game/MAIN/MainMain.c): the SCEA TIM still loads but is not
   displayed, the ND-crate song is loaded but not started (only
   `CseqMusic_Start(CSEQ_SONG_LEVEL, ...)` is gated; see the crate-song
   bullet under "Checked"), and the XA is neither played nor waited on.
   Every load, the howl and music init (`Music_SetIntro`,
   `CseqMusic_StopAll`, `Music_Start(0)`), the memcard init, and the four
   topology-lease calls run as before, in the same order. Skipping the
   play leaves no XA state
   that matters later. CDSYS_Init (from LOAD_InitCD) has already left
   `XA_State` idle and `XA_PauseFrame` 0, the values the retail play leaves
   when it ends here (`frameTimer_MainFrame_ResetDB` is still 0 in
   StateZero). The next `CDSYS_XAPlay` rewrites the index, category,
   volume, and sample fields before anything reads them.
2. LOAD_TenStages stage 0, first boot (game/LOAD/LOAD_TenStages.c): the
   copyright TIM still loads, `boolFirstBoot` is still cleared, and the
   bookmark is still pushed. Only the display and the native intro-CSEQ
   hold are skipped.
3. The crate (game/233/CS_Thread.c): on the crate's first camera tick the
   cutscene takes the retail START-skip path (flag, `CseqMusic_StopAll`,
   `CDSYS_XAPauseRequest`, `MainRaceTrack_RequestLoad(MAIN_MENU_LEVEL)`,
   `isCutsceneOver`). Only the minimum-time gate
   (`CS_ND_CRATE_SKIP_MIN_FRAME32`, about 5.8 s) is bypassed, for
   `NAUGHTY_DOG_CRATE` only. No pad tap is injected. Credits and endings
   are untouched. The skip fires once. The skip path returns 1 from
   `CS_Thread_UseOpcode`, so `CS_Thread_ThTick` sets `THREAD_FLAG_DEAD` on
   the camera thread in that same tick (game/233/CS_Thread.c:1563-1565),
   before any LOADING frame. As a second layer, on the next frame the
   crate's load request sets LOADING (the `NAUGHTY_DOG_CRATE` case,
   game/MAIN/MainMain.c:226-229), and a LOADING frame skips
   `MainFrame_GameLogic` and so every thread tick (MainMain.c:498-507).
   The crate camera script (`R233.creditsOpcodeData` from offset 0x18)
   opens with a sync marker, and its first real opcode is PLAY_XA
   (category 1, XA 0x51; game/233/R233.c:2333). In LINK the skip runs
   before opcode dispatch on the first tick, so that XA never starts,
   where a retail START skip fades it out (`CDSYS_XAPauseRequest` is a
   no-op on an idle XA). The end state is equivalent: `XA_State` stays
   idle; `XA_Playing_Category` is read only when the XA is not idle
   (game/HOWL/HOWL_AudioState.c:209) and `XA_Playing_Index` is never read;
   `XA_PauseFrame` stays 0, so its readers (HOWL_AudioState.c:297, 308,
   406, 417) just pass their 9-frame check sooner.

Why not load the main menu at boot? That was rejected as unsafe. The
first-boot branch of stage 0 skips `MEMPACK_SwapPacks` and
`MEMPACK_PopToState`, so a main menu loaded at first boot would sit in a
pack and memory layout retail never uses for it. The skip therefore keeps
`levelID = NAUGHTY_DOG_CRATE` and the whole loader flow. The main menu is
then loaded the retail way, the same as after a START skip.

Checked:

- Memcard and options: `MEMCARD_InitCard` still runs in StateZero.
  `RefreshCard_Entry` runs only on MAIN_MENU, ADVENTURE_ARENA, or
  END_OF_RACE frames (game/MAIN/MainFrame_RenderFrame.c:254-259), never
  in the crate. The boot memcard load and `RaceConfig_LoadGameOptions`
  (game/RefreshCard.c:279, game/RaceConfig.c:5-10) therefore run on the
  title as before.
- The arcade-link hook: the title is reached through the retail main-menu
  load, so its intro and `MainArcadeLinkPolicy_TitleMenuReady`'s
  menu-ready frame are unchanged.
- RNG and boot counters: the crate's later per-tick `MixRNG_Scramble`
  calls stop. Some still run in LINK: the init-time draw in
  `CS_Thread_Init` (game/233/CS_Thread.c:1815) for the camera thread and
  every box and kart thread (game/233/CS_Cutscene.c:10, 39-57), the one in
  `CS_Thread_LInB` (CS_Thread.c:1514) for any crate instance born through
  it, the camera's random clear-box draw on the first tick
  (CS_Thread.c:314), which comes before the skip check (CS_Thread.c:337),
  and whatever the box and kart threads draw on their own first tick.
  These only move `sdata->randomNumber`. The skipped VSync-pumped waits
  (the SCEA XA wait in StateZero and the copyright hold in stage 0) also
  no longer advance the `MainDrawCb_Vsync` counters `frameTimer_VsyncCallback`,
  `frameTimer_Confetti`, and `rcntTotalUnits` (game/MAIN/MainDrawCb.c:22-32).
  They never moved the audio RNG: its only writers are `Garage_PlayFX`,
  `Level_RandomFX`, and `Voiceline_RequestPlay`, none reached at boot. The
  race setup pins `randomNumber` (from the agreed seed), both `advRng`
  states, the PSX `rand` seed, `audioRNG`, `timer`, `frameTimerConfetti`,
  `rcntTotalUnits`, and `clockFrameStart` before every linked or solo
  race (game/MAIN/MainArcadeRaceSetupCore.c:346-360, applied in
  game/MAIN/MainArcadeRaceSetup.c:163-189). `frameTimer_VsyncCallback` is
  not pinned; the race digest reads it only as a delta from race start
  (game/MAIN/MainArcadeRaceDigest.c:97, 114). This is the same
  menu-history independence the roster proof relies on. The live link
  tests are the proof.
- The crate song: retail StateZero runs `Music_SetIntro`,
  `CseqMusic_StopAll`, `CseqMusic_Start(CSEQ_SONG_LEVEL, 0, NULL, 0, 0)`,
  and `Music_Start(0)`. The crate load keeps music playing
  (`boolPlayMusicDuringLoading`, game/LOAD/LOAD_TenStages.c:51), and in
  LINK the first crate tick stops every song (`CseqMusic_StopAll`,
  game/233/CS_Thread.c:377), so the song could only play over a black
  screen. In LINK, StateZero therefore skips only the `CseqMusic_Start`;
  the crate load is now silent and the title music starts as before.
  What each call does:
  - `Music_SetIntro` (game/HOWL/HOWL_Music.c:3-20) zeroes
    `audioDefaults[7]`, loads bank 33, and loads `HOWL_SONG_ND_CRATE`.
    It still runs, so the loaded audio state is unchanged.
  - `CseqMusic_Start` (game/HOWL/HOWL_CseqMusic.c:3-41) takes the first
    free `songPool` slot and calls `SongPool_Start`
    (game/HOWL/HOWL_SongPool.c:42-181), which writes that slot's flags,
    id, tempo, volume, and sequence list, and claims one `songSeq` entry
    per sequence. The per-frame `Channel_ParseSongToChannels`
    (game/HOWL/HOWL_Channel.c:311-329) then plays notes, moving SPU
    channels between `channelFree` and `channelTaken`. It touches no RNG
    (the `audioRNG` writers are only `Garage_PlayFX`, `Level_RandomFX`,
    and `Voiceline_RequestPlay`) and no game state.
  - `Music_Start(0)` (HOWL_Music.c:534-540) is bookkeeping only:
    `cseqBoolPlay = true`, `cseqHighestIndex = 0`. It starts nothing
    audible and still runs, so the flags read later are as retail.
  Nothing later depends on the song having been started. Every
  song-pool and sequence reader checks the slot's playing bit first
  (`CseqMusic_*`, `SongPool_StopAllCseq`, `Channel_ParseSongToChannels`),
  and the only reader of `timeSpentPlaying`, the stage-0 copyright hold
  (LOAD_TenStages.c:88), is already skipped in LINK. The crate's
  `CseqMusic_StopAll` finds no live slot, where retail frees slot 0; both
  leave every slot free. The main-menu load then runs as after a retail
  skip: stage 4 `Music_Restart` (LOAD_TenStages.c:305) is a no-op on the
  free slot in both cases, stage 5 `Music_Stop` (LOAD_TenStages.c:359)
  reads the same `cseqBoolPlay`/`cseqHighestIndex` and resets them, and
  stage 8 sets `AUDIO_GARAGE_ENTRY` (LOAD_TenStages.c:633-638; the crate
  latched state 4, which `Audio_SetState` ignores), whose
  `Music_Adjust(0, ...)` (game/HOWL/HOWL_AudioState.c:26-32,
  HOWL_Music.c:435-468) starts the title song in slot 0, the same free
  slot, rewriting every field `SongPool_Start` sets. What differs is
  audio-only: the stale fields of `songPool[0]` and the crate's
  `songSeq` entries (rewritten before any read), and the order of
  `channelFree`, which only picks the SPU voice for later sounds.
  `OtherFX_Play` returns a `CountSounds` handle, not a channel
  (game/HOWL/HOWL_OtherFX.c:118-119). None of this is in the race setup
  pins, the race digest, or the replay state; the race setup pins
  `audioRNG` regardless (game/MAIN/MainArcadeRaceSetupCore.c:346-360).

tests/main_arcade_link_boot_intro_isolation_test.cmake pins the structure:
the three guarded call sites, StateZero's load and lease order, the exact
gated blocks, the retail START-skip path, and that no pad word is written.

## 8. Top LOD tier (arcade link)

Owner request: in the linked split screen, the other karts show as "a
stacked set of sprites" before they pop in as 3D models. They should always
be 3D, and more generally every LOD should pick its top tier. The fix must be
presentation-only, and the model data must not be edited. The model headers
are live data: `CS_OPCODE_SET_VISIBLE_LOD` writes `maxDistanceLOD`
(game/233/CS_Thread.c:647, 651).

The gate is the pure `MainArcadeLinkPolicy_ForceTopLod(hostMode)`
(game/MAIN/MainArcadeLinkPolicy.c, 1 for LINK only, the same rule as
section 7), read only through `MainArcadeLink_ForceTopLod()`
(game/MAIN/MainArcadeLink.c). No link option, PREVIEW,
`--arcade-roster-proof`, and the replay fixtures keep the retail LOD.

### 8.1 What drew the sprite karts

The sprites are the retail multiplayer kart impostor (DecalMP). No native
rendering bug was found in it. In 2P-4P, `DecalMP_01` (game/DecalMP.c) sets
`PUSHBUFFER_EXISTS | PIXEL_LOD` on every kart. It also points each kart's
per-camera `pushBuffer` at a DecalMP entry: a copy of that camera's
PushBuffer. `RenderBucket_UpdatePushBufferMetadata`
(game/RenderBucket/RenderBucket_QueueExecute.c:1720-1746) keeps
`PUSHBUFFER_EXISTS` only while the kart's projected box fits in 96x64 and is
fully on screen. The kart is then drawn into its own OT range, onto a 96x64
VRAM rectangle (`PushBuffer_SetDrawEnv_DecalMP`, `isbg` 0, so the rectangle
is never cleared). `DecalMP_03` shows that rectangle as a textured quad, and
`DecalMP_02` redraws it only on some frames. A kart larger than 96x64 on
screen is drawn in 3D: that is the pop. The native renderer reloads the
offscreen target from VRAM and packs it back
(`NativeRenderer_SetOffscreenState`, platform/native_renderer.c). That keeps
the PS1 no-clear behaviour, so earlier silhouettes can stay inside the
rectangle. This was not compared against hardware. The retail impostor is
left unchanged outside LINK.

### 8.2 What LINK changes (two render call sites)

1. `RenderBucket_QueueDraw`: the retail LOD walk
   (`RenderBucket_SelectModelHeader`, :1216-1272) runs unchanged. That
   includes its cull: a draw past every header's range sets `lodExhausted`
   and draws nothing. Only when the walk found a header,
   `RenderBucket_ForceTopLodHeader` (:1991) replaces it with the top tier.
   The top tier is the first header with a nonzero `maxDistanceLOD`
   (normally `headers[0]`), which keeps a cutscene's
   `CS_OPCODE_SET_VISIBLE_LOD` choice. `idpp->mh` and `idpp->lodIndex` store
   the top tier. The renderer-driven animation advance
   (`RenderBucket_AdvanceInstanceAnimWord`, which writes `Instance.animFrame`)
   keeps the retail tier's frame count (:2149-2161).
2. `DecalMP_01`: each kart keeps `PIXEL_LOD` (the retail multiplayer
   distance scale-up in `RenderBucket_BuildM3x3`, :1361) but gets no
   `PUSHBUFFER_EXISTS`. Its camera `pushBuffer` is not redirected, so it is
   drawn in 3D in every view on every frame. Every `DecalMP_01` entry write
   still runs; the entry writes of the impostor render do not (8.4).
   Keeping `PIXEL_LOD` means a distant 3D kart is drawn scaled up by
   `(viewDepth/2 + 0x1000)/0x1000`, as in retail; without the impostor this
   is now more visible. The owner has not looked at it yet.

### 8.3 Every LOD threshold found

| Threshold | Where | Gates besides mesh detail | LINK |
|---|---|---|---|
| Model header walk, `(u16)maxDistanceLOD` against the projected depth | RenderBucket_QueueExecute.c:1216-1272 | Draw-distance cull when every header is exhausted; the header's frame count bounds the `animFrame` advance (:1841) | Top tier drawn; cull and anim frame count kept |
| DecalMP impostor, 96x64 fit | RenderBucket_QueueExecute.c:1740; game/DecalMP.c | DecalMP entry memory, which the retail missile check reads (8.4) | Impostor off; entry writes kept |
| Tire LOD, `idpp->lodIndex <= lodThreshold` (2 in 1P/2P, 0 in 3P/4P) | game/DrawTires.c:51, 585, 662, 1146 | Nothing (tire prims only) | Follows the top tier (tires on every drawn kart) |
| `sdata->LOD[]` = {1,2,4,4,8,8,8,8} as `lodMask` | game/zGlobal_SDATA.c:246; MainFrame_RenderFrame.c:756-759; QueueExecute.c:2075 | Not a mesh LOD: instance flag bits 0-3 choose which instances exist per player count (a cull), and it gates the anim advance | Unchanged |
| Level BSP near/far slot (1P `bspLodDistanceThreshold`, 2P 0x1540; none in 3P/4P) | game/RenderLevel/RenderLists.c:141, 246-251; MainFrame_RenderFrame.c:974, 990 | Subdivision tier of BSP leaves; the primMem budget is tuned to it | Near slot for every leaf (`MainArcadeLink_LevelLodThreshold`, RenderLists.c:249) |
| Level texture and top-level subdivision depths (`textureLodDepthThreshold0/1`, `topLevelNear`) | MainFrame_RenderFrame.c:975-977, 991-993; game/227/227_00_DrawLevelOvr2P.c:12-14; the 228/229 overlays through 226_00_DrawLevelOvr1P.c:8073-8079 | Same primMem budget | Nearest texture tier and top-level subdivision for every quad (`MainArcadeLink_ForceNearLevelDepths`: MainFrame_RenderFrame.c:999, 227_00_DrawLevelOvr2P.c:118, 226_00_DrawLevelOvr1P.c:8078) |
| Level recursive subdivision depth (`recursiveNear`) | MainFrame_RenderFrame.c:978, 994; 227_00_DrawLevelOvr2P.c:15; 226_00_DrawLevelOvr1P.c:8076 | Same primMem budget; the native renderer vertex buffer | Unchanged (forced too, it overflowed 1 MiB and the renderer vertex buffer, 8.5) |
| Per-player-count LEV file (`levelLOD`) | game/LOAD/LOAD_TenStages.c:190-199 | Geometry, collision, nav: simulation | Unchanged |
| Driver model pack HI/MED/LOW | game/LOAD/LOAD_Assets.c:80-190 | Pack layout and memory; drivers' animation frame counts (headers[0], VehFrame.c:59) | Unchanged |
| Quadblock collision LOD (`COLL_SEARCH_HIGH_LOD`) | game/COLL.c:1011, 2042 | Collision: simulation | Unchanged |
| Exhaust particle set per player count | game/Vehicle/VehEmitter.c:156-171 | Particle spawn and RNG in the driver tick: simulation | Unchanged |

The level geometry tiers were first left alone: instances are drawn before
the level, and the per-level primMem budget (MainInit.c:85-159, allocated
from MEMPACK) is sized for the retail tiers, so forcing every BSP leaf to
the subdivided tier would drop level geometry at the guard. Section 8.5
measures the cost and gives LINK larger primMem buffers without moving the
MEMPACK layout. With those buffers (now 1 MiB each), LINK also forces the
level to its near tier (8.5, "Level near tier"): the pure
`MainArcadeLinkPolicy_LevelLodThreshold` (LINK -> 0x40000000, which no
projected distance or depth reaches; any other mode -> the retail
threshold) replaces the BSP near/far slot distance and, through
`MainArcadeLink_ForceNearLevelDepths` right after each retail seed, the two
texture thresholds and `topLevelNear` in the render scratch. Every BSP leaf
takes its near slot, and every quad its nearest texture tier and its
top-level subdivision. `recursiveNear`, the deeper subdivision near the
camera, stays retail. The thresholds live in the render scratch and gate
only primitives, so nothing digested reads them (8.4).

### 8.4 Nothing digested changes

- Static: `idpp->mh`, `idpp->lodIndex`, `PIXEL_LOD`, `PUSHBUFFER_EXISTS`,
  and the idpp `pushBuffer` are not read by the V4 projection or the race
  digest. `MainCanonicalDrivers` reads only `inst->thread` from an Instance;
  `MainArcadeRaceDigest` reads GameTracker counters and RNG. The checkpoint
  only relocates idpp pointers in its memory image
  (platform/native_checkpoint.c:727-738), and the next render rewrites them.
  Nothing new goes into checkpoints, replay, or canonical state.
- A retail coupling had to be kept: `VehPickupItem_MissileGetTargetDriver`
  (game/Vehicle/VehPickupItem.c:480, 490) checks a bot's target against
  `gGT->pushBuffer[driverID].rect` for any driverID. The array has four
  cameras (`sizeof(struct PushBuffer)` is 0x110), and `gGT->DecalMP`
  (entries of 0x128) follows it (include/namespace_Main.h:336, 369), so
  bots 4-7 read DecalMP entry bytes. In a 2P race (6 drivers,
  game/MAIN/MainInit.c:357-360) those are entry 0's never-written `pb.rot`
  (bot 4) and entry 1's `inst` pointer at +0x8 (bot 5: `rect.w` is its low
  half, `rect.h` its high half). A first version that skipped the
  `DecalMP_01` entry writes diverged: bot 5 took a different missile target
  and the V4 digest split at race tick 1850. The LINK path therefore keeps
  every `DecalMP_01` entry write. The isolation test pins exactly that:
  those writes run, unguarded, in every mode. It does not pin the driver
  count or the struct offsets this bullet relies on. Since fixed (last
  bullet): the native check rejects every candidate for driverID >= 4
  before the `rect` reads, so it no longer reads DecalMP; the entry writes
  are still kept.
- LINK is not retail in the entry writes of the impostor render, which run
  only for a kart with `PUSHBUFFER_EXISTS`. `DecalMP_02` holds the entry
  timer at 1000 (its else branch) instead of counting it, and LINK never
  writes: `boolUpdatedThisFrame` (`DecalMP_02`); `renderW`, `renderH`,
  `lodIndex` and the timer reset to 0 (`DecalMP_03`); `pb.ptrOT` (left at
  the camera OT that `DecalMP_01` copies), `pb.renderBucketOTRangeEnd` and
  `pb.renderBucketOTByteOffset` (`RenderBucket_AllocateOTRange`,
  RenderBucket_QueueExecute.c:1693-1700); `pb.renderBucketScreenPos` and
  `pb.renderBucketScreenSize` (`RenderBucket_UpdatePushBufferMetadata`,
  :1737-1738). None of them is a `rect.w` or `rect.h` the missile check
  reads in 2P.
- A second retail out-of-bounds reader does see them. The weapon branch of
  `RB_CrateFruit_ThCollide` (game/231/RB_Crate.c:338-355) has no
  `ACTION_BOT` check, unlike the weapon and time crates (:160, :458). For
  the weapon's owner it calls `RB_Pickup_SetCamera`
  (game/231/RB_Pickup.h:20), which loads
  `gGT->pushBuffer[driverID].matrix_ViewProj` into the GTE rotation and
  translation, projects the kart, and adds `rect.x` and `rect.y`. For bot 5
  in 2P that is DecalMP entry 1: `rect.y` is `boolUpdatedThisFrame` (+0x6),
  and the first rotation words (+0x10..0x17) are `renderW`, `renderH` and
  `lodIndex`. All of them stay 0 in LINK. The rest of bot 5's rotation, its
  translation and `rect.x`, and every byte bot 4 reads there, are bytes
  both modes write or neither does. Effect: bot 5's
  `Driver.PickupWumpaHUD.startX/startY` and the GTE rotation left after
  that call differ from retail. `startX/startY` are not in V4
  (game/MAIN/MainCanonicalDrivers.c:465 copies only
  `PickupLetterHUD.numCollected`) and are drawn only for humans. Two LINK
  cabinets both read zeros, so this is no LINK-vs-LINK desync. That the
  path is presentation-only relies on no later simulation code in that
  tick reading those GTE registers before setting them. Since fixed (last
  bullet): the native branch returns for a bot after the cooldown and
  count writes, before `RB_Pickup_SetCamera`, so no bot reads DecalMP here.
- "The bot 6 and 7 slots do not exist" holds for 2P only. In 1P (8
  drivers) `DecalMP_01` does not run, and `MainInit_FinalizeInit`
  (game/MAIN/MainInit.c:460-467) resets only each entry's `inst`, timer,
  and `ptrOT1/2` (`pb.ptrOT`, `pb.renderBucketOTRangeEnd`). Bot 6's rect
  width and height are entry 1's `pb.renderBucketScreenPos` (+0x118),
  which retail writes only in `RenderBucket_UpdatePushBufferMetadata`
  (RenderBucket_QueueExecute.c:1737); bot 7's are entry 2's `pb.bbox`,
  never written. So in retail a 1P race after a 2P race in the same
  process has history-dependent bot-6 missile targeting (and the crate
  path above reads other stale entry bytes for bots 6 and 7); LINK leaves
  them 0. The ONE_CAB evidence was a fresh process. This is no
  LINK-vs-LINK hazard: solo has no peer. Since the fix (last bullet)
  neither the missile check nor the crate path reads these bytes for bots
  4-7; one retail reader remains (next bullet).
- Known remaining reader, not fixed: `VehPickState_NewState`
  (game/Vehicle/VehPickState.c:313-321), outside `END_OF_RACE`, loads a
  bot attacker's `pushBuffer[driverID].matrix_ViewProj` and adds `rect.x`
  and `rect.y`, writing the human-only `BattleHUD.startX/startY` and
  leaving GTE state that `VehPhysForce_OnGravity` reloads in stage 4. It
  reads no host pointer in any reachable roster.
- Open item, not fixed here: the pre-existing bot plant-eaten camera
  writes (game/BOTS.c:2324-2341) store `gGT->pushBuffer[driverID].pos`
  and `rot` for bots >= 4 into DecalMP entries; render-only, but in
  non-LINK 2P bot 5 clobbers entry 0's `pb.renderBucketOTRangeEnd`, a
  latent render-only hazard (cannot happen in LINK). Fixed later in
  c2a848531: the plant-eaten branch (game/BOTS.c:2303-2376) now makes the
  camera writes only inside the guard
  `driverID < BOTS_PLANT_CAMERA_COUNT` (game/BOTS.c:2340-2360), pinned by
  the isolation test `bots_plant_camera_isolation`. On track 5 (Papu's
  Pyramid) the roster proof reports were byte-identical before and after
  the fix in both profiles (TWO_CAB autopilot and ONE_CAB), and the branch
  fired in those races for driverIDs 4, 5 and 7 (4 and 5 in TWO_CAB; 4, 5
  and 7 in ONE_CAB).
- Shield crash-attack flash, fixed in 9fdda8aa3 + 3aaa2c184:
  `RB_ShieldDark_ThTick_Grow` (game/231/RB_MaskShieldCloud.c:405-429)
  wrote `fadeFromBlack_currentValue/desiredResult` and `fade_step` to
  `pushBuffer[owner->driverID]` unbounded. In retail, driverID 4 would hit
  DecalMP+0x12/0x14/0x16 (entry 0 `data2[6..11]`), 5 +0x122/0x124/0x126
  (entry 0 `data3[0xE..0x13]`), 6 +0x232/0x234/0x236 (entry 1 +0x10A,
  then the low and high halves of `ptrOT1`), and 7 +0x342/0x344/0x346
  (entry 2 `data2[0xE6..0xEB]`) (layout in include/namespace_Main.h). In
  `struct DecalMPEntry` terms (game/DecalMP.c:7-19), as the BOTS.c and
  DecalMP.c comments name them: 4 hits `renderH` and `lodIndex`; 5 the high
  half of `pb.cameraID` and `pb.distanceToScreen_CURR`; 6 `pb.bbox.max.z`
  and `pb.ptrOT`; 7 `pb.RenderListJmpIndex[2]` (high half) and
  `pb.RenderListJmpIndex[3]`.
  The native branch (`#ifdef CTR_NATIVE`) adds
  `owner->driverID < RB_SHIELD_FLASH_CAMERA_COUNT` (4, static-asserted
  against the array length). The `#else` branch keeps the retail line,
  because game/231 is a retail match target. No bot can reach it:
  `instBubbleHold` is set only at game/Vehicle/VehPickupItem.c:990. Bots
  fire only IDs 2/3/4, never the shield (6)
  (game/PickupBots.c:145/158/170/207), the boss path is boss-only (boss
  driverID 1), and the roster proof fires only a clock
  (MainArcadeRosterProof.c:923). `ShootOnCirclePress` runs only for the
  PLAYER bucket (MainFrame.c:300-305, driverIDs 0..3). So the guard is
  defensive, pinned by `rb_shield_crash_flash_isolation`. Audit (`grep -rn
  "pushBuffer\[" game`): every other driverID-indexed access is either
  guarded or unreachable for bots. RB_Burst.c:111 and
  RB_GenericMine.c:183 check `ACTION_BOT`. The
  RB_Crate/RB_Crystal/RB_CtrLetter/RB_Fruit/`RB_Pickup_SetCamera` callers
  are DYNAMIC_PLAYER-only or return for bot weapon owners. The BOTS.c
  plant camera and the VehPickupItem missile target were already guarded.
  The only bot-reachable one left is the `VehPickState.c:313-321` reader
  above.
- Evidence (internal build, `--arcade-roster-proof`, seed 0x5EED, 3600 race
  ticks, a temporary env override of the accessor, not committed): TWO_CAB
  (2P, autopilot) retail against forced gave byte-identical reports, every
  per-tick control, rng, drivers, and V4 digest. ONE_CAB (1P) did too. With
  the missile check neutralised, the first version was identical too, so
  that read was the only coupling seen in the reports (the crate path above
  writes nothing they record).
- Pre-existing, found here: the missile check's bot 5 rect in 2P is entry
  1's host `inst` pointer. Instances live in the static .bss
  `s_mempackMemory` (platform/native_memory.c:31), so the pointer is the
  image base plus a fixed offset (0x00AE3C08 in one run). Windows moves the
  base in 64 KB steps (`/DYNAMICBASE` is on), so `rect.w` (the low half)
  changes with the build layout and `rect.h` (the high half) with the
  per-boot image base. Two retail builds split at race tick 2088. Two
  cabinets on different bases can pick different bot-5 missile targets
  (VehPickupItem.c:490); the live digest would then abort the race
  (detected, not silent). This affected the arcade build, forced LOD or
  not. On PS1 every candidate is rejected: `rect.h` is negative as s16 for
  a 0x800Axxxx pointer, and bot 4's `rect.w` is 0. Fixed as a retail bug
  under `CTR_NATIVE`: `VehPickupItem_MissileGetTargetDriver` rejects every
  candidate when `driver->driverID >= 4` (no camera PushBuffer), after the
  per-candidate GTE work and before the `rect` reads. That matches PS1 for
  bots 4 and 5 in 1P and 2P. Bots 6 and 7 exist only in 1P (bots spawn
  only below three players and 2P gets 6 drivers,
  game/MAIN/MainInit.c:322-360), where their `rect` is render state that
  is 0 in a fresh process (the bot 6 and 7 bullet above), so PS1 rejects
  them there too.
  The weapon branch of `RB_CrateFruit_ThCollide` returns for an
  `ACTION_BOT` owner before `RB_Pickup_SetCamera`, keeping the cooldown
  and count writes. That is defensive, presentation-only hardening: its
  only outputs are the human-only fly-in `startX/startY` and leftover GTE
  state that no simulation code consumes before reloading, and in
  reachable rosters it reads no host pointer (in 1P the entry `inst` and
  `ptrOT1/2` are NULL and `DecalMP_01` does not run; in 2P bots 4 and 5
  read `DecalMP_01` copies, never-written zeros, or render state). It would
  read one (`DecalMP[2].inst`, `pb.ptrOT`) only if an 8-driver
  multiplayer roster were allowed. Guarded by
  tests/veh_pickup_missile_target_bot_bounds_isolation_test.cmake.

### 8.5 Cost

Peak primMem per frame (`cursor - start` at the end of
`MainFrame_RenderFrame`, before `RenderSubmit`; the cursor only grows within
a frame), 2P, every track of the match select pool
(platform/native_match_select_rules.c:19). The budget is
`data.primMem_SizePerLEV_2P[track] << 10` (game/zGlobal_DATA.c:4480);
guardEnd is 256 bytes below it. "Dropped" counts frames on which the level
draw stopped at its primMem preflight. The last column is the top tier with
the LINK buffers below, so it is the unclipped demand.

| Track | 2P budget | Retail LOD peak | Top LOD peak | % of budget | Margin to guardEnd | Dropped | Top LOD, LINK buffer |
|---|---|---|---|---|---|---|---|
| 0 Dingo Canyon (a) | 225280 | 93860 | 106076 | 47.1% | 118948 | 0 | - |
| 1 Dragon Mines | 122880 | 84168 | 94476 | 76.9% | 28148 | 0 | - |
| 2 Blizzard Bluff | 163840 | 113632 | 136836 | 83.5% | 26748 | 0 | 136836 |
| 3 Crash Cove | 139264 | 95832 | 115120 | 82.7% | 23888 | 0 | 115120 |
| 4 Tiger Temple | 122880 | 103836 | 116140 | 94.5% | 6484 | 89 | 124304 |
| 5 Papu's Pyramid | 122880 | 103736 | 115404 | 93.9% | 7220 | 79 | 123364 |
| 6 Roo's Tubes | 174080 | 104356 | 114588 | 65.8% | 59236 | 0 | - |
| 7 Hot Air Skyway | 128000 | 95748 | 103920 | 81.2% | 23824 | 0 | - |
| 8 Sewer Speedway | 122880 | 89188 | 88800 | 72.3% | 33824 | 0 | - |
| 9 Mystery Caves | 122880 | 102320 | 112744 | 91.8% | 9880 | 0 | 112744 |
| 10 Cortex Castle | 141312 | 104960 | 108436 | 76.7% | 32620 | 0 | - |
| 11 N. Gin Labs | 124928 | 107376 | 116884 | 93.6% | 7788 | 2 | 117532 |
| 12 Polar Pass (a) | 129024 | 83972 | 91876 | 71.2% | 36892 | 0 | - |
| 14 Coco Park | 225280 | 90828 | 107388 | 47.7% | 117636 | 0 | - |
| 15 Tiny Arena | 148480 | 108044 | 119076 | 80.2% | 29148 | 0 | 119076 |
| 16 Slide Coliseum | 122880 | 100104 | 115528 | 94.0% | 7096 | 23 | 122300 |

- The retail LOD never dropped; its highest peak is 85.9% of budget
  (track 11, 107376 of 124928). Among the 120 KiB (122880) tracks the
  highest is 84.5% (track 4).
- With the top tier in the retail budget, five tracks passed 85% and four
  dropped level geometry (4, 5, 11, 16). Tracks 4 and 5 need more than
  their retail budget. No frame ended past guardEnd. Track 3 repeats the
  earlier track-3 numbers exactly. Peaks are single frames, so track 8's
  retail peak can sit above its top-tier peak.
- On tracks 4 and 5 the roster reports (every per-tick digest line) were
  byte-identical for the retail LOD, the top tier, and the top tier with
  the LINK buffers: the drops and the growth are presentation only.
- Method (not committed; repeat it to re-measure): an internal build,
  `--arcade-roster-proof` TWO_CAB, seed 0x5EED, dwell 0, 3600 race ticks,
  `--arcade-roster-proof-autopilot`, eight processes at once, with
  temporary local edits: an env override of both choices' `trackID` in
  `NativeArcadeRosterProof_BuildTwoCabConfig`; an env override making
  `MainArcadeLink_ForceTopLod()` return 1 (and, for the last column,
  `MainArcadeLink_GrowPrimMem` use LINK); a per-frame peak log before
  `RenderSubmit`; and a failure counter in the two
  `DrawLevelOvr1P_Has*PrimReserve` helpers. (a) On tracks 0 and 12 the
  proof stops with DRIVERS_FAILED at race tick 377 and 946 (the drivers
  extraction), with the retail LOD too. For these two tracks a further
  temporary edit kept the race and the autopilot running without tick
  lines. Since fixed (d9bb4e0bc, 5761b6eec): both tracks pass (section 9).

Overflow behaviour (retail code, unchanged):

- Most writers check guardEnd and skip the primitive: the shared allocator
  (game/prim.c:6), the instance draw per triangle
  (game/RenderBucket/RenderBucket_QueueExecute.c:2875 and nine more checks
  to :3832), particles (Particle.c:1131), shadows (VehGroundShadow.c:155),
  skids (VehGroundSkids.c:79), the fades (PushBuffer.c:202, :262), boxes
  (CTR/CTR_Box.c:24-117), menus (RECTMENU.c:65, :119), and some HUD
  pieces (UI_Meter.c:174, UI_RaceHud.c:175, UI_RenderFrame.c:703,
  FLARE.c:50).
- The level draw checks `end` before each block with a reserve (0xd68 to
  0x2700, game/226/226_00_DrawLevelOvr1P.c:36-41, :3207-3218), plus 0xd00
  in 2P (game/227/227_00_DrawLevelOvr2P.c:6, :264). On a failed check
  `DrawLevelOvr2P` returns, and the rest of the level is not drawn that
  frame. Instances are drawn before the level (MainFrame_RenderFrame.c:173,
  :193), so an overflow shows as missing level geometry.
- Other writers do not check at all: tires (DrawTires.c:499, :1064, right
  after the instances), heat particles (Torch.c:497), and, after the level,
  the skybox glow (CAM.c:73-133), `DecalMP_03` (DecalMP.c:239), the HUD
  icon quads (DecalHUD.c:28-256, e.g. DotLights), the split-screen lines
  (MainFrame_RenderFrame.c:1149-1245), `CAM_ClearScreen` (CAM.c:304), and
  the checkered flag (RaceFlag.c:556). Early writers (weather, confetti,
  stars, most HUD) do not check either.
- So in the measured overflow only late primitives are dropped (level
  geometry, graphics only). A write past `end` is still possible: if the
  instances fill to guardEnd, the tires, heat particles, and late writers
  write past `end` unchecked. Past db[0]'s end is db[1]'s primMem, and past
  db[1]'s is db[0]'s OT (MainInit_PrimMem, then MainInit_OTMem;
  LOAD/LOAD_TenStages.c:210-211). Those are render buffers, not simulation
  objects, but a clobbered OT link can derail the GPU walk. No measured
  frame came near it. With the LINK buffers below the neighbours differ:
  past db[0]'s host buffer is db[1]'s host buffer, and past db[1]'s is
  arbitrary host .bss (not db[0]'s OT). The margin there is large (the
  highest LINK peak, with the level near tier, is 45% of the 1 MiB
  buffer), so no slack is added. Also
  retail: while paused, `ElimBG` lowers `end` by 0xc800 without moving
  guardEnd (ElimBG.c:92-97).

LINK primMem (the fix for the drops). `MainInit_PrimMem` still makes both
retail MEMPACK allocations of the retail size (`MainDB_PrimMem`
unchanged), so the MEMPACK layout and every simulation object after them
are unchanged. Then, under `CTR_NATIVE`, `MainArcadeLink_GrowPrimMem`
(game/MAIN/MainArcadeLink.c) points each draw buffer's start, cursor,
end, guardEnd, and capacityBytes at a static 1 MiB host buffer
(`MainArcadeLinkPolicy_PrimitiveBytes`: LINK -> 0x100000, never below the
retail size; any other mode keeps the retail size). The retail blocks stay
reserved and unused. The per-frame GPU link ranges use start and
capacityBytes (MainFrame.c:18), so they cover the new buffers. LINK only,
the gate of the top tier that needs it, so every other mode (the roster
proof, replay, previews) keeps the retail buffers. The checkpoint
relocates primMem pointers only inside its address ranges
(platform/native_checkpoint.c:665-676), so a host pointer would survive
only an in-process restore. Arcade-link mode rejects replay record and
playback (main.c) and refuses both quick-state hotkeys
(platform/native_platform.c), so nothing captures one. If the host falls
back from LINK to OFF mid-level (the defensive branch of
`NativeArcadeLinkHost_AbortToTitle`), the buffers stay bound until the next
level load while the hotkeys pass again. That corner is closed by the quick
save and load themselves (platform/native_savestate.c): both refuse while
any draw buffer's primMem start is not its allocationStart, a structural
check that names neither the accessor nor the buffers. With the retail
level tiers and the earlier 256 KiB buffers the highest measured peak was
136836 bytes, 52% of 262144. The buffers grew to 1 MiB for the level near
tier below (the owner allows 1 MiB or more on PC).
tests/main_arcade_link_prim_mem_isolation_test.cmake pins the structure,
and tests/main_arcade_link_policy_test.c the size rule.

Level near tier (8.3). Peak primMem per frame, measured as above (2P,
TWO_CAB roster proof, seed 0x5EED, dwell 0, 3600 race ticks, autopilot;
temporary local edits: an env override making the three LINK accessors use
LINK, the per-frame peak log before `RenderSubmit`, and a failure counter
in the two `DrawLevelOvr1P_Has*PrimReserve` helpers), with the top
instance tier and the 1 MiB LINK buffers (1048576 bytes). "Dropped" counts
frames on which the level draw stopped at its primMem preflight.

| Track | Retail level tier (8.5 table, LINK buffer) | BSP near slot + texture tiers | + top-level subdivision (shipped) | % of 1 MiB | Dropped |
|---|---|---|---|---|---|
| 4 Tiger Temple | 124304 | 134324 | 300952 | 28.7% | 0 |
| 5 Papu's Pyramid | 123364 | 148188 | 321460 | 30.7% | 0 |
| 11 N. Gin Labs | 117532 | 128308 | 273924 | 26.1% | 0 |
| 16 Slide Coliseum | 122300 | 189188 | 471384 | 45.0% | 0 |

- The shipped tier peaks at 45.0% (track 16), below the owner's 70-75%
  ceiling, with no dropped frame and no renderer vertex-buffer warning on
  any of the four tracks; all four proofs passed (3600/3600 ticks).
- Forcing `recursiveNear` as well (every quad fully subdivided at every
  distance) was measured and rejected: track 16 filled the buffer (peak
  1042024 of 1048576) and dropped level geometry on 10 frames, and on
  tracks 4 and 16 the native renderer hit `MAX_VERTEX_BUFFER_SIZE`
  (platform/native_renderer.c:2638) and the process crashed. Tracks 5 and
  11 peaked at 735372 and 776120 bytes (70% and 74%).
- Only the 2P tracks were measured. 1P, 3P, and 4P LINK races take the
  same tiers (the 1P render-frame seed and the 3P/4P split-ground seed) but
  have no measurement yet.

tests/main_arcade_link_top_lod_isolation_test.cmake pins the structure. It
checks the two guarded call sites, the unchanged walk and its cull, the
read-only top-tier pick, the retail animation frame count, the kept DecalMP
entry writes, and that `maxDistanceLOD` has no writer but the cutscene
opcode.

## 9. Every arcade track: the V4 drivers extraction

The roster proof failed DRIVERS_FAILED on Dingo Canyon (0; TWO_CAB autopilot
race tick 377, ONE_CAB 1785) and Polar Pass (12; TWO_CAB 946). A human in a
hazard's hit radius (armadillo, seal) is hit again while spinning: retail
`DefaultSpin` skips the queued init when already `KS_SPINNING`
(game/Vehicle/VehPickState.c:240-242), then clears `kartState` (:299), so a
spin suffix (7..10) runs with kartState 0 and a live Spinning union. That is
retail behaviour (game code unchanged); the V4 active-tag contract rejected
it. d9bb4e0bc accepts it with tag SPIN; 5761b6eec makes one predicate
(`IsSpinReHit`, platform/native_canonical_driver_behavior.c) drive both the
allowed mask and the resolved tag, excluding the queued inits 6..8 (NONE).

The sweep, one `--arcade-roster-proof` per (track, profile), all 16
match-select tracks, seed 0x5EED, dwell 0, 3600 race ticks; TWO_CAB with
`--arcade-roster-proof-autopilot`, ONE_CAB on the scripted pads (Debug build
of 5761b6eec's code, 32 runs, 8 at a time, 684 s, about 170 s per run):

| Track | Name | TWO_CAB (autopilot) | ONE_CAB |
|---|---|---|---|
| 3 | Crash Cove | PASS, 3600/3600 | PASS, 3600/3600 |
| 6 | Roo's Tubes | PASS, 3600/3600 | PASS, 3600/3600 |
| 4 | Tiger Temple | PASS, 3600/3600 | PASS, 3600/3600 |
| 14 | Coco Park | PASS, 3600/3600 | PASS, 3600/3600 |
| 9 | Mystery Caves | PASS, 3600/3600 | PASS, 3600/3600 |
| 2 | Blizzard Bluff | PASS, 3600/3600 | PASS, 3600/3600 |
| 8 | Sewer Speedway | PASS, 3600/3600 | PASS, 3600/3600 |
| 0 | Dingo Canyon | PASS, 3600/3600 | PASS, 3600/3600 |
| 5 | Papu's Pyramid | PASS, 3600/3600 | PASS, 3600/3600 |
| 1 | Dragon Mines | PASS, 3600/3600 | PASS, 3600/3600 |
| 12 | Polar Pass | PASS, 3600/3600 | PASS, 3600/3600 |
| 10 | Cortex Castle | PASS, 3600/3600 | PASS, 3600/3600 |
| 15 | Tiny Arena | PASS, 3600/3600 | PASS, 3600/3600 |
| 7 | Hot Air Skyway | PASS, 3600/3600 | PASS, 3600/3600 |
| 11 | N. Gin Labs | PASS, 3600/3600 | PASS, 3600/3600 |
| 16 | Slide Coliseum | PASS, 3600/3600 | PASS, 3600/3600 |

"3600/3600" is the race ticks logged (every tick line present, report v12,
`end ticks 3600`). Within 3600 race ticks only one autopilot human finished
(Crash Cove, player 0 at race tick 3552) and no race reached END_OF_RACE, so
this sweep covers racing and the hazards, not the finish. The finish is
recorded at 6000 race ticks under "Race finish (6000 ticks)" below (END_OF_RACE
on 9 of 16 tracks). Its Crash Cove player 0 finish (3608) is a different
race from this sweep's 3552: the two builds have different build identities,
so a different setup seed.

The sweep (output under the ignored build tree):

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/arcade-roster-track-sweep.ps1 \
    -Executable build-msvc-x86/Debug/ctr_native.exe \
    -OutputDirectory C:/re-tools/ctr-native/build-msvc-x86/arcade_roster_track_sweep_record/3600 \
    -Profile both -Ticks 3600 -Parallel 8 -TimeoutSeconds 900
```

The live ctest `arcade_roster_track_sweep` (labels `live;live-roster`,
skips with 77) runs the same sweep at 1800 race ticks, past the latest known
failures (946 TWO_CAB, 1785 ONE_CAB), both profiles, with `-Pairs two-cab`:
every TWO_CAB run twice as a same-seed pair whose reports must be
byte-identical (below), ONE_CAB once, 16 at a time: 48 runs of 113-115 s,
348 s for the test (measured alone, Debug; TIMEOUT 900, 300 s per run). The
fast test `arcade_roster_track_sweep_plan`
(tests/arcade_roster_track_sweep_plan_test.cmake) pins its plan through
`-ListRuns`: the 16 tracks of `k_matchSelectTracks`, the flags per profile
(the autopilot only on TWO_CAB), the pairs (run b's arguments are run a's but
for the report path, `track<NN>-<profile>-b`), the arguments and timeouts the
live test registers, and the rejection of the non-table tracks 13 and 17 and
of `-Pairs` naming a profile `-Profile` does not run.

### Per-track identity

`-Pairs` makes each run of a paired profile twice with byte-identical game
arguments (seed 0x5EED, dwell 0) except the report path. Once both runs of a
pair pass, the sweep compares the two whole reports byte-for-byte (the report
holds no run-specific text, as the checker's A = B and F = G already rely
on); a difference fails the pair and prints the first differing tick, the
digest fields that differ there (control, rcontrol, rng, input, drivers, v4,
...), and both lines. A TWO_CAB pair also requires equal stdout autopilot
summaries (END_OF_RACE and both finish ticks). Before this, byte-compared
same-seed determinism was proven on track 3 only (`arcade_roster_determinism_*`).

Record run (Debug build of 3aaa2c184 with this sweep, 1800 race ticks, 64
runs, 16 at a time: 460 s, 110-115 s per run; every run PASS 1800/1800):

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/arcade-roster-track-sweep.ps1 \
    -Executable build-msvc-x86/Debug/ctr_native.exe \
    -OutputDirectory C:/re-tools/ctr-native/build-msvc-x86/arcade_roster_track_identity_record/1800 \
    -Profile both -Pairs both -Ticks 1800 -Parallel 16 -TimeoutSeconds 600
```

| Track | Name | TWO_CAB a==b | ONE_CAB a==b |
|---|---|---|---|
| 3 | Crash Cove | a==b | a==b |
| 6 | Roo's Tubes | a==b | a==b |
| 4 | Tiger Temple | a==b | a==b |
| 14 | Coco Park | a==b | a==b |
| 9 | Mystery Caves | a==b | a==b |
| 2 | Blizzard Bluff | a==b | a==b |
| 8 | Sewer Speedway | a==b | a==b |
| 0 | Dingo Canyon | a==b | a==b |
| 5 | Papu's Pyramid | a==b | a==b |
| 1 | Dragon Mines | a==b | a==b |
| 12 | Polar Pass | a==b | a==b |
| 10 | Cortex Castle | a==b | a==b |
| 15 | Tiny Arena | a==b | a==b |
| 7 | Hot Air Skyway | a==b | a==b |
| 11 | N. Gin Labs | a==b | a==b |
| 16 | Slide Coliseum | a==b | a==b |

All 32 pairs are byte-identical whole reports; every TWO_CAB autopilot
summary is END_OF_RACE -1, player 0 finish -1, player 1 finish -1 (no finish
within 1800 race ticks) in both runs. The live ctest (`-Pairs two-cab`, run
alone: 348 s) reproduced the 16 TWO_CAB pairs identical.

The live ctest gates the TWO_CAB pairs only. ONE_CAB pairs on every track
stay a record: `-Pairs both` took 460 s, four full waves of 16 on this
16-thread machine, only 4% under the ~480 s budget for the test, so any
per-run slowdown of about 5 s would cross it; ONE_CAB same-seed identity
stays gated on track 3 (`arcade_roster_determinism_one_cab`, F = G bytes).
Gating it is `-Pairs both` with TIMEOUT at least 920.

### Battle-only gap (RB_Player.c:87)

Analysed from the code and deferred. The owner kept the canonical rule
unchanged. There was no live run.

The chain. A hit on a human (`VehPickState_NewState`,
game/Vehicle/VehPickState.c:48) queues a damage init and never touches the
suffix pointers `funcPtrs[1..12]`: Tumble for a blast (:182), PlantEaten for
a mask grab (:200), SpinFirst for spin, squish or burn (:246), or nothing on
a spin re-hit (:240-242). It then clears `kartState` to `KS_NORMAL` (:299).
With an attacker and no `END_OF_RACE` (:308) it calls `RB_Player_KillPlayer`
(:330), which returns unless `BATTLE_MODE` is set (game/231/RB_Player.c:7-10)
and, past the `POINT_LIMIT` branch, `LIFE_LIMIT` (:67-70). When the
victim's last life goes (:72-78), it overwrites the queued init with
`VehStuckProc_RIP_Init` (:87). This leaves init 5
(game/MAIN/MainCanonicalDrivers.c:35), kartState 0, and whatever suffix ran
before the hit.

When extraction sees it. A crash hit is queued in `pendingDamageType`.
`VehPickupItem_ShootOnCirclePress` (game/Vehicle/VehPickupItem.c:1519)
applies it at game/MAIN/MainFrame.c:305, before the driver stages
(:311-328) of the same frame. So RIP_Init runs at once: `PlantEaten_Init`
sets kartState 5 and clears the init (game/Vehicle/VehStuckProc.c:879,
:922), which gives behavior 13 with kartState 5 (PLANT_EATEN, accepted).
A weapon hit goes through `RB_Hazard_HurtDriver`, for example
game/231/RB_Burst.c:87-100, RB_GenericMine.c:177 and :252, and
RB_TNT.c:220. It runs in a later thread bucket (MainFrame.c:334), after
the driver stages, so the tuple is still queued at the tick boundary where
`MainCanonicalDrivers_ExtractDriverActive` (MainCanonicalDrivers.c:589; its
`ResolveActualActiveTag` call is :596) resolves it. Bot victims take `BOTS_ChangeState` instead
(game/231/RB_Hazard.c:9-17) and are NONE-only anyway.

The contract (platform/native_canonical_driver_behavior.c). For init 5
with kartState 0, `AllowedActiveTagMask` (:70-113) adds no queued-init NONE:
that is only for inits 6..8 (:90) and for init 1 with kartState 4 (:91). A
suffix passes only if it expects kartState 0 or `IsSpinReHit` (:59-62,
which excludes only inits 6..8) admits it (:109):

| Suffix | Live at the hit | Expects | (init 5, suffix, kartState 0) |
|---|---|---|---|
| 1 driving | yes | 0 | behavior 86, accepted, NONE |
| 4, 5 drift (PowerSlide) | yes | 2 (:98) | behaviors 89, 90, rejected |
| 6 slam (SlamWall, `KS_CRASHING`) | yes | 1 (:99) | behavior 91, rejected |
| 3 anti-vshift (FreezeVShift, `KS_ANTIVSHIFT`) | yes: `VehPhysProc_Driving_Update` calls `FreezeVShift_Init` whenever `vShiftCount >= 5`, in any race or battle (VehPhysProc.c:1686, kartState 9 at :1850), and `VehPickState_NewState` has no early return for 9 | 9 (:97) | behavior 88, rejected |
| 7..10 spin | yes | 3 | behaviors 92..95, accepted, SPIN |
| 15 tumble (`KS_BLASTED`) | by a non-blast hit (below) | 6 (:104) | behavior 100, rejected |
| 14 rev engine | not traced (mask respawn, VehStuckProc.c:747) | 4 | behavior 99, rejected if reached |
| 11..13 mask grab, plant | no: `KS_MASK_GRABBED` returns first (VehPickState.c:65-68) | 5 | - |
| 2, 16 freeze, warp | no: adventure hub, Aku hint or crystal challenge only (MainFrame.c:791, UI_RenderFrame.c:985, game/232/AH_*) | 11, 10 | - |

A blast returns on `KS_BLASTED` (VehPickState.c:152), but spin, squish and
burn do not. A tumbling driver gets no invincibility until `Driving_Init`
(game/Vehicle/VehPhysProc.c:1707-1709). So the premise holds: the rejected
tuples are drift (89, 90), slam (91) and anti-vshift (88), plus tumble (100)
and possibly rev engine (99). The spin suffixes are accepted as SPIN, not
rejected. The fix below (init 5 queued, so NONE for any suffix) covers 88
as well.

Reachability: none in an arcade race. The arcade race setup plan sets
`gameMode1 = (before & TRANSIENT) | ARCADE_MODE`
(game/MAIN/MainArcadeRaceSetupPlan.c:200-201, :243; masks
MainArcadeRaceSetupPlan.h:359-369). The adapter stores it at
game/MAIN/MainArcadeRaceSetup.c:137, and the same plan serves both
profiles. `BATTLE_MODE` (0x20) and `LIFE_LIMIT` (0x8000) are not transient,
so the plan clears both. The only `BATTLE_MODE` setter is the battle menu
row (game/230/MM_MenuFlow.c:253). The match config must also carry
`gameMode1` 0 (platform/native_arcade_bot_rules.c:458; link fixture
platform/native_arcade_link_options.c:415).

The other RIP_Init writer, game/233/CS_Camera.c:234 (`drivers[0]`), is the
adventure podium camera (`CS_Camera_ThTick_Podium`), not a cutscene in a
race. Only `CS_Podium_FullScene_Init` spawns it
(game/233/CS_Podium.c:743). game/MAIN/MainInit.c:670-676 calls that only
under `ADVENTURE_ARENA` with a podium reward, and the arcade plan clears
`ADVENTURE_ARENA`. It is unreachable in arcade races.

What a fix would need (not implemented):

- Treat init 5 as a queued init. That means `init>=5&&init<=8` at :90 and
  the same precedence in `ResolveActualActiveTag` (:138), so that
  (init 5, any suffix, kartState 0) is NONE.
- Exclude init 5 from `IsSpinReHit` (:61). Otherwise behaviors 92..95 with
  kartState 0 would allow both NONE and SPIN. That is two encodings of one
  state, and `ResolveActiveTag` (:114) refuses a non-singleton mask.
- Ambiguity and review risks:
  - This changes an accepted encoding: 92..95 with kartState 0 move from
    SPIN to NONE, which drops the Spinning bytes.
  - The CS_Camera.c:234 podium write would get the same NONE for any
    suffix.
  - A reviewer must confirm that dropping the suffix union is lossless.
    RIP_Init runs `PlantEaten_Init`, which writes kartState and
    `EatenByPlant.boolInited` (VehStuckProc.c:879-881), not the whole
    union. Inits 6..8 already make the same trade.
- Tests that would change: tests/native_canonical_driver_behavior_test.c.
  - `LegacyValidateState` (:22) and `ActualTagOracle` (:63) would use the
    6..8 bounds as 5..8.
  - The queued-init loops (:115, :200, :239) would also start at init 5.
  - `TestSpinReHitRows` lists init 5 as a steady spin re-hit init (:157,
    comment :153-156).
  - The drift loop asserts that (init 5, 4/5, kartState 0) is rejected
    (:208-214).
  - tests/main_canonical_drivers_binding_test.c needs new rows. Its spin
    re-hit rows use init 10 (:523, :1052, :1335), and its RIP row is
    behavior 98 with kartState 5 (:1360); none of these change.
- It needs a `reviewer` pass because it changes the canonical acceptance
  contract.

Deferred because it cannot happen in an arcade race (no `BATTLE_MODE` or
`LIFE_LIMIT`, and no podium) and it changes the canonical acceptance
contract for no arcade gain. Revisit it if battle mode reaches the
cabinets.

### Race finish (6000 ticks)

Before this run, no gate reached a race finish except on track 3. At the
two-cab tick cap (6000 race ticks, the script's `MaxTicks` and the game's
option), every TWO_CAB run was paired with `-Pairs two-cab`. A pair
reported as a==b with a given END_OF_RACE tick is the one-machine evidence
that two cabinets fed the same inputs reach END_OF_RACE on the same race
tick. Record run (Debug build of 37d54726f, 32 runs, 16 at a time: 522 s,
250-256 s per run; every run PASS 6000/6000, 16 of 16 pairs
byte-identical):

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/arcade-roster-track-sweep.ps1 \
    -Executable build-msvc-x86/Debug/ctr_native.exe \
    -OutputDirectory C:/re-tools/ctr-native/build-msvc-x86/arcade_roster_track_finish_record/6000 \
    -Profile two-cab -Pairs two-cab -Ticks 6000 -Parallel 16 -TimeoutSeconds 1200
```

| Track | Name | TWO_CAB a==b | END_OF_RACE | p0 finish | p1 finish | Classification |
|---|---|---|---|---|---|---|
| 3 | Crash Cove | a==b | 3705 | 3608 | 3705 | finished |
| 6 | Roo's Tubes | a==b | 3645 | 3645 | 3611 | finished |
| 4 | Tiger Temple | a==b | 4995 | 4981 | 4995 | finished |
| 14 | Coco Park | a==b | 4021 | 3970 | 4021 | finished |
| 9 | Mystery Caves | a==b | 5515 | 5515 | 5505 | finished |
| 2 | Blizzard Bluff | a==b | 4202 | 4202 | 4197 | finished |
| 8 | Sewer Speedway | a==b | -1 | -1 | -1 | autopilot: pinned on a wall |
| 0 | Dingo Canyon | a==b | 4007 | 4007 | 3922 | finished |
| 5 | Papu's Pyramid | a==b | 4795 | 4780 | 4795 | finished |
| 1 | Dragon Mines | a==b | 4063 | 4060 | 4063 | finished |
| 12 | Polar Pass | a==b | -1 | -1 | -1 | tick cap |
| 10 | Cortex Castle | a==b | -1 | -1 | -1 | autopilot: fall and respawn loop |
| 15 | Tiny Arena | a==b | -1 | -1 | -1 | tick cap |
| 7 | Hot Air Skyway | a==b | -1 | -1 | -1 | tick cap |
| 11 | N. Gin Labs | a==b | -1 | -1 | -1 | autopilot: fall and respawn loop |
| 16 | Slide Coliseum | a==b | -1 | -1 | -1 | tick cap |

The ticks are race ticks from the stdout autopilot lines, the same in runs
a and b (the sweep also requires equal summaries). -1 means never within
6000 race ticks. Each race has 3 laps.

Nine tracks (0, 1, 2, 3, 4, 5, 6, 9, 14) reach END_OF_RACE, and both runs
of each pair reach it on the same race tick with byte-identical whole
reports. On every one of them, END_OF_RACE is the later human's finish
tick, and the two humans finish 3 to 97 race ticks apart. That makes 8
tracks with a two-cabinet race finish, on top of track 3. The seven tracks
that do not finish (7, 8, 10, 11, 12, 15, 16) show no game issue: PASS
6000/6000, a==b, no FAIL, no crash, and no "player finished" line for
either human.

The proof logs no lap or restart-point progress, so the classifications
come from a progress diagnostic, which was temporary and is not committed
(reverted, and the Debug build rebuilt at HEAD).
`MainArcadeRosterProof_AutopilotStep` logged to stdout, read-only, every
300 race ticks and on every `lapIndex` change for players 0 and 1: lap,
`distanceToFinish_curr`, position, speed, `kartState`, `actionsFlagSet`,
and the autopilot's restart-point target. The autopilot cap
(`NATIVE_ARCADE_ROSTER_PROOF_AUTOPILOT_MAX_TICKS`, the tick option's
4-digit parse, and the sweep's two-cab `MaxTicks`) was raised to 14000. The
seven tracks and track 9 (baseline) ran once each, 8 at a time: 520 s,
every run PASS 14000/14000.

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/arcade-roster-track-sweep.ps1 \
    -Executable build-msvc-x86/Debug/ctr_native.exe \
    -OutputDirectory C:/re-tools/ctr-native/build-msvc-x86/arcade_roster_finish_diag \
    -Profile two-cab -Pairs none -Ticks 14000 -Tracks 8,12,10,15,7,11,16,9 -Parallel 8 -TimeoutSeconds 2400
```

Which race this is: the proof config carries the build identity, and a
dirty tree runs under the fixed proof build identity. So the diagnostic
build does not replay the 37d54726f record's race. Its setup seed is
0x5218508ABCA49688 (the record's is 0xB5C373CFEF03CDEF), and its tick lines
differ from the 6000-tick record's from tick 0 (rng, drivers, v4, v4rng,
v4drivers; control, rcontrol, input, v4control, v4input, v4world and
v4topology are equal). It does
replay the race of the 1800-tick identity record above, which was also
built from a dirty tree. On all 8 tracks the config digest is equal (the
tick count is not in it), and the first 1800 tick lines are byte-identical,
so neither the log nor the raised cap perturbs the simulation. The causes
below come from the steering and the track geometry, not the seed, but the
ticks are this race's.

| Track | Name | END_OF_RACE | p0 laps end | p1 laps end | Lap (distance units) | Cause |
|---|---|---|---|---|---|---|
| 9 | Mystery Caves | 5485 | 1867, 3590, 5314 | 1874, 3684, 5485 | ~92,600 | baseline |
| 16 | Slide Coliseum | 6135 | 2079, 4096, 6029 | 2124, 4084, 6135 | ~92,200 | tick cap |
| 7 | Hot Air Skyway | 7740 | 2666, 5224, 7740 | 2676, 5210, 7711 | ~129,900 | tick cap |
| 12 | Polar Pass | 7833 | 2713, 5304, 7833 | 2642, 5172, 7719 | ~122,700 | tick cap |
| 15 | Tiny Arena | 9474 | 3216, 6272, 9425 | 3185, 6210, 9474 | ~142,300 | tick cap |
| 8 | Sewer Speedway | -1 | none | none | - | autopilot: wall |
| 10 | Cortex Castle | -1 | none | 2294, 4416 | - | autopilot: falls |
| 11 | N. Gin Labs | -1 | none | none | - | autopilot: falls |

Tick cap (16, 7, 12, 15): both humans finish every lap at a steady pace,
1933 to 3264 race ticks per lap. No sample shows wall contact or a respawn.
The laps of 7, 12 and 15 are 1.3 to 1.5 times as long as Mystery Caves',
at about the same pace (laps 2-3, excluding the standing start: 44 to 52
distance units per race tick, against 51 to 54). Slide Coliseum's lap is as
long as Mystery Caves', driven at 45 to 48 units per tick (laps 2-3). The races end 135 to 3474 ticks past 6000.

Sewer Speedway (8), pinned on a wall: by race tick 900 both humans stop in
lap 0 near x -3350 / -3480, z -20490, y 1 (distanceToFinish about 58,850 /
58,750), and they stay there to race tick 14000, creeping less than 100
units along the wall. From race tick 900 on, every sample shows speed 373
to 689 (cruise is about 13,000) and CROSS held without steering, and 87 of
the 88 samples show `ACTION_DRIVING_AGAINST_WALL` (not player 1 at race
tick 6300). The
target, restart point 78, is at (-3073, 799, -20615), 800 units above the
kart. The autopilot steers in x/z only and cannot reverse or unstick, so it
drives straight into the wall under the upper route.

Cortex Castle (10), fall and respawn loop: from about race tick 1800 in
lap 0 (distanceToFinish 23,500 to 25,000 of about 105,000), player 0 falls
at x -9051, z -1110, from about y 2500. Aku Aku picks it up (`kartState` 5,
mask flag), it respawns at (-9913, ~2400, -2304), and it drives back into
the same drop. It never passes restart point 104 (-9216, 2688, -384), and
the loop repeats to race tick 14000. Player 1 passes the spot twice (laps
end 2294 and 4416), then falls into the same loop in lapIndex 2 (third lap)
by race tick
6600. Every respawn completes (`kartState` 5, then 4, then 0, driving), so
the game recovers the kart; the autopilot's line takes it off the edge.

N. Gin Labs (11), fall and respawn loop: the same pattern for both humans,
from about race tick 1500 in lap 0. Player 0 falls at (18198, 26580), short
of restart point 97 (18624, 384, 26688), and respawns at
(17856, ~200, 27072). Player 1 falls at (18394, 25025), short of point 102
(16896, 768, 24192), and respawns at (18624, ~580, 25152). The samples
catch the karts falling as low as y -1389 (player 1) and -2394 (player 0),
and the loop repeats to race tick 14000.

Conclusion: nothing here is a game issue. Every run is PASS 14000/14000
with no FAIL, and laps, finishes and Aku Aku respawns work on every track.
Four tracks are tick cap: their 3 laps take more than 6000 race ticks.
Three are autopilot quality: a wall on 8, and falls on 10 and 11. Of the
required tracks, Polar Pass (12) finishes at 7833 and Slide Coliseum (16)
at 6135, both past the 6000 cap. A two-cabinet finish on them needs a
higher autopilot cap (the report's tick-line array is sized by it). Tracks
8, 10 and 11 need an autopilot that follows the route in 3D and keeps off
the drops.
