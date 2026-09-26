# Solo-cabinet milestone

Design-and-status record for a player-facing solo (ARCADE_ONE_CAB) flow on
the arcade link, on branch `arcade`: a linked cabinet whose peer is silent
offers the player a solo race (one human and seven bots, the retail 1P
arcade race) from its LOBBY, and keeps listening for the peer in the
background. Read AGENTS.md and docs/HANDOFF.md first. This document follows
the pattern of docs/MATCH_SELECT_MILESTONE.md and
docs/GAME_LOOP_UI_MILESTONE.md: a prospective plan with a slice list,
updated to record status as slices land. Every line citation was checked
against the tree at e614b7327. Physical two-cabinet validation stays gated
behind HANDOFF steps 6 and 7.

## 1. Why

ONE_CAB is complete at the simulation level through the race setup seam
(game/MAIN/MainArcadeRaceSetup.h: Arm :91, Launch :95, Disarm :153). The
live test `arcade_roster_determinism_one_cab` proves it
(tools/arcade-roster-proof-check.ps1 `-Group one-cab`,
CMakeLists.txt:2095-2109).
That proof (RS-23, docs/ROSTER_MILESTONE.md:704-710) launches from the title
with no lobby and no select.

No player-facing ONE_CAB flow exists:

- docs/ROSTER_MILESTONE.md RS-1 (:545-553): "the arcade-link lobby, and match
  select remain TWO_CAB-only [...] a ONE_CAB lobby/UI flow is a follow-up
  (risk 14)"; risk 14 (:1095-1101).
- docs/RACE_LAUNCH_MILESTONE.md risk 9 (:1037): "The ONE_CAB lobby and UI
  flow is a follow-up".

Today a linked cabinet whose peer is off sits on the LOBBY forever (UX-5,
docs/GAME_LOOP_UI_MILESTONE.md:516). With the default attempt budget of
150 ticks per candidate
(include/platform/native_arcade_netplay.h:201), one cycle is 150 ticks of
CONNECTING (a HELLO every tick) and then WAITING. After
lobbyRetryPauseTicks = 30 (include/platform/native_arcade_flow.h:64) the
flow restarts the cycle (platform/native_arcade_flow.c:141-153, :179-182).
BACK (Triangle) goes to EXIT and the title (:158-163, :380-392).

Owner direction:

- One versus two cabinets is not a static setting. It follows from whether
  the other cabinet is heard, and a continuous background poll checks for it
  waking (docs/HANDOFF.md:841-847).
- RS-1 was overridden: both ONE_CAB and TWO_CAB are supported.
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
- Race launch: game/MAIN/MainArcadeRaceLaunch.c.
  - The race config comes from NativeArcadeLinkHost_GetAgreedConfig (:368),
    followed by Arm and Launch (:373-377).
  - Each tick goes through NativeArcadeLinkHost_RaceStep and RaceHold
    (:893, :699).
  - End kinds are handled at :817-842. Disarm is RL-9 (:999-1008).
  - InstallPads hard-codes PROFILE_TWO_CAB (:258-264).
  - tests/main_arcade_race_setup_isolation_test.cmake:9-14 allows Arm,
    Launch, and Disarm only in this caller, each named exactly once.
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
  (:51-55).

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
   - Reason: a cabinet pair booting together links before anyone can go
     solo. The START that entered the LOBBY cannot start solo either, since
     release-to-arm already swallows it (netplay.c:745-751); the delay adds
     margin.
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
     It latches `peerAwake` when a well-formed handshake datagram
     (NativeLockstepHandshakeMessageV1_Decode,
     include/platform/native_lockstep_handshake.h:146) arrives from a
     configured peer.
   - Nothing is sent during solo, so a woken peer stays in its own LOBBY,
     where it can go solo too. A peer never interrupts solo select, race,
     or results.
   - This is the owner's "continuous background poll".
