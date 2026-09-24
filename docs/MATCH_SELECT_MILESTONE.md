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
race on the agreed config is GAME_LOOP_UI Task 7, now done
(docs/RACE_LAUNCH_MILESTONE.md).

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
  NativeMatchConfigV1_Validate. NativeMatchSelect_BuildConfig also requires
  humanCount to equal the base's number of human-role slots (so only 2
  builds today) and rejects any outcome Resolve cannot produce: tracks,
  laps, or characters outside the tables, duplicate or stray characters, a
  stale or zero seed, reassignment bit 0, and, for two humans and four bots,
  anything but the first qualifying retail 2P AI set in set order.

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
fault cause per check (enum NativeMatchSelectMessageFaultCause). The checks
run in a fixed, documented order, not wire order: size (BAD_SIZE), magic,
version, encoded size, the trailer digest (BAD_DIGEST), then the shape
checks of NativeMatchSelectMessageV1_ShapeCause: reserved bytes, humanCount,
senderHuman < humanCount (BAD_SENDER), phase, lockMask bits, sequence,
character, track, laps, currentItem, lockMask == (1 << currentItem) - 1
(cause 17, BAD_ITEM_LOCK_MISMATCH), a zero resolvedDigest while PICKING,
and currentItem 3 while RESOLVED (cause 18, BAD_RESOLVED_ITEM). Causes 17
and 18 were appended, so the check order is not the numeric order. The
reader's bytes are touched only after the reads have validated them, so a
hand-built reader with NULL data is a size fault, not a crash.

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
- Accept checks, in order: a terminal session ignores the record; a
  malformed record (any codec fault) is dropped and counted; a baseDigest
  not ours is dropped and counted (DROPPED_FOREIGN); a humanCount not ours
  latches FAILED/HUMAN_COUNT_MISMATCH; its own senderHuman is dropped and
  counted; a sequence not above the last accepted from that sender
  (duplicate or reordered) is dropped and counted (DROPPED_STALE); a nonce
  change latches NONCE_CHANGED; a locked item that unlocks or changes
  value, or a RESOLVED sender whose digest changes, latches LOCK_CHANGED.
  There is no separate drop for a sender >= humanCount: with the record's
  own humanCount it is a codec BAD_SENDER (a malformed drop), and against
  ours it is a HUMAN_COUNT_MISMATCH failure.
- The session also latches FAILED for a resolvedDigest that differs from
  ours once both sides are resolved (DIGEST_MISMATCH), a Resolve failure
  (RESOLVE_FAILED), and peer silence: no accepted message from a peer for
  90 ticks while not CONFIRMED (PEER_SILENT, SEL-9).
- The foreign-baseDigest filter separates selects on different bases only.
  It does not separate two selects on the same base, and every first match
  uses the fixture base. A stale record from an earlier select on the same
  base fails safe, never a wrong agreement: NONCE_CHANGED when a fresh
  record follows it, DIGEST_MISMATCH when the local side resolves against
  it first, or PEER_SILENT when its sequence is above anything the fresh
  peer sends in time (every fresh record is then dropped as stale); a stale
  record below the fresh sequence is simply dropped. The fault test pins
  each case. Three mitigations keep such records from arriving at all: the
  peer link resets its aux inbox on Open and Close, it drops aux datagrams
  while not RUNNING, and the adapter discards the inbox at BEGIN_SELECT.
- Accessors: NativeMatchSelectSession_Status, _Fault, _CurrentItem,
  _TicksLeft, _Human, _CharacterLockedByPeer, _PeerLockedCharacterMask,
  _Base, _HumanCount, _LocalHuman, _Outcome, _ResolvedDigest, and the four
  drop counters (_DroppedMalformed, _DroppedForeign, _DroppedSelf,
  _DroppedStale). The adapter reads the session only through them.
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
bytes bundle, anything else dropped). It has a third, generic route: a
datagram of exactly NATIVE_LOCKSTEP_PEER_LINK_AUX_BYTES (64) from the peer
address while RUNNING is copied, opaque, to a bounded aux inbox of
NATIVE_LOCKSTEP_PEER_LINK_AUX_CAPACITY (16) entries. The inbox is
newest-wins: when it is full, Poll discards the oldest entry to make room
and counts it. While HANDSHAKING a 64-byte datagram is dropped uncounted;
once the link is terminal Poll receives nothing, so the inbox keeps what
it held.

- NativeLockstepPeerLink_SendAux: RUNNING only, exactly 64 bytes; a failed
  send is lossy and changes no state.
