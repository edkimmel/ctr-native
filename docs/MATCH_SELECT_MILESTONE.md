# Match-select milestone

Design-and-status record for a pre-race match-select phase on the arcade
link, on branch `arcade`: after MATCH FOUND each player picks a character,
the players vote on the track and the lap count, and the result becomes the
agreed NativeMatchConfigV1 the race launches on. Read AGENTS.md and
docs/HANDOFF.md first. This document follows the same pattern as
docs/GAME_LOOP_UI_MILESTONE.md: a prospective plan with a task list, updated
to record status as tasks land.

It expands the docs/HANDOFF.md "Next work" item 1 as it stood when the
milestone was queued (commit a4995975d):

> 1. Active: the match-select milestone (`docs/MATCH_SELECT_MILESTONE.md`, to be
>    written as its first task). It adds a pre-race phase between MATCH FOUND
>    and START_RACE:
>    - each player picks their own character;
>    - the players vote on the track and on the lap count;
>    - a disagreement resolves to a random pick drawn from the shared match
>      seed.
>    Opponent cursors are display-only; only the final choices must agree. The
>    result becomes the agreed `NativeMatchConfigV1` that Task 7 launches,
>    replacing the fixed per-build fixture (UX-8). The design stays general
>    enough for up to 4 players (step 8).

It adds no scope beyond that item except the 4-player sizing the owner
asked for: the selection state, the select message, and the resolution
rules are sized for up to 4 human slots. LAN discovery itself, and the 3-4
human roster and match config, are HANDOFF integration step 8 and are not
built here.

Real two-cabinet, physical-hardware validation (real wire, real switch, real
G29 input) is not part of this milestone; it stays gated behind step 6 (CAB1
G29/kiosk gate) and step 7 (two-cabinet fleet acceptance). Launching the
race on the agreed config is GAME_LOOP_UI Task 7 and stays gated on step 3.

## 1. Starting point

What exists and is reused (docs/GAME_LOOP_UI_MILESTONE.md sections 2.1-2.6,
docs/LOBBY_MILESTONE.md):

- The lobby/connect stack: native_udp_transport, native_lockstep_handshake
  (validate-and-reject exchange of a full NativeMatchConfigV1),
  native_lockstep_peer_link, native_lobby_state.
- The arcade-link layer: native_arcade_menu_input (PREV, NEXT, CONFIRM,
  BACK events, release-to-arm, rising edge), native_arcade_flow (pure screen
  state machine), native_arcade_netplay (host adapter, implicit rematch seed
  derivation), native_arcade_link_options (CLI options and fixture),
  native_arcade_link_host (game-facing singleton), and under game/MAIN the
  layout builder, policy, and live hook with its drawer.
- The preview capture checker (native_capture_check, the
  ctr_native_arcade_link_capture_check CLI,
  tools/arcade-link-preview-check.ps1) and the arcade_link_preview_render
  ctest.
- native_match_config (NativeMatchConfigV1, _Validate, _Digest),
  native_sha256, and the canonical codec (NativeCodecWriter /
  NativeCodecReader with the FNV-1a 64 digest), which the handshake and the
  lockstep bundle already use for their fixed little-endian records.

Modules this milestone edits:

- native_lockstep_peer_link: the aux channel only (section 2.4). This
  relaxes GAME_LOOP_UI constraint 2 for this one module and this one
  addition; the handshake and bundle routes are unchanged.
- native_arcade_flow, native_arcade_netplay, native_arcade_link_host,
  native_arcade_link_options, game/MAIN/MainArcadeLinkLayout,
  game/MAIN/MainArcadeLink (hook and drawer), main.c (select entropy), the
  capture checker, and the preview script.

Modules this milestone does not edit: the lockstep protocol, input window,
and session; the handshake; native_lobby_state; native_udp_transport;
native_match_config; native_lockstep_rematch; native_lockstep_match_outcome;
native_lockstep_match_roster.

Every duration below is in game-loop ticks at the retail 30 Hz loop
(GAME_LOOP_UI section 1).

### Retail facts this design rests on

Characters:

- `enum Characters` is at include/namespace_Vehicle.h:37-55. Characters 0..7
  (CRASH_BANDICOOT, NEO_CORTEX, TINY_TIGER, COCO_BANDICOOT, N_GIN, DINGODILE,
  POLAR, PURA) are always available. Characters 8..14 are unlockable: the
  unlock bits are `enum CharacterUnlock` at include/namespace_Main.h:230-240
  and the check is in game/230/MM_Characters.c:173-179. NITROS_OXIDE (15) is
  not selectable.
- Retail forbids two players on one character. MM_Characters_boolIsInvalid
  (game/230/MM_Characters.c:188-212) reports a character another player
  already holds; the navigation code skips such a character
  (game/230/MM_Characters.c:1139-1144); and MM_Characters_PreventOverlap
  (game/230/MM_Characters.c:649-713) repairs any duplicate: a later player
  holding an earlier player's character gets the first free default
  character, in ascending ID order.
- Race-side state keyed by character ID would be shared by two drivers on
  one character: the voice-line state `sdata->timeSet1[characterID]`
  (game/HOWL/HOWL_Voiceline.c:161) and `sdata->timeSet2[characterID]`
  (game/HOWL/HOWL_Voiceline.c:179), declared at include/regionsEXE.h:4008-4011,
  and the last-voiced-character compare (game/HOWL/HOWL_Voiceline.c:191).
- The 2P robot sets exclude both humans' characters (LOAD_Robots2P,
  game/LOAD/LOAD_Assets.c:21-59). Driver models load per driver
  (game/LOAD/LOAD_Assets.c:176-184), so models alone would not collide.

Laps and tracks:

- Retail lap rows are 3, 5, 7 (`lapCountByRow`, game/230/D230.c:621),
  written to numLaps at game/230/MM_TrackSelect.c:773.
- `arcadeTracks` (game/230/D230.c:561-599) has 18 rows of {levelID, ...,
  unlock, ...}. MM_TrackSelect_boolTrackOpen
  (game/230/MM_TrackSelect.c:385-400) opens a row whose unlock field is
  MM_TRACK_UNLOCK_ALWAYS (-1), or MM_TRACK_UNLOCK_1P_ONLY (-2) with one
  player, or an unlock bit the save holds (the constants are at
  include/ovr_230.h:229-230). OXIDE_STATION's row has unlock 0xFFFE (1P
  only), so it is never offered in multiplayer; TURBO_TRACK's row needs
  unlock bit 1. Level IDs are at include/namespace_Level.h:4-23.
- The base multiplayer set is therefore 16 tracks. In retail menu order,
  with level IDs: CRASH_COVE 3, ROO_TUBES 6, TIGER_TEMPLE 4, COCO_PARK 14,
  MYSTERY_CAVES 9, BLIZZARD_BLUFF 2, SEWER_SPEEDWAY 8, DINGO_CANYON 0,
  PAPU_PYRAMID 5, DRAGON_MINES 1, POLAR_PASS 12, CORTEX_CASTLE 10,
  TINY_ARENA 15, HOT_AIR_SKYWAY 7, N_GIN_LABS 11, SLIDE_COLISEUM 16.

2P bots:

- LOAD_Robots2P takes the first of the seven `characterIDs_2P_AIs` sets
  (game/zGlobal_DATA.c:3558-3580; LOAD_2P_AI_SET_RACER_COUNT 4 and
  LOAD_2P_AI_SET_COUNT 7 at include/namespace_Load.h:117-118) that contains
  neither human character, writes it to characterIDs[2..5], and loads
  BI_2PARCADEPACK + setIndex. It is called from the 2P LOD path at
  game/LOAD/LOAD_Assets.c:184. The sets, in order: {6,4,2,3}, {0,6,3,5},
  {0,6,1,2}, {0,6,4,7}, {1,2,3,5}, {4,7,3,5}, {4,7,1,2}.

Where retail stores the race choice: gGT->levelID
(include/namespace_Main.h:426), gGT->numLaps (include/namespace_Main.h:574),
and data.characterIDs[8] (include/regionsEXE.h:2241). GAME_LOOP_UI Task 7
maps the agreed config onto these; this milestone does not.

## 2. Decided design