5. SOLO-5 (default): Select. Solo uses the same SELECT and SELECT_RESULT
   screens and rules for one human (humanCount 1, localHuman 0 on either
   seat) on a ONE_CAB base: the OD-1 per-item timer and automatic pick
   (docs/MATCH_SELECT_MILESTONE.md:560-562), the SEL-5 order (:600-603),
   the SEL-6 automatic pick (:604-606), SEL-7 BACK ignored (:607-608), and
   the SEL-8 result hold (:609-610). It drops the opponent footer and the
   peer-locked markers, SEL-9 peer silence, the relink, and the launch
   agreement. Characters are the 8 base characters only (RS-20,
   docs/ROSTER_MILESTONE.md:679-686). Tracks and laps are the linked lists
   (SEL-2, SEL-3: no Oxide Station).
6. SOLO-6 (default): Config. The solo race config is a ONE_CAB
   NativeMatchConfigV1 built like the roster proof's
   (native_arcade_roster_proof.c:389-457): the fixture identity, tick
   rate, and bot difficulty; NativeMatchConfigV1_InitArcadeOneCab and
   Digest1PV1; track, laps, and the human character from the select
   outcome; bots exactly LOAD_Robots1P for that human. It must pass
   NativeArcadeBotRules_ValidateConfigV1. masterSeed comes from the
   select's existing derivation (DeriveSeed with humanCount 1: this
   cabinet's nonce alone). The human is CAB1_HUMAN in slot 0 on either
   seat.
7. SOLO-7 (default): Race.
   - The race launches locally through MainArcadeRaceSetup, using the one
     existing Arm/Launch call (the RL-9 Disarm rule is unchanged).
   - The local pad drives slot 0 on every tick. There is no lockstep
     session, bundle, digest exchange, or hold.
   - Pacing is the same fixed VBlank pacing as a linked race, and so are
     the end rules: END_OF_RACE, finish grace, race tick limit, local
     failure.
   - The race then goes to RESULTS.
8. SOLO-8 (default): Results.
   - Solo RESULTS has rows RACE AGAIN and LOBBY.
   - RACE AGAIN goes to solo SELECT with the cursors on the previous picks,
     as OD-3 (docs/MATCH_SELECT_MILESTONE.md:576-582), and a new masterSeed.
   - LOBBY goes to LOBBY and the handshake restarts on the TWO_CAB fixture.
   - The idle timeout (900 ticks) goes to LOBBY.
   - While `peerAwake` is latched the screen adds "OTHER CABINET IS READY".
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
    tree never strands a player on a solo screen that cannot race.
12. SOLO-12 (default): TWO_CAB is unchanged: every linked transition,
    timing, test, and live gate, and the linked LOBBY's text while solo is
    not offered.
13. SOLO-13 (default): Prompt text. While offered, the LOBBY shows
    "WAITING FOR OTHER CABINET" and "PRESS START TO RACE SOLO". Before the
    offer the lobby text stays as it is today
    (game/MAIN/MainArcadeLinkLayout.c:338, :362).
    - Every string uses only the glyphs the layout test accepts: A-Z,
      0-9, space, `.`, `:`, `,`, `-`, `'`
      (tests/main_arcade_link_layout_test.c:1516-1518).
      No new glyph is needed; "PRESS START" is already the attract line
      (MainArcadeLinkLayout.c:321).
    - The strings must fit the 400-wide panel (MainArcadeLinkLayout.c:14-17),
      which the same test checks (:1525-1531).

## 4. Slices

S1 and S2 land dark (SOLO-11): solo is reachable only in tests until S4
switches `soloAvailable` on. Order: S1, S2, S3, S4, S5.

### SOLO-S1 -- flow core (pure)

- Files: include/platform/native_arcade_flow.h, platform/native_arcade_flow.c.
- Changes:
  - a solo mode bit in the flow, and `soloOffered` behind an accessor;
  - SOLO_OFFER_DELAY as a tenth frozen default timing (90u) with its own
    counter;
  - `soloAvailable` in the observation's reserved byte;
  - new actions (for example BEGIN_SOLO_SELECT and START_SOLO_RACE);
  - the solo SELECT, SELECT_RESULT, RACING, and RESULTS transitions, in
    which no lobby status or link failure is read;
  - the solo RESULTS rows and the timeout to LOBBY.