- NativeLockstepPeerLink_TakeAux: pops the oldest entry; works in every
  mode.
- NativeLockstepPeerLink_AuxCount: entries waiting.
- NativeLockstepPeerLink_DroppedAuxCount: overflow discards since the last
  successful Open or Close.

A successful Open and every Close empty the inbox and zero the overflow
count. The aux route never touches the handshake, the session, or the link
mode, and it passes the same sender-address filter as the other routes.
The peer link names no select type, and an isolation test keeps select
tokens out of it. The codec may not name the peer link, so the static
assert that the select width equals the aux width lives in
platform/native_arcade_netplay.c, the one module that names both. The
handshake, bundle, and NativeMatchConfigV1 formats are unchanged.

### 2.5 Flow: SELECT and SELECT_RESULT (native_arcade_flow)

Appended screens SELECT (7) and SELECT_RESULT (8); appended actions
BEGIN_SELECT (7) and RELINK (8). The observation gains selectStatus
(PENDING 0, CONFIRMED 1, FAILED 2) in a former reserved byte; the timings
gain selectResultHoldTicks (60) and launchTimeoutTicks (300). Existing enum
values are unchanged.

- MATCH_FOUND: after the hold, moves to SELECT and returns BEGIN_SELECT
  (instead of moving to RACING and returning START_RACE).
- SELECT: menu events go to the select session, not the flow (BACK is
  ignored). Checked in order: a lobby status other than READY, then
  selectStatus FAILED, each move to RESULTS with LINK_ERROR and return
  CLOSE_LINK; CONFIRMED moves to SELECT_RESULT.
- SELECT_RESULT: phase 1 shows the resolved match for
  selectResultHoldTicks, ignoring the lobby status, while select messages
  keep flowing (the linger), then returns RELINK: the adapter closes the
  link and begins the lobby on the resolved config. Phase 2 starts on the
  tick after RELINK, because a READY still seen on the RELINK tick belongs
  to the old link. Then READY moves to RACING and returns START_RACE;
  REJECTED, or launchTimeoutTicks without READY, moves to RESULTS with
  LINK_ERROR and returns CLOSE_LINK; WAITING or LOST returns RESTART_LOBBY
  after lobbyRetryPauseTicks; CONNECTING stays. BACK is ignored.
- Every pre-race LINK_ERROR (from SELECT or SELECT_RESULT) returns
  CLOSE_LINK, so a relink handshake the peer completes later cannot reach
  READY in the background behind the results screen and replace the
  config a rematch derives from (2.6). RACING -> RESULTS still returns
  NONE: the link stays open so the race driver can read the latched
  session report.
- REMATCH is unchanged up to READY -> MATCH_FOUND, which leads to SELECT
  (OD-3). MATCH_FOUND never starts a race.

### 2.6 Adapter: native_arcade_netplay

The adapter owns one select session.

- BEGIN_SELECT discards every datagram waiting in the aux inbox, then
  starts the session on the agreed lobby config (the base), with humanCount
  the base's number of human-role slots, the local human localRole - 1,
  initial cursors from the base config's own slot character, track, and
  laps (the fixture on a first match; each replaced by the first table
  entry if it is not a table value), and a per-select nonce
  (NativeArcadeNetplay_DeriveSelectNonce): the first LE 8 bytes of
  SHA-256("CTRN match select nonce v1" || selectEntropy (8 bytes LE) ||
  localRole (1 byte) || selectSerial (4 bytes LE)), where selectSerial
  counts the selects this adapter has begun since Init. If the session cannot start, the flow is told FAILED and
  shows LINK ERROR.
- Rematch cursors (OD-3). The rematch base derives from lastReadyConfig,
  so after a finished race it carries the previous picks and each cursor
  starts on them; the netplay loopback test
  TestSelectRematchThroughSelect proves this. It is reachable live since
  Task 7: START_RACE no longer calls NativeArcadeLinkHost_AbortToTitle
  (docs/RACE_LAUNCH_MILESTONE.md RL-S8b), so the adapter keeps
  lastReadyConfig across the race, taken in ArmRace on the START_RACE of
  a relink lobby (RL-6), and a rematch after a finished race starts its
  cursors on the previous picks. A rematch after a pre-race LINK ERROR
  still starts from the select base.