Three new pure modules (2.1-2.3), one edit to the peer link (2.4), and
edits to the existing arcade-link layers (2.5-2.8). No new module lives
under game/; game code sees selection only through the host view.

### 2.1 Selection rules: native_match_select_rules (pure)

platform/native_match_select_rules.c and
include/platform/native_match_select_rules.h, library
ctr_native_match_select_rules (links ctr_native_match_config and
ctr_native_sha256 only).

- NATIVE_MATCH_SELECT_MAX_HUMANS is 4. Human index h is the cabinet role
  minus 1 (CAB1 -> 0, CAB2 -> 1; indices 2 and 3 are reserved for step 8).
  humanCount is 1..4 in this module and on the wire. NativeMatchConfigV1
  has only two human roles, so building a config supports humanCount 2
  (profile ARCADE_TWO_CAB) only, until step 8.
- Tables: the base characters 0..7; the 16 base tracks in retail menu order
  (section 1); the lap options {3, 5, 7}; the seven retail 2P AI sets,
  mirrored, with an isolation test that checks the mirror against
  game/zGlobal_DATA.c.
- Choice per human: characterID, trackID (a vote), lapCount (a vote), and a
  nonce (uint64).
- Match seed: the first little-endian 8-byte word, trying [0, 8), [8, 16),
  [16, 24), [24, 32) in order, of

  SHA-256("CTRN match select seed v1" || base config SHA-256 digest
  (32 bytes) || humanCount (1 byte) || nonce[0..humanCount-1] (8 bytes LE
  each))

  that is nonzero and differs from base.masterSeed. It becomes the resolved
  config's masterSeed.
- Draw: the first LE 8-byte word of SHA-256("CTRN match select draw v1" ||
  domain (1 byte: 1 track, 2 laps) || matchSeed (8 bytes LE)), modulo the
  candidate count. Domain separation keeps the track and lap draws
  independent. The modulo bias is below 2^-60; determinism is exact.
- Votes: plurality per item. The options tied for the most votes, in table
  order, are the candidates; one candidate wins outright, two or more are
  drawn with the item's domain. Two players who disagree therefore get a
  50/50 draw between their two votes.
- Characters are unique (OD-2): resolution walks the humans in role order
  (CAB1 first); a human whose locked character a lower-role human already
  holds is reassigned the lowest-ID base character no human holds (the
  MM_Characters_PreventOverlap rule).
- Bots, two humans: the first retail 2P AI set that contains neither human
  character goes to bot slots 2..5 in set order (the LOAD_Robots2P rule).
  Every ordered pair of distinct base characters has such a set; the unit
  test checks all of them exhaustively. For 3-4 humans there is no retail
  set; the provisional rule is ascending unpicked base characters, to be
  revisited at step 8.
- Outcome: masterSeed, trackID, lapCount, per-human characterID, per-bot
  characterID, and the flags trackDrawn, lapsDrawn, and
  characterReassignedMask.
- Outcome digest: SHA-256("CTRN match select outcome v1" || base config
  digest || a fixed LE encoding of the outcome). Its first 8 bytes are the
  resolvedDigest that peers compare.
- Build: copy the base config; set trackID, lapCount, masterSeed, the human
  slots' characterID by role, and the bot slots' characterID in slot order.
  Everything else (profile, tick rate, identity, difficulty, bot rules,
  gameMode and rules) is unchanged. The result must pass
  NativeMatchConfigV1_Validate.

### 2.2 Select wire message: native_match_select_message (pure codec)

A 64-byte fixed record, little-endian through NativeCodecWriter and
NativeCodecReader, with an FNV-1a 64 trailer (the handshake and bundle
idiom):

```
offset size field
0      4    magic 0x31534d4e ("NMS1")
4      2    messageVersion 1
6      2    encodedSize 64
8      1    senderHuman (0..humanCount-1)
9      1    humanCount (1..4)
10     1    phase: 0 PICKING, 1 RESOLVED
11     1    lockMask: bit0 character, bit1 track, bit2 laps;
            must equal (1 << currentItem) - 1
12     4    sequence, >= 1, strictly increasing per sender per select
16     8    baseDigest: bytes 0..7 of the base config SHA-256 digest
24     8    nonce
32     1    characterID (cursor, or pick once locked), a base character
33     1    trackID (cursor or vote), a base track
34     1    lapCount (cursor or vote), 3, 5, or 7
35     1    currentItem: 0 character, 1 track, 2 laps, 3 done
36     4    reserved, zero
40     8    resolvedDigest (zero unless phase is RESOLVED)
48     8    reserved, zero
56     8    FNV-1a 64 over bytes 0..55
```