- tests/native_arcade_flow_test.c cases: offer timing across restarts;
  READY beats CONFIRM, and BACK beats both; REJECTED never offers and
  restarts the count; with the gate off there is never an offer; solo
  ignores link observations; the RACE AGAIN and LOBBY rows; the timeout to
  LOBBY.
- Also update the isolation test's frozen defaults (ten).
- Scope: fast suite.

### SOLO-S2 -- adapter and host (pure)

- Files: platform/native_arcade_netplay.c, platform/native_arcade_link_host.c,
  their headers, and the smallest lobby/peer-link addition needed.
- Changes:
  - the listen-only link (SOLO-4);
  - a one-human select session on the ONE_CAB base (localHuman 0, the
    initial cursor from the CAB1_HUMAN slot);
  - the SOLO-6 config;
  - solo launch without agreement;
  - the race caller's config query (GetAgreedConfig answers in solo, or a
    solo query beside it);
  - `soloOffered` and `peerAwake` in the views (the netplay view has a
    reserved byte, include/platform/native_arcade_netplay.h:324).
- Tests in tests/native_arcade_netplay_test.c, which uses real loopback UDP
  sockets in its own port band (:31-35), not a fake transport:
  - a probe socket on the peer address receives nothing in solo;
  - a HELLO sent in solo sets `peerAwake` and changes no screen;
  - LOBBY after solo links with a peer;
  - the solo config passes ValidateConfigV1, and its bots equal
    ExpectedBots1P for all 8 characters.
- Update the isolation tests: the netplay include allow-list
  (tests/native_arcade_netplay_isolation_test.cmake:74) does not name the
  bot rules or the fixture today.
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
  - preview screens (for example `lobby-solo` and `results-solo`) for
    `--arcade-link-preview`, with their captures in the preview check.
- Tests: layout unit tests (glyph and panel checks).
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
- New one-process live test `arcade_solo_race`:
  - labels `live;live-link`;
  - a race tick cap (`--arcade-link-autopilot-race-ticks`);
  - the peer pointed at an unused loopback port, outside 7101/7102,
    7001/7002, and 48000-48600 (CMakeLists.txt:2054-2057);
  - its own checker.
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
- docs/HANDOFF.md state (not its Next work).
- This document's status.

## 5. Risks and open items

1. Listen-only needs a socket without a handshake. Today the socket exists
   only inside a peer link, whose Open sends a HELLO
   (native_lockstep_peer_link.h:158-174), and a WAITING lobby holds none.
   S2 picks the smallest change: a lobby or peer-link listen mode that
   decodes without replying, or a bare transport the adapter owns. Either
   way it edits the lobby or the peer link. The match-select milestone
   left native_lobby_state unedited (docs/MATCH_SELECT_MILESTONE.md:71-74)
   and relaxed GAME_LOOP_UI constraint 2 only for the peer-link aux channel
   (:63-65). This document records the same kind of relaxation for the
   listen mode.
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
   (SOLO-13) catch a bad string in the unit test, not on the cabinet.
5. `peerAwake` checks only that the HELLO is well formed and comes from a
   configured peer. A peer on a different build shows "OTHER CABINET IS
   READY" and then REJECTS when the player returns to the LOBBY: an
   operator fault, as today.
6. An abandoned solo cabinet ends on the LOBBY (SOLO-8 timeout), not the
   attract title, as a linked lobby does today (UX-5). A LOBBY idle
   timeout is out of scope.
7. A local race failure in solo shows LINK ERROR, the only failure end
   reason (RL-11). S3 may choose solo title text; the flow's end reasons
   do not change.
8. CROSS is also the G29 throttle, so pressing the throttle at the offer
   starts solo. Release-to-arm makes a throttle held across the screen
   change harmless.
9. No two-cabinet live test yet of "peer wakes during solo, then links
   from the LOBBY". That is a later live proof.