- selectEntropy is host-local and never parsed from argv. main.c reads the
  wall clock and SDL's performance counter once, only with --arcade-link
  ((time << 32) XOR counter); default and preview runs leave it 0. The host
  mixes a process-local epoch into it on every LINK Configure and every
  AbortToTitle, before it initializes the adapter:
  NativeArcadeLinkHost_MixSelectEntropy = entropy XOR (epoch *
  0x9E3779B97F4A7C15). AbortToTitle re-runs the adapter's Init (restarting
  selectSerial), so without the epoch the first-select nonces would repeat
  across it; with it they vary. Until Task 7 that happened after every
  START_RACE; since RL-S8b no game code calls AbortToTitle, so one
  adapter Init lasts the whole link run and selectSerial keeps counting
  across races. Previews never
  derive a nonce. The entropy reaches identity only through the exchanged
  nonces and the agreed masterSeed.
- Each tick in SELECT, and in SELECT_RESULT before RELINK: drain the aux
  inbox into the session, feed the menu event (SELECT only), tick the
  countdown, map the session status into the observation, run the flow,
  then compose and SendAux.
- RELINK closes the lobby and builds the resolved config with
  NativeMatchSelect_BuildConfig on the session's own base
  (NativeMatchSelectSession_Base, the config its exchanged base digest
  covers) and outcome. On success it becomes the current config and a new
  lobby is begun on it; the relink handshake is the full byte-identity
  check. A failed build begins nothing and blocks every restart, so the
  flow ends in LINK_ERROR at the launch timeout and never races on the
  base.
- START_RACE arms the race on the resolved config.
- BEGIN_REMATCH derives from lastReadyConfig, the proposal of the most
  recent lobby that reached READY. The handshake guarantees both sides hold
  a READY proposal byte-identically: after a finished race it is the
  resolved config; after a pre-race failure (a select failure, a failed
  build, or a launch timeout) it is the select base both held at
  MATCH_FOUND, so both sides rematch from the base, provided neither
  side's relink handshake reached READY. A cabinet whose relink reached
  READY stores the resolved config as lastReadyConfig while a peer that
  timed out keeps the base, so their rematch proposals differ (risk 7).
  Today that is unreachable: READY leads to START_RACE, which aborts to
  the title and re-inits the adapter.
- NativeArcadeNetplay_AgreedConfig is non-NULL on RACING, and on RESULTS
  only after START_RACE armed a race on it. A RESULTS screen reached
  without a race reads NULL.

### 2.7 Host, options, preview, logging

- The adapter view carries a flat select sub-view, copied field for field
  into the host view (NativeArcadeLinkHostView.select, struct
  NativeArcadeLinkHostSelectView): active, humanCount, localHuman, the
  local current item, ticksLeft (the countdown), status, and the resolved
  outcome (resolved, trackID, lapCount, trackDrawn, lapsDrawn,
  characterReassignedMask, botCount, humanCharacter, botCharacter), the
  peerLockedCharacterMask, and per human
  (NativeArcadeLinkHostSelectHumanView) present, characterID, trackID,
  lapCount, lockMask, and currentItem. The
  view is active on SELECT and SELECT_RESULT while a select session exists
  and all zero otherwise. The host static-asserts every field offset
  against the adapter's view.
- Host API additions: NativeArcadeLinkHost_GetAgreedMatch (track, laps,
  seed, and slot roles and characters of the agreed race config, only when
  NativeArcadeNetplay_AgreedConfig is non-NULL);
  NativeArcadeLinkHost_MixSelectEntropy (2.6); and value names game code
  can use without naming the select or match-config modules:
  NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_*, _SELECT_LOCK_*, _SELECT_STATUS_*,
  _ROLE_* (INACTIVE, CAB1, CAB2, BOT), and the view capacities, each
  static-asserted against the value it mirrors.
  include/platform/native_arcade_link_host_internal.h is a test-only
  read-back (NativeArcadeLinkHost_InternalSelectEntropy) that no game
  source may include.
- The options gain selectEntropy (never parsed from the command line; set
  by main.c).