RESOLVED requires currentItem 3. Decode reports a distinct, append-only
fault cause per check, in wire order.

### 2.3 Select session: native_match_select_session (pure)

One cabinet's selection state: the base config, humanCount, localHuman, the
nonce, the local cursors, locks, and current item, a per-item countdown
(600 ticks, OD-1), the latest accepted message per peer, and a latch-once
status.

- Local input: PREV and NEXT move the current item's cursor, with wrap, in
  table order; CONFIRM locks it and advances to the next item, but is
  refused on a character a peer has locked (the
  MM_Characters_boolIsInvalid rule); BACK is ignored (SEL-7).
- Expiry: the cursor value is locked (for a character a peer has locked,
  the next free character in table order from the cursor) and the item
  advances.
- Compose: the current state with sequence + 1, sent every tick. This is
  state replication: loss only delays, and a lost cursor update just makes
  the opponent's cursor jump.
- Accept drops, and counts, a malformed record, a foreign baseDigest (a
  previous or later select), its own senderHuman, a sender >= humanCount,
  and a sequence not above the last accepted from that sender (duplicate or
  reordered). It latches FAILED for a humanCount mismatch on the same base,
  a nonce change, a locked item that unlocks or changes value, a
  resolvedDigest that differs from ours once both sides are resolved, and
  peer silence: no accepted message from a peer for 90 ticks while not
  CONFIRMED (SEL-9).
