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
9. No two-cabinet live test yet of "peer wakes during solo, then links
   from the LOBBY". That is a later live proof. `arcade_solo_race` runs two
   independent solo cabinets whose peers stay silent, so it never covers
   the wake. Unit coverage, over real loopback sockets:
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

Open: risks 3 and 9 (no live test of the race window or of a peer that
wakes during solo), risk 6 (an abandoned LOBBY still waits forever), and
physical two-cabinet validation (HANDOFF steps 6 and 7).

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
   displayed, and the XA is neither played nor waited on. Every load, the
   howl and music init, the memcard init, and the four topology-lease calls
   run as before, in the same order. Skipping the play leaves no XA state
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
- Known cosmetic effect: `Music_SetIntro` and `CseqMusic_Start`
  (game/MAIN/MainMain.c:795-797) still start the ND-crate song in
  StateZero. The crate load keeps music playing
  (`boolPlayMusicDuringLoading`, game/LOAD/LOAD_TenStages.c:51), so in
  LINK the song plays over a black screen during the crate load until
  `CseqMusic_StopAll` on the first crate tick (CS_Thread.c:377). Nothing
  carries over to the title. An owner listen is pending.

tests/main_arcade_link_boot_intro_isolation_test.cmake pins the structure:
the three guarded call sites, StateZero's load and lease order, the exact
gated blocks, the retail START-skip path, and that no pad word is written.