- New previews: `select-character`, `select-track`, `select-laps`,
  `select-wait`, `select-result`. Each shows two humans, the local cabinet
  as P1. Cursors start on the fixture: P1 CRASH, P2 CORTEX, both on CRASH
  COVE and 3 laps. The countdown runs from 20 s over each 600 preview
  ticks while picking. On the picking screens and select-wait the
  opponent's cursor on its current item steps one entry every 30 ticks
  through its list, so a capture at a given frame is deterministic.
  - select-character: both on the character item; P2's cursor walks the 8
    characters.
  - select-track: both have locked their characters (the view reports
    CORTEX peer-locked); P2's cursor walks the 16 tracks.
  - select-laps: P1 has locked CRASH and CRASH COVE; P2 has locked CORTEX
    and TIGER TEMPLE, and its laps cursor walks 3, 5, 7.
  - select-wait: as select-laps, but P1 is done (3 laps), status WAITING,
    no countdown.
  - select-result: SELECT_RESULT, CONFIRMED: TIGER TEMPLE drawn (the votes
    differ), 3 laps agreed, P1 CRASH, P2 CORTEX, no reassignment, and the
    bots of retail 2P AI set 0 (POLAR, N. GIN, TINY, COCO).
- On START_RACE the hook logs the agreed match with Platform_Log, then
  hands the launch to the live race caller (Task 7,
  docs/RACE_LAUNCH_MILESTONE.md RL-S8b; until Task 7 it returned to the
  title with a "not wired yet" line). The agreed-match line:
  "arcade link: agreed match track <id> laps <n> seed 0x<16 hex> slots
  <8 characters> (<8 roles>)", the roles as 1, 2, B (bot), or - (inactive).

### 2.8 Screens (MainArcadeLinkLayout and drawer)

Same style as the accepted screens: the retail panel, DecalFont text, and
the retail row highlight. The select screens use a widened panel (x 4, w
504, y 28, h 176; the accepted screens keep x 56, w 400), so two columns of
names fit with a marker slot on each side of every name. The layout owns
its own name tables and select-order lists and names no select module;
MainArcadeLinkLayout_InputFromHostView maps the host view onto the layout
input field for field, and the drawer calls it.

- Picking screens (the local human on character, track, or laps): the
  title (SELECT CHARACTER, VOTE TRACK, VOTE LAPS) in FONT_BIG orange;
  "TIME s" in FONT_SMALL, s = ceil(ticksLeft / 30); the list; the retail row
  highlight on the local cursor's cell; and the footer with each
  opponent's progress in its player colour ("P2: CHOOSING CHARACTER",
  "VOTING TRACK", "VOTING LAPS", "READY", or "CONNECTING" before it is
  heard; with two or three opponents, short entries such as "P2 TRACK" or
  "P2 JOINING").
  - Character: 8 names, 2 columns x 4 rows, FONT_BIG.
  - Track: 16 names, 2 columns x 8 rows, FONT_SMALL.
  - Laps: 3 rows, one centred column, FONT_BIG.
- Markers: each present human at or past the item has a marker on its
  cursor's (or locked value's) cell, in the retail multiplayer player
  colours (PLAYER_BLUE, PLAYER_RED, PLAYER_GREEN, PLAYER_YELLOW for
  P1..P4). With two humans they read "P1" left and "P2" right of the name;
  with three or four they are digits, 1 then 3 from the left, 2 then 4
  from the right.
- Locks by colour, not a marker suffix: names are ORANGE like retail rows;
  a track or lap count some human has locked as a vote is WHITE; a
  character a peer has locked is GRAY (taken; CONFIRM on it is refused).