- Status: PICKING; WAITING (local done, a peer is not); RESOLVED (every
  human locked, resolved locally, awaiting every peer's RESOLVED);
  CONFIRMED (every peer RESOLVED with an equal resolvedDigest); FAILED.
  CONFIRMED and FAILED are terminal.

Reliability: every message carries the sender's full state, so a lock
arrives with any later message. A cabinet leaves select only after
CONFIRMED, that is, after every peer has proved by its RESOLVED digest that
it holds every lock. After CONFIRMED it keeps sending for the 60-tick
result hold (2.5), so a peer still RESOLVED receives the confirming message
with overwhelming probability; if every copy is lost, that peer fails
cleanly by peer silence instead of hanging. The relink handshake (2.5)
re-checks the full 32-byte config digest.

### 2.4 Peer-link aux channel (edit of native_lockstep_peer_link)

The peer link routes datagrams by exact size (284 bytes handshake, 128
bytes bundle, anything else dropped). It gains a third, generic route: a
datagram of exactly NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES (64) from the peer
address while RUNNING goes to a bounded aux inbox (16 entries; when full,
the oldest is discarded and counted); while not RUNNING it is dropped.
New calls: SendAux (RUNNING only, exactly 64 bytes) and TakeAux (pops the
oldest). Open and Close reset the inbox. The aux route never touches the
handshake or the session. The peer link names no select type; the select
codec static-asserts that its width equals the aux width. The handshake,
bundle, and NativeMatchConfigV1 formats are unchanged.

### 2.5 Flow: SELECT and SELECT_RESULT (native_arcade_flow)

Appended screens SELECT (7) and SELECT_RESULT (8); appended actions
BEGIN_SELECT (7) and RELINK (8). The observation gains selectStatus
(PENDING 0, CONFIRMED 1, FAILED 2) in a former reserved byte; the timings
gain selectResultHoldTicks (60) and launchTimeoutTicks (300). Existing enum
values are unchanged.

- MATCH_FOUND: after the hold, moves to SELECT and returns BEGIN_SELECT
  (instead of moving to RACING and returning START_RACE).
- SELECT: menu events go to the select session, not the flow (BACK is
  ignored). A lobby status other than READY, or selectStatus FAILED, moves
  to RESULTS with LINK_ERROR (the existing LINK ERROR path); CONFIRMED
  moves to SELECT_RESULT.
- SELECT_RESULT: shows the resolved match for selectResultHoldTicks while
  select messages keep flowing (the linger), then returns RELINK: the
  adapter closes the link and begins the lobby on the resolved config. Then
  READY moves to RACING and returns START_RACE; REJECTED, or
  launchTimeoutTicks without READY, moves to RESULTS with LINK_ERROR;
  WAITING or LOST returns RESTART_LOBBY after lobbyRetryPauseTicks. BACK is
  ignored.
- REMATCH is unchanged up to READY -> MATCH_FOUND, which now leads to
  SELECT (OD-3).

### 2.6 Adapter: native_arcade_netplay

The adapter owns one select session.

- BEGIN_SELECT starts it on the agreed lobby config (the base), with
  initial cursors from the base config's own slot character, track, and
  laps (the fixture on a first match; the previous picks on a rematch,
  OD-3), and a per-select nonce: the first LE 8 bytes of
  SHA-256("CTRN match select nonce v1" || selectEntropy (8 bytes LE) ||
  localRole (1 byte) || selectSerial (4 bytes LE)), where selectSerial
  counts the selects this adapter has begun.
- selectEntropy is host-local: main.c reads the wall clock and a
  performance counter once at startup; it is 0 in tests and previews. It
  reaches identity only through the exchanged nonces and the agreed
  masterSeed.
- Each tick in SELECT, and in SELECT_RESULT before RELINK: drain the aux
  inbox into the session, feed the menu event (SELECT only), tick the
  countdown, map the session status into the observation, run the flow,
  then compose and SendAux.
- RELINK builds the resolved config, makes it the current config, closes
  the lobby, and begins it on that config; the relink handshake is the full
  byte-identity check. A build failure blocks like a failed rematch and
  ends in LINK_ERROR at the launch timeout.
- START_RACE arms the race on the resolved config.
- NativeArcadeNetplay_AgreedConfig is non-NULL on RACING and RESULTS only
  (it was also non-NULL on MATCH_FOUND, when the lobby config was the race
  config).

### 2.7 Host, options, preview, logging

- The host view gains flat select fields: the current item, the countdown,
  humanCount, localHuman, per-human cursor, locks, current item, and seen
  flag, and the resolved outcome with its draw and reassignment flags. A
  GetAgreedMatch call returns track, laps, seed, and slot characters for
  logging.
- The options gain selectEntropy (not parsed from the command line; set by
  main.c).
- New previews: `select-character`, `select-track`, `select-laps`,
  `select-wait`, `select-result`. In each, the opponent cursor moves on a
  fixed tick schedule, so captures are deterministic.
- START_RACE still returns to the title (Task 7 is gated), after the hook
  logs the resolved match with Platform_Log.

### 2.8 Screens (MainArcadeLinkLayout and drawer)

Same style as the accepted screens: the retail panel, DecalFont text, the
retail row highlight for the local cursor, a "P1"/"P2" text marker per
human cursor (opponent markers in a distinct, high-contrast retail colour),
locked items marked, the countdown in whole seconds, and a footer with the
opponent's progress.

- Character: 8 names, 2 columns x 4 rows.
- Track: 16 names, 2 columns x 8 rows, FONT_SMALL.
- Laps: 3 rows.
- Wait: your picks and the opponent's progress.
- Result: track, laps, each human's character, the bots, "RANDOM" beside a
  drawn value, and a note on a reassigned character.

The draw-list capacity grows as needed.

Reusing the retail character and track select was evaluated and declined.
MM_Characters and MM_TrackSelect (overlay 230) are local multi-pad menus
that write data.characterIDs and the gGT fields directly. Showing a remote
cursor would mean injecting a remote player into their per-player state or
driving them under lockstep, which this design avoids. Track and lap select
are single-player (there is no vote concept). And they need the retail menu
hierarchy that the arcade layer deliberately makes unreachable (GAME_LOOP_UI
section 2.5).

### 2.9 Protocol compatibility

The NativeMatchConfigV1 layout, protocolVersion (1), the handshake, and the
bundle are unchanged; only field values of the agreed config change. The
64-byte select datagram has its own magic and version. Mixed builds never
reach it: the handshake rejects a differing build identity, and an older
build would just drop 64-byte datagrams.

## 3. Owner decisions

These are decided by the owner, not defaults.

1. OD-1, countdown (DECIDED): 20 s per selection screen (character, track
   vote, lap vote), 600 ticks at the 30 Hz loop, with an automatic pick or
   vote at expiry.
2. OD-2, duplicate characters (DECIDED): the owner allowed duplicates only
   if the retail race code is safe with them, and otherwise unique
   characters with a deterministic tie-break by cabinet role. Finding: it
   is not safe. Retail forbids and repairs duplicates, the voice-line state
   is keyed by character, and the 2P robot sets assume two distinct humans
   (section 1). Characters are therefore unique: resolution walks the humans
   in role order (CAB1 first), and a human whose locked character a
   lower-role human already holds is reassigned the lowest-ID base
   character no human holds (the MM_Characters_PreventOverlap rule). The
   select screen also refuses CONFIRM on a character a peer is already
   known to have locked (the MM_Characters_boolIsInvalid rule), so
   reassignment fires only on simultaneous locks or at expiry. Bots still
   fill deterministically (2.1).
3. OD-3, rematch goes back through select (DECIDED): REMATCH -> handshake on
   a new derived seed -> MATCH FOUND -> select again, never keeping the
   previous picks. Each cursor starts on that player's previous pick,
   because the rematch base config carries them.

## 4. UX defaults for operator review

Each is a default chosen for a two-cabinet, wheel-only kiosk, and is flagged
for the operator to confirm or change after seeing the built flow.

1. SEL-1: A vote tie resolves by a seeded draw among the tied options only,
   not among all tracks, so a player's vote always counts.
2. SEL-2: The lap options are 3, 5, 7, as in retail. The cursor starts on 3
   (the fixture) or on the previous pick.
3. SEL-3: The track list is the 16 base multiplayer tracks: no Oxide Station
   (retail offers it in 1P only) and no Turbo Track (it needs a save
   unlock, which is host-local state the two cabinets do not agree on).
4. SEL-4: Bots follow the retail 2P AI set rule, so the chosen set's 2P
   arcade pack exists on the disc and bots never duplicate a human.
5. SEL-5: The order is character -> track -> laps. Each cabinet advances on
   its own; a waiting screen shows the opponent's progress; opponent
   cursors are display-only and live. Nothing waits on the slower player
   except the final result.
6. SEL-6: The automatic pick at expiry is the cursor (the next free
   character if the cursor's character is taken), so an idle player gets
   what they were looking at.
7. SEL-7: BACK is ignored in SELECT and SELECT_RESULT. The countdown bounds
   the phase, and leaving mid-select would strand the other cabinet.
8. SEL-8: The result screen shows for 60 ticks (2 s); "RANDOM" marks a
   drawn value, so a player can see why the track is not their vote.
9. SEL-9: Select peer silence of 90 ticks (3 s, as UX-9) -> LINK ERROR; the
   relink (launch) timeout is 300 ticks (10 s) -> LINK ERROR. Both bound
   every wait.
10. SEL-10: Navigation: PREV and NEXT step through the list with wrap and
    no auto-repeat (the UX-4 inputs). The track list needs up to 8 presses
    to reach any track, which is acceptable within 20 s.
11. SEL-11: Vote display: live opponent markers and marked locked items; the
    result screen shows each human's pick and the outcome.
12. SEL-12: The first match's race seed now varies per match (it is derived
    from both cabinets' nonces); the fixture seed "CTRNARC1" only seeds the
    lobby base config. This supersedes UX-8's fixed first seed.
13. SEL-13: A cabinet can choose its nonce after seeing its peer's (there is
    no commit-reveal). This is acceptable for a trusted kiosk fleet.

## 5. Constraints

1. The topology lease is untouched: no acquire, activate, capture, or
   publish; no lease owner in checkpoints, replay, or canonical state; no
   retire hook on LOAD_Hub_ReadFile.
2. No edits to the lockstep protocol, window, session, failure-handling,
   handshake, lobby-state, UDP-transport, match-config, or rematch modules.
   The peer link gains only the aux channel (2.4).
3. The existing lockstep and failure-handling isolation rules on game/ stay
   exactly as strict as they are. New: no `NativeMatchSelect` token under
   game/; game code talks only to the host API.
4. No dynamic allocation in any new module; portable C17 with extensions
   off on every new target.
5. Every new seam gets a unit test; every structural rule gets an isolation
   test. New game .c files go in game/game_unity.h.
6. Default behaviour (no `--arcade-link` option) is unchanged; the full
   replay and network suites must stay green.
7. A task is done only when `cmake --build build-msvc-x86 --config Debug`
   succeeds and the full `ctest --test-dir build-msvc-x86 -C Debug` suite
   passes. Commit each green task on `arcade` only; no push.

## 6. Task list

Baseline before this milestone: 111 tests, 100% passing (commit a4995975d).

### MS-1 -- this document

Status: done (this document). No review required.

### MS-2 -- selection rules (native_match_select_rules)

Status: open. Header, implementation, unit test (including every ordered
pair of distinct base characters against the bot rule), and an isolation
test (pure; the 2P AI set mirror checked against game/zGlobal_DATA.c).
Review required: it decides the agreed config's contents.

### MS-3 -- select message codec (native_match_select_message)

Status: open. Header, implementation, unit test (every field, every fault
cause in wire order), and an isolation test. Review required: it is a wire
format.

### MS-4 -- select session (native_match_select_session)

Status: open. Header, implementation, unit test, a fault-injection test over
native_virtual_datagram (loss, duplication, reordering, delay, corruption,
lock change, nonce change, digest mismatch, silence; 2 humans, and 4 humans
over pairwise harnesses), and an isolation test. Review required.

### MS-5 -- peer-link aux channel

Status: open. The aux route, SendAux, and TakeAux (2.4), loopback tests, and
an isolation test (the peer link names no select token). Review required:
it edits a lockstep module and the wire routing.

### MS-6 -- flow SELECT and SELECT_RESULT

Status: open. The appended screens, actions, observation field, and timings
(2.5), with unit tests of every new transition. Reviewed with MS-7.

### MS-7 -- netplay integration

Status: open. The adapter's select session, nonce, relink, and launch
timeout (2.6), with loopback tests: a full select to READY on the resolved
config, a disagreement draw identical on both sides, an idle auto-pick,
simultaneous locks on the same character, silence, and a rematch through
select with the previous picks pre-focused and a new seed. Review
required.

### MS-8 -- host, preview, options, entropy, logging

Status: open. The host view select fields and GetAgreedMatch, the five
previews, the options' selectEntropy, main.c's entropy read, and the hook's
Platform_Log of the resolved match (2.7). Review required: it touches the
game loop.

### MS-9 -- select screens

Status: open. Layout and drawer for the five select screens (2.8), with
layout unit tests and a visual check. No review required unless the visual
check finds a problem.

### MS-10 -- preview captures

Status: open. The capture checker, the preview script, and the
arcade_link_preview_render ctest cover the five new previews;
alpha-stripped review PNGs checked. No review required.

### MS-11 -- docs close-out

Status: open. This document's statuses, docs/HANDOFF.md, and the
docs/GAME_LOOP_UI_MILESTONE.md UX-8 and section 2.2 cross-references. No
review required.

## 7. Risks and open questions

1. GAME_LOOP_UI Task 7 must map the agreed config onto gGT->levelID,
   gGT->numLaps, and data.characterIDs, and load the 2P AI pack of the
   chosen set. The bot rule mirrors LOAD_Robots2P, so that pack exists.
2. NativeMatchConfigV1 supports only two humans; 3-4 humans (step 8) need a
   config change. The rules, message, and session are already sized for 4.
3. If every select message during the 60-tick linger is lost, the peer
   fails with LINK ERROR by silence rather than hanging.
4. The wall-clock entropy is host-local. With both cabinets' entropy equal
   (for example 0), first matches differ only by base config.
5. Everything is loopback- or virtual-tested; the real two-cabinet and G29
   run remains the step 6/7 gate.
6. The capture checker cannot tell select screens with a similar layout
   apart beyond their band structure (like GAME_LOOP_UI risk 14).