- Wait (the local human done, on SELECT): "WAITING FOR P2" (two humans) or
  "WAITING FOR PLAYERS", with the dot animation; "YOUR CHARACTER: ...",
  "YOUR TRACK VOTE: ...", "YOUR LAP VOTE: N LAPS"; then one line per
  opponent in its colour, its progress and live cursor (for example "P2:
  VOTING LAPS  5 LAPS"), "READY" when done, or "CONNECTING" before it is
  heard. No countdown or footer.
- Result (SELECT_RESULT): title "MATCH SET"; "TRACK <name>" and "LAPS <n>",
  each followed by " - RANDOM" when drawn; one line per human in its
  colour, "P1 CRASH", followed by " - REASSIGNED" when its pick was taken
  (for example "P2 CORTEX - REASSIGNED"); the bots as "CPU <name>, ..."
  wrapped at 36 characters; footer "GET READY".
- Strings use only glyphs the retail font has (checked by the layout
  isolation test against font_characterIconID). The font has no
  parentheses, hence " - REASSIGNED" rather than a parenthesised note;
  names use the full stop and the apostrophe ("N. GIN", "PAPU'S
  PYRAMID"), which it has.

The draw-list capacity is 32 items (it was 10).

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
   because the rematch base config carries them; the adapter implements
   and tests this (2.6), and it is live since Task 7: a rematch after a
   finished race starts on the previous picks, and one after a pre-race
   LINK ERROR starts from the select base.

## 4. Owner-accepted UX defaults

Each was chosen as a default for a two-cabinet, wheel-only kiosk and flagged
for the operator to confirm or change after seeing the built flow. The
owner has accepted all of them, SEL-1..SEL-17, as decisions.

1. SEL-1: A vote tie resolves by a seeded draw among the tied options only,
   not among all tracks, so a player's vote always counts.
2. SEL-2: The lap options are 3, 5, 7, as in retail. The cursor starts on 3
   (the fixture) or, in a post-race rematch (reachable since Task 7), on
   the previous pick (2.6).
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

Added when the screens were built (MS-9), and also accepted by the owner:

14. SEL-14: With 3-4 humans the cursor markers are coloured digits (1..4)
    instead of "P1".."P4", so four markers fit beside one name.
15. SEL-15: A locked pick is shown by the name's colour (WHITE for a locked
    track or lap vote, GRAY for a character a peer has taken) rather than
    by a marker suffix.
16. SEL-16: The select screens use a widened panel (x 4 to 508) instead of
    the accepted screens' panel, so two columns of names and their markers
    fit.
17. SEL-17: Marker colours are the retail multiplayer player colours
    (PLAYER_BLUE, _RED, _GREEN, _YELLOW for P1..P4). Blue P1 has the weakest
    contrast over the blue CTR ring behind the panel, but is legible.

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
Current state: 119 tests, 100% passing. MS-1 to MS-11 are done. Every
review found nothing blocking; the follow-up commits named below closed
the should-fix items and nits.

### MS-1 -- this document

Status: done (dc91e044a). No review required.

### MS-2 -- selection rules (native_match_select_rules)

Status: done (06e7157b8). Header, implementation, unit test (including
every ordered pair of distinct base characters against the bot rule), and
an isolation test (pure; the 2P AI set mirror checked against
game/zGlobal_DATA.c). Review outcome: nothing blocking.
- 6bf2453c4 (MS-2b): BuildConfig requires humanCount to equal the base's
  human slots and validates the outcome; the isolation test bans
  NativeMatchSelect/native_match_select tokens under game/ and checks the
  retail default-character mirror.
- d9edda9ef (MS-3b): BuildConfig rejects outcomes Resolve cannot produce
  (reassignment mask bit 0; anything but the first qualifying 2P AI set).

### MS-3 -- select message codec (native_match_select_message)

Status: done (85b196ec8). Header, implementation, unit test (every field,
every fault cause and the check order, every single-byte flip), and an
isolation test. Review outcome: nothing blocking.
- d9edda9ef (MS-3b): fault causes 17 BAD_ITEM_LOCK_MISMATCH and 18
  BAD_RESOLVED_ITEM (one cause per check), the documented check order, a
  NULL-data reader guard, and a digest-carrying writer test.

### MS-4 -- select session (native_match_select_session)

Status: done (bca5636ad). Header, implementation, unit test, a
fault-injection test over native_virtual_datagram (loss, duplication,
reordering, delay, corruption, lock change, nonce change, digest mismatch,
silence, stale selects; 2 humans, and 4 humans over pairwise harnesses),
and an isolation test. Review outcome: nothing blocking.
- c74f6926b (MS-4b): stale-record scenarios, including a stale record with
  a high sequence that ends in PEER_SILENT, never CONFIRMED; reorders
  counted apart from duplicates; padding-safe outcome compares.

### MS-5 -- peer-link aux channel

Status: done (a0f695012). The aux route, SendAux, TakeAux, AuxCount, and
DroppedAuxCount (2.4), loopback tests, and isolation checks (frozen aux
widths; the peer link names no select token). Review outcome: nothing
blocking.
- c74f6926b (MS-5b): tests of Open's aux reset and of aux retention across
  a terminal transition; lease, allocation, and higher-layer token bans on
  the peer link; the Poll terminal-mode doc corrected (no drain).

### MS-6 and MS-7 -- flow SELECT and SELECT_RESULT, netplay integration

Status: done, landed together in 5f869c165: with MATCH_FOUND -> SELECT in
the flow, every netplay path to START_RACE was broken until the adapter
drove the session. The appended screens, actions, observation field, and
timings (2.5), with unit tests of every new transition; the adapter's
select session, nonce, relink, and launch timeout (2.6), with loopback
tests (scripted, idle, same-tick, silence, launch-timeout, blocked-relink,
and rematch selects, and a golden nonce). Review outcome: nothing
blocking.
- 319f30201 (MS-7b): AgreedConfig on RESULTS only after a started race;
  BEGIN_REMATCH derives from lastReadyConfig; RELINK builds on the
  session's own base.
- a95623e4e (MS-8b): a pre-race LINK_ERROR returns CLOSE_LINK; the adapter
  reads the session only through accessors (new _Base, _HumanCount,
  _LocalHuman, _PeerLockedCharacterMask).

### MS-8 -- host, preview, options, entropy, logging

Status: done (eb5db1c29). The host view select fields and GetAgreedMatch,
the five previews, the options' selectEntropy, main.c's entropy read, the
host epoch mix, and the hook's Platform_Log of the agreed match (2.7).
Review outcome: nothing blocking.
- a95623e4e (MS-8b): host-named select and role constants with static
  asserts, and offsetof checks on every select-view field; the hook uses
  the host role constants, and no MainArcadeLink game file names the
  match-config roles.

### MS-9 -- select screens

Status: done (949cbfeaa). Layout and drawer for the five select screens
(2.8), with layout unit tests, isolation checks of the order lists, font
advances, glyphs, and colour ordinals against the retail tables, and a
visual check. No review required.

### MS-10 -- preview captures

Status: done (c345fc149). The capture checker, the preview script, and the
arcade_link_preview_render ctest cover the five new previews (17 in all);
alpha-stripped review PNGs checked. No review required.
- 52992d383 (MS-10b): the host-view -> layout-input mapping moved into the
  layout library as MainArcadeLinkLayout_InputFromHostView, and a
  cross-seam ctest (main_arcade_link_view_layout_unit) proves every
  preview and live host view passes the layout.

### MS-11 -- docs close-out

Status: done; reviewed; follow-up fixes in MS-11b (this change). This
document's statuses and design sections, docs/HANDOFF.md, the
docs/GAME_LOOP_UI_MILESTONE.md cross-references, and the
docs/LOBBY_MILESTONE.md peer-link note.

## 7. Risks and open questions

1. Closed by GAME_LOOP_UI Task 7: the race caller arms the race setup
   seam with the agreed config, whose plan maps it onto gGT->levelID,
   gGT->numLaps, and data.characterIDs, and the retail 2P load brings in
   the chosen set's AI pack. The bot rule mirrors LOAD_Robots2P, so that
   pack exists; arcade_link_launch validates two such races live.
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
7. Asymmetric relink completion (a Task 7 gate item, now handled). Each
   side's relink handshake completes independently, so one cabinet can
   reach READY on the resolved config while the other times out to LINK
   ERROR. Task 7 answers it with the launch agreement
   (docs/RACE_LAUNCH_MILESTONE.md RL-1..RL-7): START_RACE needs a launch
   commit, a relink that completes on one side only launches neither
   cabinet, and lastReadyConfig of a relink lobby is taken at START_RACE
   (RL-6), so the rematch after it agrees; the netplay loopback tests
   prove both. The residual two-generals case (a commit whose records
   never reach the peer before its launch timeout) launches one cabinet
   alone; it is fail-safe, and the rematch is REJECTED to OPPONENT LEFT on
   both (RL-7).
8. A stale select record from an earlier select on the same base is not
   filtered by baseDigest (2.3). It fails safe (NONCE_CHANGED,
   DIGEST_MISMATCH, or PEER_SILENT), never a wrong agreement, and the aux
   inbox resets and the BEGIN_SELECT discard keep it from arriving.
9. One-frame blank: on the single tick where the select session fails to
   start, the flow is on SELECT with an inactive select view, the layout
   rejects it, and nothing is drawn before LINK ERROR on the next tick.
10. MAIN_ARCADE_LINK_SELECT_TICKS_PER_SECOND (30) hard-codes the 30 Hz loop
    for the countdown, like every tick count here (GAME_LOOP_UI risk 3).
11. The title scene behind the translucent panel differs between parallel
    and one-at-a-time preview renders; the capture checker was calibrated
    to pass both (native_capture_check.c records the ranges).
12. A live `--arcade-link` run shows up to about 60 s of select (three
    20 s items), the 2 s result, and the relink and launch agreement
    before START_RACE logs the agreed match and launches the race. Until
    Task 8 that race is the undriven launch rehearsal
    (docs/RACE_LAUNCH_MILESTONE.md RL-10), which reports itself finished
    150 ticks after race tick 0 and shows RESULTS (RACE COMPLETE).
