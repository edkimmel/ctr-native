# Roster milestone

Design-and-status record for HANDOFF integration step 3, "Stable
two-human-plus-bot roster and RNG ownership", on branch `arcade`. Read
AGENTS.md and docs/HANDOFF.md first. This document follows the same pattern
as docs/MATCH_SELECT_MILESTONE.md: a prospective plan with a task list,
updated to record status as tasks land.

The owner decided RS-1 (section 4): the setup supports both match
profiles, ARCADE_TWO_CAB (the retail 2P arcade race, two humans and four
bots) and ARCADE_ONE_CAB (the retail 1P arcade race, one human and seven
bots). The single-cabinet tasks OC-1..OC-5 (section 6) added ONE_CAB to
the bot rules, the race setup plan, facts, and core, and the live roster
proof; it gives one machine a path to launch a real race without a peer.
The fixture, the arcade-link lobby, and match select remain TWO_CAB-only.

It gates docs/GAME_LOOP_UI_MILESTONE.md Task 7 (networked race launch) and,
through it, Task 8. Networked race launch itself (START_RACE loading the
race, asymmetric relink handling) stays Task 7; this milestone leaves the
seam Task 7 calls. Real two-cabinet hardware stays the step 6/7 gate.

## 1. Starting point

### What exists (dormant, tested, no live callsite)

- NativeMatchConfigV1 (platform/native_match_config.c): portable match
  identity, TWO_CAB (slot 0 CAB1 human, slot 1 CAB2 human, slots 2-5 bots,
  6-7 inactive) and ONE_CAB profiles, per-slot characterID and difficulty,
  masterSeed, rngDerivationVersion, botRulesDigest.
- NativeDeterministicRngBankV1 (platform/native_deterministic_rng.c):
  xoshiro256** bank, SHA-256/CTRNRNG1 derivation from (masterSeed, stream
  tag, stable slot), streams MATCH_SETUP, ITEMS, HAZARDS, BOT[0..7], with
  global versus per-bot requester ownership. It reads and replaces no
  retail RNG.
- game/MAIN/MainArcadeRoster: config -> stable roster plan; validates
  pointer-free native facts (counts, compact prefix, per-slot role,
  lifecycle, character, difficulty, kinds) against it.
- game/MAIN/MainArcadeBotSetup: validates spawn / nav path / acceleration
  facts per slot against the validated roster and stages MATCH_SETUP draws
  (one setupRandom per bot, ascending stable slot) into a locked plan with
  before/after bank digests.
- game/MAIN/MainArcadeSetupV4: composes both into a revalidated context for
  the value-only V4 projector.
- game/MAIN/MainArcadeBotTickEvidence: a dormant per-bot tick proof record
  binding config digest, botRulesDigest, and setup plan digest.
- Match select (docs/MATCH_SELECT_MILESTONE.md) resolves the race config:
  track, laps, unique characters, the retail 2P AI set for the four bots
  (the LOAD_Robots2P rule), and a masterSeed derived from both cabinets'
  nonces.
- The fixture (platform/native_arcade_link_options.c,
  NativeArcadeLinkFixture_Build). At the start of this milestone it set
  characters 0..5 at difficulty 0 and a placeholder botRulesDigest (the
  SHA-256 of "CTRN arcade-link fixture bot rules v1"). Since R-3 it holds
  characters {0, 1, 6, 4, 2, 3} in slots 0-5 (CAB1 Crash and CAB2 Cortex at
  difficulty 0, then Polar, N. Gin, Tiny, and Coco, the retail 2P AI set for
  Crash and Cortex, at medium 0xA0), gameMode1/gameMode2/rules 0, and the
  real botRulesDigest, NativeArcadeBotRules_DigestV1; the builder fails
  unless NativeArcadeBotRules_ValidateConfigV1 accepts the result.
- Live canonical evidence today: in internal builds the per-frame V1
  canonical projection (control, retail RNG, input;
  MainCanonicalState_ProjectLive, game/MAIN/MainMain.c) feeds V2 record and
  playback. The V3/V4 runtime (game/MAIN/MainCanonicalRuntime) is dormant;
  full DRIVERS Physics needs a topology snapshot, and topology capture stays
  unwired (the topology lease is retire-only).

### Retail facts this design rests on

Race shape (2P arcade):

- MainInit_Drivers (game/MAIN/MainInit.c:288-406) spawns the humans in
  drivers[0..numPlyrCurrGame-1], then, in arcade with 2 humans, bots via
  BOTS_Driver_Init for drivers[2..5] (6 drivers). The TWO_CAB profile
  therefore maps onto retail slots one to one; no compaction.
- LOAD_Robots2P (game/LOAD/LOAD_Assets.c:21-59, called at :184 on the 2P
  LOD path) writes data.characterIDs[2..5] from the first
  characterIDs_2P_AIs set holding neither human's character. No RNG.
- Difficulty is global: gGT->arcadeDifficulty
  (include/namespace_Main.h:792), set from the menu row
  (game/230/MM_MenuFlow.c:532) to D230.cupDifficulty.speed
  {0x50, 0xA0, 0xF0} (game/230/D230.c:302); BOTS_Adv_AdjustDifficulty
  (game/BOTS.c:199-441) reads it in arcade (:219-229), with CHEAT_SUPERHARD
  overriding it.
- Race-defining fields: gGT->levelID, gGT->numLaps, gGT->numPlyrNextGame,
  gGT->gameMode1 (ARCADE_MODE set at game/230/MM_MenuFlow.c:224, mode bits
  cleared at :152), gGT->gameMode2 (cheats, cup flags),
  gGT->arcadeDifficulty, data.characterIDs[8]; the load is requested with
  MainRaceTrack_RequestLoad (game/MAIN/MainRaceTrack.c:16).
- Bot setup facts: sdata->kartSpawnOrderArray (include/regionsEXE.h:3113),
  sdata->driver_pathIndexIDs (include/regionsEXE.h:2402),
  sdata->accelerateOrder (include/regionsEXE.h:3092).

Race shape (1P arcade, the ONE_CAB profile; the difficulty, field, and bot
setup facts above hold for it unchanged):

- MainInit_Drivers sets numDrivers = 8 when numPlyrCurrGame is 1 in arcade
  (game/MAIN/MainInit.c:352-355) and spawns bots for drivers[1..7] (the
  loop from numPlyrCurrGame, :363-366). ONE_CAB (slot 0 CAB1 human, slots
  1-7 bots) also maps onto retail slots one to one.
- LOAD_Robots1P (game/LOAD/LOAD_Assets.c:61-76, called at :150-153 on the
  1P LOD path) writes data.characterIDs[1..7]: for a base human h, the IDs
  0..7 without h, ascending. No RNG and no AI-set table.

Retail RNG (the ownership problem):

| State | Declared / seeded | Advanced by | Class |
|---|---|---|---|
| sdata->randomNumber (16-bit LCG, MixRNG_Scramble, game/MixRNG/MixRNG.c:3-8) | include/regionsEXE.h:2806; 100 at boot (game/zGlobal_SDATA.c:308); never reseeded | items (game/Vehicle/VehPhysGeneral.c:1767,1807,1864), bot item cooldown and attacks (game/PickupBots.c:88,110,117), stuck/warp (game/Vehicle/VehStuckProc.c:1478,1559-1577), particles (game/Particle.c:76-86), VS quips (game/UI/UI_VsQuip.c:52,71) | simulation (shared with presentation consumers) |
| sdata->advRng (RngDeadCoed pair) | include/regionsEXE.h:3074; constants only when both words are 0 (game/BOTS.c:310-313) | menus every frame (game/RECTMENU.c:737); bot nav-path and acceleration setup (game/BOTS.c:369-400); bot behaviour (game/BOTS.c:1039) and the per-bot start-line weapon cooldown (BOTS_GotoStartingLine, game/BOTS.c:3054) | simulation |
| gGT->deadcoed_struct | include/namespace_Main.h:1301; reset to constants every race (game/MAIN/MainInit.c:457-458) before MainInit_Drivers(gGT) (:482) and the post-drivers hook (:486) | particles (MixRNG_Particles, game/Particle.c:1334-1356) | simulation-canonical, reset per race |
| psxRandSeed (PSX BIOS rand) | game/MixRNG/PSX_BIOS_Rand.c:3; 1 at boot | roulette display (game/UI/UI_Weapon.c:109), crate spin (game/231/RB_Crate.c:143,440) | presentation |
| sdata->audioRNG | include/regionsEXE.h:2495; boot constant (game/zGlobal_SDATA.c:94) | voice lines (game/HOWL/HOWL_Voiceline.c:126-127), level random FX (game/HOWL/HOWL_LevelAudio.c:140-141), garage FX (game/HOWL/HOWL_Garage.c:110-111) | presentation |

NativeCanonicalRngV1 (include/platform/native_canonical_state.h:34-41)
carries randomNumber, deadcoed, and advRng; audioRNG and PSX rand are
deliberately not canonical (game/MAIN/MainMain.c, the comment in
MainCanonicalState_ProjectLive). Checkpoints carry psxRandSeed
(platform/native_checkpoint.c:2126,2191).

Consequence: randomNumber and advRng at race start depend on everything a
machine did since boot (menu time advances advRng every frame), and retail
bot setup draws nav paths and acceleration order from advRng. Two cabinets
with different menu histories would build different bots from the same
config. Step 3 must own these states at a defined point.

## 2. What "step 3 complete" means

1. Bot rules are defined and versioned, and
   NativeMatchConfigV1.botRulesDigest is the SHA-256 of their canonical
   encoding (no placeholder).
2. Given a validated TWO_CAB or ONE_CAB NativeMatchConfigV1, one seam
   configures the retail race (track, laps, 2 humans or 1, per-slot
   characters, the retail 2P AI set or the retail 1P bot rule, difficulty,
   mode and cheat bits) and requests its load.
3. RNG ownership is defined and enforced: at one fixed point in race init,
   every retail RNG state that survives across races is seeded from the
   match's masterSeed through the bank's MATCH_SETUP stream; the rest of
   the bank has a documented reserved role.
4. After MainInit_Drivers, the live roster and bot setup facts are read and
   validated through MainArcadeRoster and MainArcadeBotSetup_Plan directly
   (not MainArcadeSetupV4Context_Init: ctr_native does not link
   MainArcadeSetupV4); a mismatch latches a failed status.
5. Evidence: a single-machine proof runs the live game into a configured
   race several times and shows identical canonical digests for the same
   config and inputs, independence from menu history, and divergence for a
   different seed.
6. Everything is dormant unless armed; normal retail boot is unchanged; no
   canonical-state schema or replay format changes.

Status (OC-5): functionally complete for both profiles. R-2 through R-6e
(section 6) meet items 1-6 for TWO_CAB on one machine, and OC-1 through
OC-5 (section 6) extend items 1-5 to ONE_CAB on one machine. The live
roster proof (3.4) is the evidence for item 5, for each profile over 900
race ticks, covering same-seed identity, menu-history independence (runs
C and E for TWO_CAB, their one-cab counterparts I and J for ONE_CAB),
and seed divergence. The ONE_CAB runs go through the 1P setup, the green
light, bot driving, and 1P race physics, digested through the rng,
rcontrol, and topology-free drivers digests (the Physics group itself is
not digested, RS-13). Networked launch (Task 7) is done
(docs/RACE_LAUNCH_MILESTONE.md); the in-race lockstep drive (Task 8) and
real two-cabinet evidence (steps 6-7) remain.

## 3. Decided design

### 3.1 Bot rules: native_arcade_bot_rules (pure, platform)

platform/native_arcade_bot_rules.c,
include/platform/native_arcade_bot_rules.h, library
ctr_native_arcade_bot_rules. It links exactly ctr_native_match_config,
ctr_native_sha256, ctr_native_canonical_codec,
ctr_native_deterministic_rng, and ctr_native_match_select_rules (for the
2P AI table and the base-character check); no game tokens, no I/O, no heap.

Each profile has its own rules, canonical encoding, and digest (RS-19): the
TWO_CAB V1 rules below, unchanged byte for byte since R-2, and the ONE_CAB
1P V1 rules (OC-1). A config's botRulesDigest must be its own profile's
digest.

- NATIVE_ARCADE_BOT_RULES_V1 canonical encoding (TWO_CAB): ASCII tag "CTRN
  arcade bot rules v1" (no NUL), then fixed little-endian fields:
  rulesVersion (u32 = 1); the profile (TWO_CAB) and its 8 slot roles;
  retail race shape (humans 2, drivers 6, first bot slot 2, bots 4); the
  difficulty table {0x50, 0xA0, 0xF0}; the difficulty, bot-character, and
  mode policies; the seven 2P AI sets (28 bytes, from
  NativeMatchSelect_AiSetRacer); the RNG recipe (derivation version,
  MATCH_SETUP stream tag, the ordered seed-target codes of 3.3, the 16-bit
  randomNumber mask, the advRng all-zero fallback constants); the
  reserved-stream mask (ITEMS, HAZARDS, BOT[0..7] undrawn in v1). The
  offset/size/field table in include/platform/native_arcade_bot_rules.h is
  the authority for the layout; the encoding is 111 bytes
  (NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES).
- NativeArcadeBotRules_DigestV1(digest[32]) = SHA-256 of that encoding, the
  botRulesDigest of every TWO_CAB config. The golden digest, frozen in
  tests/native_arcade_bot_rules_test.c, is
  9022154eab793fb25d0a2d3b0c787d62fdaf9af490b7e3f1d48fbec8d4065eab.
- The 1P V1 canonical encoding (ONE_CAB): ASCII tag "CTRN arcade bot rules
  1P v1" (no NUL), then the same kinds of fields: rulesVersion 1; the
  profile (ONE_CAB) and its 8 slot roles; retail race shape (humans 1,
  drivers 8, first bot slot 1, bots 7); the difficulty table; the
  difficulty policy, bot-character policy 2 (the retail 1P rule,
  LOAD_Robots1P), and mode policy; the 8 candidate IDs 0..7 in
  LOAD_Robots1P walk order (in place of the AI sets); and the same RNG
  recipe and reserved-stream mask. The header's second offset table is the
  authority; the encoding is 93 bytes
  (NATIVE_ARCADE_BOT_RULES_1P_V1_ENCODED_BYTES).
- NativeArcadeBotRules_Digest1PV1(digest[32]) = SHA-256 of the 1P
  encoding, the botRulesDigest of every ONE_CAB config. The golden digest,
  frozen in the same test, is
  8d06649af8aa2594fbaca395aba3ebf1cce24689f8c193f5055f4441f1d8b0e3.
  NativeArcadeBotRules_DigestForProfileV1 returns DigestV1 for TWO_CAB and
  Digest1PV1 for ONE_CAB, and fails for any other profile
  (platform/native_arcade_bot_rules.c:301-312).
- Helpers: difficulty-table lookup/validation; expected 2P bots for two
  human characters (NativeArcadeBotRules_ExpectedBots2P, the LOAD_Robots2P
  rule); expected 1P bots for one human character
  (NativeArcadeBotRules_ExpectedBots1P, the LOAD_Robots1P loop mirrored
  loop for loop); the retail seed mapping and derivation of 3.3 on a
  caller-owned bank, shared by both profiles; and
  NativeArcadeBotRules_ValidateConfigV1, the config check behind RS-1..RS-4
  and RS-19..RS-20. It accepts a TWO_CAB or ONE_CAB config whose
  botRulesDigest is its own profile's digest; TWO_CAB needs distinct base
  human characters and exactly ExpectedBots2P's bots, ONE_CAB a base CAB1
  character and exactly ExpectedBots1P's bots, both with the humans at
  difficulty 0 and every bot at one table difficulty
  (platform/native_arcade_bot_rules.c:443-529).
- Isolation test: pure (bans game, lease, lockstep, replay tokens), the
  link set is exact, the difficulty table mirrors game/230/D230.c, the
  advRng fallback constants mirror game/BOTS.c, and (OC-1) the 1P candidate
  count equals LOAD_CHARACTER_ID_COUNT, the LOAD_Robots1P body (one
  definition in game/) and its 1P call site in LOAD_Assets.c are
  unchanged, and MainInit_Drivers still gives 1P arcade 8 drivers with the
  first bot in slot 1.

### 3.2 Race setup: game/MAIN/MainArcadeRaceSetup

As built: three pure standalone libraries (C17, extensions off, linked
into ctr_native, never unity-included) behind one thin live adapter. All
four serve both profiles since OC-2 (the per-profile shape lives in the
plan and the core; the adapter holds no profile logic).

- Plan (R-4, OC-2, game/MAIN/MainArcadeRaceSetupPlan.{c,h}):
  MainArcadeRaceSetupPlan_Build(config, out) fails closed unless
  NativeArcadeBotRules_ValidateConfigV1 accepts the config (RS-1..RS-4 and
  RS-19..RS-20: a TWO_CAB or ONE_CAB profile; gameMode1, gameMode2, and
  rules 0; botRulesDigest equal to the profile's digest, DigestV1 or
  Digest1PV1; a match-select table track and lap count; base human
  characters at difficulty 0, distinct for TWO_CAB; bots that follow the
  profile's retail rule, LOAD_Robots2P or LOAD_Robots1P, at one table
  difficulty), the tick rate is exactly 30/1 (RS-14), and the humans are
  retail players 0 and 1 (TWO_CAB: CAB1 slot 0, CAB2 slot 1) or 0 (ONE_CAB:
  CAB1 slot 0, no CAB2). The plan holds the profile, levelID, numLaps,
  numPlyrNextGame (2 or 1), the gameMode1 and gameMode2 clear and set masks
  (RS-2: every non-transient bit pinned, ARCADE_MODE set, the vibration
  bits 0, RS-15; the same masks for both profiles, since the retail 1P menu
  path differs only in numPlyrNextGame), arcadeDifficulty (the bots' shared
  value), boolDemoMode 0 (RS-16), the characterIDs of its
  characterWriteMask (TWO_CAB 0x3F: slots 0..5, 6-7 untouched, since retail
  never reads them for a 6-driver race; ONE_CAB 0xFF: all eight), the
  firstBotSlot and botCount (2 and 4, or 1 and 7), the aiSetIndex (the
  retail 2P AI set, or none, 0xFF, for ONE_CAB), and the expected bots
  (RS-22). MainArcadeRaceSetupPlan_Apply applies it to a pointer-free
  mirror of the retail fields and refuses a plan that is not well-formed,
  including one whose profile fields are mixed or whose bots are not the
  humans' bots; MainArcadeRaceSetupPlan_Digest is the SHA-256 of its v2
  encoding, 135 bytes (RS-21). The goldens, frozen in
  tests/main_arcade_race_setup_plan_test.c, are
  927e7d2f3f62c1b56666ac3a56e3328221585ef71b9d54d412d9965ab0902bb2
  (TWO_CAB) and
  c7f0dabbf003eda2f4ce1968dac4de2285533a163f4957221a697072420bc40d
  (ONE_CAB). The header holds the gameMode1/gameMode2 bit audit, the 1P
  load-path audit, and the adapter contract.
- Facts (R-5a, OC-2, game/MAIN/MainArcadeRaceSetupFacts.{c,h}):
  MainArcadeRaceSetupFacts_Build turns a pointer-free snapshot of the race
  after MainInit_Drivers and the pre-race roster input into
  MainArcadeRosterNativeFacts and MainArcadeBotSetupSourceFacts, observed
  only, never copied from the config: it reads only the config's profile,
  to know which retail players may be human (TWO_CAB players 0 and 1,
  ONE_CAB player 0; a human anywhere else fails), and its isolation test
  enforces that. The header holds the retail audit of the spawn, nav path,
  and acceleration orders for the 2P race and, since OC-2, the 1P race; no
  dormant validator rule needed a correction for either.
- Decision core (R-5c, game/MAIN/MainArcadeRaceSetupCore.{c,h}): every
  decision of the adapter as a pure step over a pointer-free view of the
  live values: the state checks, the Launch preconditions, the load-field
  verification, the exact ordered list of retail writes, the seed order,
  the fact validation (MainArcadeRoster_BuildPlan and
  MainArcadeRoster_ValidateNativeFacts, then MainArcadeBotSetup_Plan on the
  post-seed bank), and the failure codes. The header holds the state
  machine and the RS-17 counter audit. Launch emits the same 15 ops for
  both profiles: every characterIDs slot is written, the plan's
  characterWriteMask slots with the plan's values and the others with
  their observed values.
- Adapter (R-5b, game/MAIN/MainArcadeRaceSetup.{c,h}, CTR_NATIVE only, in
  the unity chain): reads the view from the retail globals, calls the core,
  applies exactly the writes it returns, in order, and logs one line per
  state change or failure ("arcade race setup:"); the "armed" line names
  the profile (TWO_CAB or ONE_CAB) and all eight characterIDs of the plan.
  It uses MainArcadeRoster and MainArcadeBotSetup_Plan directly, not
  MainArcadeSetupV4Context_Init: ctr_native does not link
  ctr_native_arcade_setup_v4, and neither the adapter nor the proof hook
  names the V4 setup, the V4 projector, or MainCanonicalRuntime.

The seam (Task 7's entry points; the proof uses the same ones):

- MainArcadeRaceSetup_Arm(config): IDLE only. Builds the plan, derives the
  bank (NativeDeterministicRngBankV1_Init from masterSeed and
  rngDerivationVersion), and saves the cabinet's vibration bits (RS-15).
  ARMED; on failure it stays IDLE with failure PLAN or BANK and returns 0.
- MainArcadeRaceSetup_Launch(): ARMED only. Preconditions, each failing
  closed (FAILED/PRECONDITION) with nothing written: Loading.stage idle,
  the four pending OnBegin mode words 0, the options loaded, and no PAUSE
  bit. Then it writes the plan's fields except levelID (the mode words,
  arcadeDifficulty, boolDemoMode, numLaps, numPlyrNextGame, characterIDs)
  and calls MainRaceTrack_RequestLoad with the plan's level. LAUNCHED. It
  may run from inside the attract demo race; MainArcadeRaceSetupCore.h
  says why that is safe.
- MainArcadeRaceSetup_Status and _Failure: IDLE, ARMED, LAUNCHED, SEEDED,
  VALIDATED, or FAILED, and the latched failure code (append-only);
  _StatusName and _FailureName give their fixed log names.
- MainArcadeRaceSetup_Digests (VALIDATED only: the config, race plan,
  locked bot setup plan, and post-setup bank digests), _SlotFacts
  (VALIDATED only: the validated per-slot facts), _SeedReadback and
  _PinReadback (SEEDED or VALIDATED: what the setup wrote and what it read
  back), and _Bank (the post-setup bank when VALIDATED, else NULL).
- MainArcadeRaceSetup_Disarm(): any state to IDLE. It touches the
  vibration bits only if Launch set fieldsWritten
  (MainArcadeRaceSetupCore.c:214, checked at :423). After a Launch it
  restores the saved bits only on the idle main-menu level; otherwise it
  leaves them as they are and logs that, since gameMode1 must not change
  under a running race. "As they are" means 0 from the pin; the pause-menu
  toggle (game/MAIN/MainFreeze.c, risk 8) can no longer flip one mid-race
  since Task 7's RL-13 guard.

Two CTR_NATIVE hooks in MainInit_FinalizeInit (game/MAIN/MainInit.c):

- MainArcadeRaceSetup_OnFinalizeInitBegin, the very first statement block
  (:425), acts only in LAUNCHED: it verifies levelID (LEVEL_MISMATCH) and
  numLaps, numPlyrCurrGame (2 or 1), and the characterIDs of the plan's
  characterWriteMask (0..5 for TWO_CAB; 0..7 for ONE_CAB, where the load's
  LOAD_Robots1P wrote 1..7) (LOAD_FIELDS_MISMATCH), re-applies the mode
  words, arcadeDifficulty, and boolDemoMode, pins gGT->timer and
  gGT->frameTimer_Confetti (RS-17), seeds the retail RNG states (3.3), and
  reads the seeds and pins back. SEEDED.
- MainArcadeRaceSetup_OnDriversInitialized, immediately after
  MainInit_Drivers (:486), acts only in SEEDED: it checks that the plan's
  mode bits still hold and no cheat bit is set (FACTS), reads the snapshot
  and the roster input (MainCanonicalDrivers_ExtractRosterInputPreRace:
  the race order is not rebuilt before the first race tick), builds the
  facts, and validates them (ROSTER, BOT_SETUP). On a NAV_MISMATCH it logs
  each nav path's numPoints. VALIDATED, else FAILED; it writes no retail
  field.
- A hook in the wrong state (OnFinalizeInitBegin in SEEDED or VALIDATED,
  OnDriversInitialized in LAUNCHED) latches FAILED/STATE, except, since
  Task 7's RL-9 change, OnFinalizeInitBegin in VALIDATED with a tracker
  on the main-menu level (the return load), a no-op; a hook that
  should act without a game tracker latches FAILED/NO_TRACKER. Every other
  hook call is a no-op, so default boot and every load not launched here
  are unchanged.
- The state is a file-scope static in the adapter, outside every
  checkpoint region, never recorded or canonical (RS-11).
- tests/main_arcade_race_setup_isolation_test.cmake enforces the hook
  placement, that Arm, Launch, and Disarm are named only in
  MainArcadeRaceSetup.{c,h}, MainArcadeRosterProof.c, and (since Task 7)
  the race caller MainArcadeRaceLaunch.c, that each core
  write target stores to its one retail field, that nothing writes
  levelID, and the token bans. The plan, facts, and core have their own
  unit and isolation tests; each unit test covers both profiles, and the
  core test runs a ONE_CAB race setup end to end.

### 3.3 RNG ownership

- Retail RNGs remain the in-race simulation RNG: no retail call site is
  migrated to the bank (RS-5). Identical call order on both cabinets comes
  from lockstep running the same simulation on the same inputs, and is
  proven by the two-run evidence (3.4).
- The seeds are written by the pre-drivers hook, at the very start of
  MainInit_FinalizeInit, when it acts in LAUNCHED (and moves to SEEDED),
  right after the RS-17 pins. It draws five values from the bank's
  MATCH_SETUP stream (global slot) and writes them in this fixed order:
  randomNumber = draw & 0xFFFF; advRng.state0 = draw; advRng.state1 = draw
  (both zero -> the retail constants 0x30215400 / 0x493583fe);
  psxRandSeed = draw; audioRNG = draw.
- deadcoed is not seeded: the retail reset to its constants still runs,
  after the hook (MainInit.c:457-458). The R-1 plan put the seeds after
  that reset; seeding at the very start is equivalent, because nothing
  between the hook and MainInit_Drivers draws a seeded state.
- MainArcadeBotSetup's per-bot setupRandom draws follow on the same stream
  after the seeds, in ascending stable slot, so the post-setup bank
  (MainArcadeRaceSetup_Bank) has drawn MATCH_SETUP nine times for TWO_CAB
  (five seeds, four bots) and twelve times for ONE_CAB (five seeds, seven
  bots; RS-22). That is the bank Task 8 must project. The seed recipe and
  its order are the same for both profiles.
- ITEMS, HAZARDS, BOT[0..7] are reserved and undrawn in bot rules v1. Any
  migration of a retail call site to them is a new bot-rules version.
- Presentation RNGs (psxRandSeed, audioRNG) are seeded for cabinet parity
  but stay out of canonical state, as today.

### 3.4 Evidence: live roster proof (internal builds)

- An internal-only option, `--arcade-roster-proof <log path>`, with
  `--arcade-roster-proof-seed <u64>` (default 1),
  `--arcade-roster-proof-dwell <ticks>` (0..7200, default 0),
  `--arcade-roster-proof-ticks <N>` (1..3600, default 900), and (OC-3)
  `--arcade-roster-proof-profile two-cab|one-cab` (default two-cab). main.c
  configures the proof (NativeArcadeRosterProof_Configure builds the
  config of the chosen profile) and hands the config to the CTR_NATIVE &&
  CTR_INTERNAL game hook game/MAIN/MainArcadeRosterProof; its usage text
  and its startup line name the profile. The config, per profile:
  - two-cab: the fixture, resolved through match select with two fixed
    choices (the fixture characters, track, and laps; nonces seed and seed
    XOR 0x9E3779B97F4A7C15);
  - one-cab (RS-23): no match select; InitArcadeOneCab roles, the
    fixture's identity, track, laps, tick rate, and CAB1 character, the
    LOAD_Robots1P bots at the fixture's bot difficulty, masterSeed = the
    seed, botRulesDigest Digest1PV1.

  Options, config builder, report writer, and exit-code table live in
  platform/native_arcade_roster_proof.{c,h}. The proof is rejected with any
  arcade-link or replay option and with --exit-after-frame; quick states
  are disabled.
- The hook waits for the title's menu-ready frame
  (MainArcadeLinkPolicy_TitleMenuReady, the rule the arcade-link hook
  uses), then `dwell` ticks, then launches (MainArcadeRaceSetup_Arm and
  _Launch) from the first launch window it sees: the title, or the attract
  demo race. It installs the profile's scripted pads for the whole run
  (Platform_InputInstallPadSnapshots; RS-24): pads 0 and 1 connected in
  both profiles, neutral through race tick 0; then for two-cab both
  players hold CROSS and CAB2 also holds RIGHT for ticks 60-89 of every
  120, and for one-cab player 0 (CAB1) holds CROSS and also RIGHT for ticks
  60-89 of every 120 while player 1 stays neutral.
  Race tick 0 is the first frame after VALIDATED on which the
  topology-free DRIVERS extraction succeeds. From it the hook logs one line
  per tick: tick, the V1 control, rcontrol, RNG, and input domain digests,
  and the SHA-256 of the topology-free DRIVERS candidate
  (MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta
  through the canonical encoders with Physics zeroed, never raw struct
  bytes). The report header carries the result, the profile (format v8: a
  "profile TWO_CAB" or "profile ONE_CAB" line right after the result, the
  profile of the configured config), the launch window, the config, race
  plan, bot setup plan, and bank digests, the "seeded" line (the seeds and
  pins read back, "match 1"), and one line per slot.
- The proof ends through Platform_RequestExit with its result as the exit
  code: 0 PASS; failures from 20 up (the table in
  include/platform/native_arcade_roster_proof.h); any other exit while the
  proof is active is 20 (INCOMPLETE), never 0.
- Host timing (R-6b, RS-18): main.c turns on a host-local, proof-only
  fixed VBlank pacing (Platform_SetFixedVBlankPacing,
  platform/native_vblank_pacing.c): the pacer never emits late (catch-up)
  VBlanks, so every game tick advances exactly the retail 2 VBlanks and
  gGT->elapsedTimeMS cannot follow a host hitch. Every other run keeps the
  default pacing; V2 playback keeps its own packet path.
- Each tick line also carries a race-relative control digest (rcontrol):
  the V1 control encoding and FNV-1a 64 digest with frameTimer,
  frameCounter, and timer zeroed, computed locally (no schema change). The
  report header (format v7 and later) logs those three counters and
  frameTimerConfetti at the launch tick and at race tick 0.
- The checker, tools/arcade-roster-proof-check.ps1, run by the ctest
  arcade_roster_determinism (Windows only, label "live"), starts ten
  proofs, five two-cab (A-E) and five one-cab (F-J), each logging -Ticks
  race ticks (900 in ctest):
  - A: seed 0x5EED, dwell 0 (launches from the title);
  - B: A again;
  - C: seed 0x5EED, dwell 5400 (launches from inside the attract demo
    race);
  - D: seed 0x5EEE, dwell 0;
  - E: seed 0x5EED, dwell 37 (launches from the title 37 ticks late);
  - F: one-cab, seed 0x5EED, dwell 0 (launches from the title);
  - G: F again;
  - H: one-cab, seed 0x5EEE, dwell 0;
  - I: one-cab, seed 0x5EED, dwell 5400 (launches from inside the attract
    demo race; the one-cab C);
  - J: one-cab, seed 0x5EED, dwell 37 (launches from the title 37 ticks
    late; the one-cab E).

  A-E pass no profile option, so they run the default two-cab profile with
  the command lines they had before OC-3. The checker runs all ten in
  parallel by default (-Sequential runs them one after another) and fails
  unless:
  - every run exits 0, and its report is format v8 with result PASS, the
    profile it expects on the line right after the result, the launch
    window it expects, both counter lines, a seeded line ending "match 1",
    eight slot lines with the profile's roles (TWO_CAB: CAB1_HUMAN,
    CAB2_HUMAN, four BOTs, two INACTIVE; ONE_CAB: CAB1_HUMAN and seven
    BOTs), and exactly the run's requested tick lines, numbered from 0,
    then "end ticks N" (the proof itself reports PASS only when
    tickLineCount == ticksRequested);
  - A and B are byte-identical, and so are F and G;
  - C and E equal A in the config, race plan, bot setup plan, and bank
    digests, the seeded line, the slot lines, and at every tick the rng,
    input, drivers, and rcontrol digests;
  - E's timer offset from A at launch is odd (measured at the launch tick,
    before the RS-17 pin; after it every run reads 0);
  - C's and E's race tick 0 timer and frameTimerConfetti equal A's (the
    RS-17 pins held);
  - I and J are measured against F exactly as C and E are against A: they
    equal F in the same setup digests, seeded and slot lines, and per-tick
    digests; J's timer offset from F at launch is odd; and I's and J's
    race tick 0 timer and frameTimerConfetti equal F's;
  - D differs from A in the config digest, the bank digest, and the tick 0
    rng digest, and H differs from F in the same three;
  - F differs from A in the config digest and the race plan digest
    (another profile);
  - the input digest (the raw pad state only) shows the one-cab pads reach
    the game: F equals A at race tick 0 (neutral pads, the same pad
    layout), differs from A at every later tick both logged (A's player 1
    holds CROSS, F's is neutral), and equals H at every tick (the pads do
    not depend on the seed).

  The full control digest is informational only: it still differs in the
  unpinned boot-relative counters frameCounter and frameTimer. The checker
  prints the C-A, E-A, I-F, and J-F offsets of all four counters at launch
  and at race tick 0, with their values mod 8. It skips (77) when
  assets/ctr-u.bin is absent, no display is available, or the build
  rejects the internal option.
- The one-cab runs were first capped at 90 race ticks (-OneCabTicks),
  before the green light, by an MSVC Debug run-time check failure in the
  retail 1P rank-icon HUD (risk 13). The cap and -OneCabTicks were removed
  once game/UI/UI_Rank.c was fixed in place (a98dccbe8); F-J now run the
  full -Ticks.

## 4. Owner-accepted defaults

The owner reviewed and accepted every remaining default below (RS-2..RS-24),
so each is now an owner decision rather than a pending default. RS-1 was
never a pending default: it is the owner's own earlier decision, which
overrode the TWO_CAB-only default.

1. RS-1 (owner decision, which overrode the TWO_CAB-only default): the
   live scope is both profiles. TWO_CAB is the retail 2P arcade single
   race, two humans and four bots, 6 drivers; ONE_CAB is the retail 1P
   arcade single race, one human and seven bots on the LOAD_Robots1P rule
   (RS-20), 8 drivers. The reason is a one-machine path to launch a real
   race without a peer. The fixture, the arcade-link lobby, and match
   select remain TWO_CAB-only; only the internal roster proof (RS-23)
   launches a race through the ONE_CAB setup today, and a ONE_CAB
   lobby/UI flow is a follow-up (risk 14).
2. RS-2: The config's gameMode1, gameMode2, and rules stay 0 and mean
   "retail arcade single race, no cheats, no cup". The plan pins every
   non-transient gameMode1 and gameMode2 bit, not only the cheat and cup
   bits (the bit audit in game/MAIN/MainArcadeRaceSetupPlan.h): gameMode1
   keeps only its TRANSIENT bits (START_OF_RACE, WARPBALL_HELD, MAIN_MENU,
   END_OF_RACE, GAME_CUTSCENE, LOADING) and gains ARCADE_MODE; gameMode2
   keeps only LEV_SWAP, CREDITS, and NO_LEV_INSTANCE. Every other bit,
   cheat, cup, pause, and vibration bits included, is 0. Launch writes the
   words, the pre-drivers hook re-applies them, and the post-drivers hook
   fails closed (FACTS) if a cheat bit or a plan bit changed. Nonzero
   config values are reserved for future modes and rejected. This closes
   the Task 7 cheat-reset item (GAME_LOOP_UI risk 8) once Task 7 launches
   through the seam, which it does (game/MAIN/MainArcadeRaceLaunch.c).
3. RS-3: Difficulty stays global, as in retail. Every bot slot carries the
   retail speed value (0x50 easy, 0xA0 medium, 0xF0 hard), all equal; human
   slots carry 0. The arcade-link fixture uses medium (0xA0).
4. RS-4: TWO_CAB bot characters follow the LOAD_Robots2P rule (as SEL-4;
   ONE_CAB bots follow LOAD_Robots1P, RS-20); retail still writes them
   live, and the fact validation checks agreement.
5. RS-5: Retail RNGs stay the in-race simulation RNG; nothing migrates to
   the bank in v1.
6. RS-6: The bank's MATCH_SETUP stream owns the retail seeds, then the
   per-bot setupRandom draws. ITEMS, HAZARDS, and BOT[0..7] are reserved.
7. RS-7: Seed mapping and order as in 3.3, written at the very start of
   MainInit_FinalizeInit by the pre-drivers hook (in LAUNCHED). deadcoed is
   not seeded; its retail per-race reset still runs after the hook.
8. RS-8: Presentation RNGs (psxRandSeed, audioRNG) are seeded for cabinet
   parity but stay non-canonical.
9. RS-9: The bot-rules digest covers native rule choices only. Retail
   per-track difficulty tables and nav data are covered by build and
   content identity, not re-hashed. Source: the CAB1 fleet plan
   C:\Arcade\docs\ctr-native\M3-BOTS-ROSTER-PACKAGE.md (outside this
   repository), section "Decisions intentionally deferred", which leaves
   open whether normalized nav semantics belong in the bot-rules digest or
   are sufficiently diagnosed by content identity plus canonical TOPOLOGY.
   Bot rules v1 takes the content-identity side, which the owner accepted
   (section 4 intro).
10. RS-10: A fact-validation failure latches FAILED and logs; step 3 does
    not abort the race itself. Task 7 decides the player-facing response.
    Closed by Task 7 RL-11 (docs/RACE_LAUNCH_MILESTONE.md): a FAILED setup
    before the rehearsal ends, an Arm or Launch failure, or a bounded-wait
    expiry ends the linked race locally as RESULTS LINK ERROR through
    NativeArcadeLinkHost_ReportRaceFailure (the peer is not told); the race
    caller logs the failure, returns to the main-menu level, clears the
    rehearsal pads, and Disarms on the first idle main-menu frame (at once
    after an Arm or Launch failure at the title).
11. RS-11: Setup state is game-owned static, never checkpointed, replayed,
    or canonical; quick states are disabled in proof mode.
12. RS-12: Boot-relative control counters are not altered by the setup in
    v1, except the ones that feed the race simulation, which the setup pins
    (RS-17).
13. RS-13: The proof digests V1 control/RNG/input plus the topology-free
    DRIVERS candidate; Physics, WORLD, TOPOLOGY, and live V4 projection
    stay with Task 8.
14. RS-14 (R-4): The config's tick rate must be exactly 30/1, the retail
    30 Hz loop. MainArcadeRaceSetupPlan_Build enforces it
    (game/MAIN/MainArcadeRaceSetupPlan.c:159-160, against
    MAIN_ARCADE_RACE_SETUP_TICK_RATE_NUMERATOR/_DENOMINATOR in
    MainArcadeRaceSetupPlan.h); NativeArcadeBotRules_ValidateConfigV1 does
    not check it, and tests/main_arcade_race_setup_plan_test.c rejects 60/1
    and 30/2.
15. RS-15 (R-4, R-5b): In a linked race the vibration bits P1..P4_VIBRATE
    are pinned to 0 (rumble on for every pad, the retail default without a
    memory card): they are in the plan's gameMode1 clear mask
    (MainArcadeRaceSetupPlan.h, MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK
    and _CLEAR_MASK), so a cabinet's saved rumble preference does not apply.
    Arm saves the cabinet's bits (MainArcadeRaceSetupCore.c:148). Disarm
    touches them only if Launch set fieldsWritten
    (MainArcadeRaceSetupCore.c:214, checked at :423); after a Launch it
    restores them only on the idle main-menu level
    (MainArcadeRaceSetupCore.c:423-436), otherwise it leaves them as they
    are and logs that: 0 from the pin, unless the pause-menu toggle
    (game/MAIN/MainFreeze.c:518) flipped one mid-race.
16. RS-16 (R-4): boolDemoMode is pinned to 0
    (MainArcadeRaceSetupPlan.c:197), written at Launch and re-applied by the
    pre-drivers hook. Demo mode would turn every human driver into a bot
    (game/MAIN/MainInit.c:568-573) and run the demo exit.
17. RS-17 (R-6c): At race init, after the load-field verification and
    before the seeds, the setup pins the boot-relative counters that feed
    the race simulation or its RNG: gGT->timer = 0 (exhaust, terrain, warp
    dust, bubble, and flame particles pick frames by it, and those particles
    draw MixRNG) and gGT->frameTimer_Confetti = 0 (particle oscillators).
    The adapter reads both back and the proof checks them (the "seeded"
    line; PIN_MISMATCH); the checker also requires C and E to start race
    tick 0 with A's timer and frameTimerConfetti, and I and J with F's.
    sdata->frameCounter and gGT->frameTimer_VsyncCallback feed only
    presentation and the platform (the VBlank counter is also not safe to
    reset: the load queue compares it with a stored timestamp), so they
    stay boot-relative and the full V1 control digest stays informational. The audit of every counter, its
    readers, and its verdict is in game/MAIN/MainArcadeRaceSetupCore.h.
18. RS-18 (R-6b): Fixed VBlank pacing (Platform_SetFixedVBlankPacing,
    include/platform.h; the pure decision NativeVBlankPacing_Plan,
    platform/native_vblank_pacing.c) is proof-only: main.c turns it on only
    for the roster proof, and every other run keeps the retail-faithful
    catch-up pacing. It is not the linked-race answer: Task 8 must adopt
    deterministic VBlanks per tick (risk 7).

RS-19..RS-24 are the implementation defaults chosen to carry out the
owner's RS-1 decision; the owner has since accepted them with the rest of
this section, so they are owner decisions.

19. RS-19 (OC-1, eb5ef25ef): Each profile has its own bot-rules encoding
    and digest, and a config's botRulesDigest must be its own profile's:
    NativeArcadeBotRules_DigestV1 (TWO_CAB, 111 bytes, tag "CTRN arcade
    bot rules v1", golden 9022154e..., unchanged byte for byte because the
    fixture, match select, and the netplay config paths carry it) or
    NativeArcadeBotRules_Digest1PV1 (ONE_CAB, 93 bytes, tag "CTRN arcade
    bot rules 1P v1", golden 8d06649a...). DigestForProfileV1 picks it
    (platform/native_arcade_bot_rules.c:301-312) and ValidateConfigV1
    requires it (:462-467).
20. RS-20 (OC-1, eb5ef25ef): A ONE_CAB human is a base character
    (NativeMatchSelect_CharacterIndex), and the bots, in ascending slot
    order, are exactly LOAD_Robots1P's result for that human:
    NativeArcadeBotRules_ExpectedBots1P mirrors LOAD_Robots1P
    (game/LOAD/LOAD_Assets.c:61-76, called at :150-153) loop for loop
    (platform/native_arcade_bot_rules.c:353-386), and ValidateConfigV1
    requires it (:492-500). The bot rules isolation test pins the
    LOAD_Robots1P body, its 1P call site, and the MainInit_Drivers 1P
    shape.
21. RS-21 (OC-2, 30d5a1c71): The race setup plan encoding is v2 for both
    profiles: tag "CTRN arcade race setup plan v2", 135 bytes, adding the
    profile, firstBotSlot, botCount, and 7 expectedBots entries (the unused
    tail 0). TWO_CAB field values are unchanged; its plan digest changes
    only with the version. The size is static-asserted
    (game/MAIN/MainArcadeRaceSetupPlan.c:16-24) and the goldens (3.2) are
    frozen in tests/main_arcade_race_setup_plan_test.c. The plan digest is
    proof evidence only: not canonical, not replayed, never sent.
22. RS-22 (OC-2, 30d5a1c71): The per-profile plan shape
    (game/MAIN/MainArcadeRaceSetupPlan.c:56-76, enforced by the
    well-formedness check at :78-148): TWO_CAB numPlyrNextGame 2,
    characterWriteMask 0x3F, firstBotSlot 2, botCount 4, aiSetIndex the
    retail 2P AI set; ONE_CAB numPlyrNextGame 1, characterWriteMask 0xFF,
    firstBotSlot 1, botCount 7, aiSetIndex none (0xFF). The ONE_CAB
    post-setup bank has drawn MATCH_SETUP 12 times (5 seeds, 7 bots)
    versus 9 for TWO_CAB (tests/main_arcade_race_setup_core_test.c:1210).
23. RS-23 (OC-3, 5e0c71c36): The one-cab proof config: match select is
    not used; NativeMatchConfigV1_InitArcadeOneCab gives the roles, the
    arcade-link fixture gives the identity, track, laps, tick rate, CAB1
    character, and bot difficulty, the bots are ExpectedBots1P of the CAB1
    character, masterSeed is the seed option itself, and botRulesDigest is
    Digest1PV1; the result must pass ValidateConfigV1
    (platform/native_arcade_roster_proof.c:356-419).
24. RS-24 (OC-3, 5e0c71c36): The proof's scripted pads per profile
    (platform/native_arcade_roster_proof.c:515-576): pads 0 and 1 connected
    digital pads in both profiles (the same pad layout), pads 2 and 3
    disconnected, all neutral through race tick 0. From race tick 1,
    TWO_CAB: both players hold CROSS and player 1 also holds RIGHT in the
    steer window (ticks 60-89 of every 120); ONE_CAB: player 0 holds CROSS
    and also RIGHT in the steer window, and player 1 stays neutral.

## 5. Constraints

1. Topology lease untouched: no acquire, activate, capture, or publish; no
   lease owner in checkpoints, replay, or canonical state; no retire hook
   on LOAD_Hub_ReadFile. The setup never calls topology capture.
2. No canonical-state schema or replay-format change. NativeMatchConfigV1's
   layout is unchanged; only field values (botRulesDigest, difficulty)
   change.
3. Game code names no lockstep, failure-handling, or match-select module
   (existing isolation tests stay as strict).
4. No dynamic allocation; portable C17, extensions off.
5. Every new seam gets a unit test; every structural rule an isolation
   test. New game .c files go in game/game_unity.h.
6. Default boot (no arcade-link or proof option) is unchanged.
7. A task is done only when `cmake --build build-msvc-x86 --config Debug`
   succeeds and the full `ctest --test-dir build-msvc-x86 -C Debug` suite
   passes. Commit each green task on `arcade` only; no push. Tasks that
   touch simulation identity, RNG, replay, or canonical state are reviewed.

## 6. Task list

Baseline before this milestone: 119 tests, 100% passing (commit
269afbd5b). End state: 133 tests, two of them labelled "live"
(arcade_roster_determinism and arcade_link_preview_render; `ctest -LE live`
excludes them); the full suite, both live tests included, takes about
400 s.

Review outcomes below come from the commit messages and the task briefs;
"review: none recorded" means there is no evidence the commit was
reviewed on its own.

### R-1 -- this document

Status: done, 1b8f5ed76; this close-out (R-7) records the statuses.
Review: none recorded.

### R-2 -- bot rules (native_arcade_bot_rules)

Status: done. Module, unit test with the golden digest, isolation test
(3.1).

- R-2, ae3e6bd1c: the module, its v1 encoding and digest, the 2P bot rule,
  the retail seed mapping and derivation, and the config validator.
  Reviewed: nits. R-2b closed them; the R-4 brief records its 30/1 tick
  rate check (RS-14) as closing one more R-2 review nit.
- R-2b, fb5aff0e1: the derivation versions asserted equal, RNG banks
  compared field by field, a non-base CAB1 human rejected, and the BOTS.c
  fallback pinned inside its exact guard block. Review: none recorded.

### R-3 -- fixture uses the real bot rules

Status: done.

- R-3, 200715b09: botRulesDigest from NativeArcadeBotRules_DigestV1, the
  bots the retail 2P AI set for Crash and Cortex at RS-3 medium (0xA0), and
  a builder that fails unless the bot rules accept the config. Reviewed:
  should-fixes, closed in R-3b.
- R-3b, 22399d37d: the real fixture run through match select and rematch
  for all 56 character pairs and checked against the bot rules, the agreed
  config checked in the host test, tighter options isolation, and stale
  comments fixed. Review: none recorded.

### R-4 -- race setup plan

Status: done. The pure plan MainArcadeRaceSetupPlan (3.2); the seed
derivation the R-1 plan put here landed in R-2
(NativeArcadeBotRules_DeriveRetailSeedsV1).

- R-4, 054895163: build, apply to a pointer-free retail mirror, digest,
  and the gameMode1/gameMode2 bit audit; every non-transient mode bit
  pinned (RS-2, RS-15, RS-16) and the 30/1 tick rate required (RS-14).
  Reviewed: findings, closed in R-4b.
- R-4b, 080bde9b0: PAUSE_1..4 became MODE-clear, the plan check covers the
  bots and the difficulty, the R-5 adapter contract is recorded, all 56
  character pairs are tested, and the isolation is tighter. Review: none
  recorded.

### R-5 -- race setup live adapter

Status: done. Split into:

- R-5a, b4fdd43ec: MainCanonicalDrivers_ExtractRosterInput split out of
  ExtractRosterPrelude with identical results, and the pure facts builder
  MainArcadeRaceSetupFacts with its retail audit. Reviewed: should-fixes,
  closed in R-5a2.
- R-5a2, 4443f3087: one-pass roster extraction (stack back under budget),
  an ExtractRosterInput caller isolation test, the slot-1 bot and tampered
  count cases, and the adapter contract (one pre-drivers hook at the very
  start of MainInit_FinalizeInit) with corrected citations. Review: none
  recorded.
- R-5b, 03b5ca5e4: the live adapter MainArcadeRaceSetup with its two
  MainInit_FinalizeInit hooks, and the minimal internal launcher
  (--arcade-roster-proof). Besides Arm, Launch, Status, Digests, Bank, and
  Disarm it added MainArcadeRaceSetup_StatusName, _FailureName, and
  _SlotFacts, Platform_RequestExit (include/platform.h, the proof's exit
  code), and MainArcadeLinkPolicy_TitleMenuReady (the menu-ready rule the
  arcade-link hook and the proof share). Reviewed: should-fixes and nits,
  closed in R-5c; the review's corrections to this document (3.2, 3.3,
  RS-7: seeding at the very start of MainInit_FinalizeInit, hooks acting
  in LAUNCHED and SEEDED, the LAUNCHED status, no
  MainArcadeSetupV4Context_Init) landed in R-7.
- R-5c, 3bb053d73: every adapter decision moved into the pure, unit-tested
  core MainArcadeRaceSetupCore; the adapter reads the pre-race roster input
  (MainCanonicalDrivers_ExtractRosterInputPreRace), so a launch after an
  earlier race, such as the attract demo race, validates despite the stale
  race order; the proof exits nonzero until PASS and its dwell reaches the
  demo race. Reviewed: findings, closed in R-5d.
- R-5d, fdceaae0e: the seeded fields read back (SEED_MISMATCH), every
  adapter write target pinned to its one retail field, the launch-from-demo
  contract documented, a write-list overflow failing closed (OPS), and the
  untested core failure paths covered. Review: none recorded.

### R-6 -- live roster proof

Status: done. Split into (in commit order):

- R-6, 27e703b66: scripted pads from boot, per-tick V1 control, rng, and
  input digests and the drivers digest (Physics zeroed), and the ctest
  arcade_roster_determinism over four runs. Reviewed: findings, closed in
  R-6b.
- R-6c, ab83ea92f: the RS-17 audit and pins (gGT->timer and
  gGT->frameTimer_Confetti), their readback, and PIN_MISMATCH.
- R-6b, 1bafa6005: proof-only fixed VBlank pacing (RS-18), parallel runs,
  the race-relative control digest, run E, the "live" label, and the R-6
  review nits.
- R-6c and R-6b were reviewed together in R-7: no blockers, four nits.
  NIT 1-3 (an audit citation, missing audit readers, frameTimer_Confetti
  not checked at race tick 0) were closed in R-6d; NIT 4 (the section 4 RS
  numbering) is closed by R-7.
- R-6d, fc9a7b21f: those three nits; report format v7, with
  frameTimerConfetti on both counter lines and the checker requiring C's
  and E's race tick 0 value to equal A's. Re-reviewed: NIT 1-3 closed, no
  blockers, three new nits. Two (comment wording and wrapping) are closed
  in R-6e; one stays open (risk 12).
- R-6e, b9cb4b4e8: comment-only; the race tick 0 frameTimer_Confetti
  comment in MainArcadeRosterProof.c names both writers and why neither
  runs between the control snapshot and the hook, and a checker header line
  is rewrapped. Review: none recorded.

### R-7 -- docs close-out

Status: complete. This document's statuses, RS-14..RS-18, and risks;
docs/HANDOFF.md step 3, Deterministic simulation, build and test, and key
files; the docs/GAME_LOOP_UI_MILESTONE.md fixture, UX-8, risk 6, and Task 7
text. Review: none recorded.

### OC -- single-cabinet race setup (owner override of RS-1)

Status: done. ONE_CAB is supported by the bot rules, the race setup plan,
facts, core, and adapter, and the live roster proof (RS-1, RS-19..RS-24).
The full suite, both live tests included, was verified independently on
eb5ef25ef, 30d5a1c71, and 164e34d2f: 133 of 133 passed each time. On
164e34d2f the suite took about 367 s, arcade_roster_determinism about
265 s with its eight runs in parallel (A-E about 80 s each except C, about
264 s; F-H, then capped at 90 race ticks, about 53 s each). After OC-5
the suite is still 133 tests; on b024a1814 it passed 133 of 133 in about
394 s, arcade_roster_determinism about 265 s with its ten runs in
parallel (C and I about 264 s each, the other eight about 81 s).

- OC-1, eb5ef25ef: the retail 1P arcade bot rule. The 1P V1 encoding and
  Digest1PV1, DigestForProfileV1, ExpectedBots1P, and ValidateConfigV1
  accepting ONE_CAB with its own profile's digest (RS-19, RS-20); the
  TWO_CAB V1 bytes and golden unchanged; the isolation test pins
  LOAD_Robots1P, its 1P call site, and the MainInit_Drivers 1P shape.
  Reviewed: no blockers; the should-fixes were closed in the same commit
  before it landed. Re-review clean, with two optional isolation-test
  hardening nits (risk 15).
- OC-2, 30d5a1c71: ONE_CAB in the race setup plan, facts, and core. Plan
  encoding v2 (RS-21) and the per-profile plan shape with the 12
  MATCH_SETUP draws (RS-22); facts that accept a 1P roster (a human only in
  slot 0) with a 1P retail audit; core comments for both profiles and a
  ONE_CAB end-to-end core test; plan well-formedness that ties the bots to
  the humans. TWO_CAB writes, seeds, and failures unchanged. Reviewed: no
  blockers or should-fixes; the nits were closed in the same commit.
  Re-review clean, with two optional nits (risk 15).
- OC-3, 5e0c71c36: a single-cabinet race from the roster proof.
  `--arcade-roster-proof-profile`, the one-cab config (RS-23) and pads
  (RS-24), report format v8 with the profile line, the adapter's "armed"
  line with the profile and all eight characters, and runs F-H in the
  checker, capped at 90 race ticks (risk 13). Reviewed: no blockers; two
  should-fixes and the nits were closed in OC-3b.
- OC-3b, 164e34d2f: the checker's input-digest checks (F = A at race tick
  0, F != A at every later shared tick, F = H at every tick), the cap
  comment saying F-H stop before the green light, the report's profile
  taken from the configured config, main.c's usage text and startup line
  naming the profile, and the proof test finding the first bot slot the
  way the builder does. Re-review clean, with three optional nits (risk
  15).
- OC-4, 51c964c9c: this document (the intro, sections 1-3, RS-1,
  RS-4, RS-19..RS-24, this subsection, and risks 13-15, plus the
  MainArcadeRaceSetupPlan.c and MainArcadeRaceSetupCore.c line citations
  OC-2 moved) and docs/HANDOFF.md step 3, Deterministic simulation, and
  key files. Reviewed: the should-fixes were closed in OC-4b and the
  header-comment nit in OC-5 (228c38e14).
- OC-4b, 9308fb19f: the OC-4 review should-fixes (ONE_CAB evidence
  scoped; RS-19..RS-24 marked defaults pending owner review). Review:
  none recorded.
- OC-5 -- ONE_CAB full-race proof. Split into (in commit order):
  - a98dccbe8: game/UI/UI_Rank.c initializes pos.y, which the retail 1P
    rank-icon HUD read uninitialized on the transitioning path (risk 13).
    Fixed in place under the owner's standing directive to fix retail
    bugs in place; render-only, no simulation change. Reviewed: no
    blockers or should-fixes on the code (one wording nit, left as is).
  - e491763ed: the checker's -OneCabTicks parameter and 90-tick cap are
    removed, so every run logs -Ticks (900 in ctest), and the new one-cab
    runs I (dwell 5400, the one-cab C) and J (dwell 37, the one-cab E)
    are checked against F exactly as C and E are against A (3.4). Ten
    runs, all parallel. Reviewed: no blockers or should-fixes; the nits
    were closed in b024a1814.
  - b024a1814: those checker and CMake comment nits (a CMake comment
    rewrapped, the R-6c/R-6d citation). Closes the e491763ed review nits;
    no re-review (owner process rule: should-fixes and nits go back
    without re-review).
  - 228c38e14: the code headers call RS-19..RS-24 "defaults pending
    owner review", closing the OC-4 review nit (risk 15). Reviewed
    together with b375e8b86: no blockers; the nits were closed in the
    follow-up below.
  - b375e8b86: this document (the intro, section 2, 3.4, this
    subsection, and risks 1, 13, and 15) and docs/HANDOFF.md step 3,
    Deterministic simulation, and key files. Reviewed: no blockers; the
    nits were closed in the follow-up below.
  - This commit: the b375e8b86 and 228c38e14 review nits (the ONE_CAB
    evidence says the Physics group is not digested, RS-13; review
    statements and the evidence commits in this subsection; RS-17 names
    I and J; two header comments rewrapped). Comments and docs only.

  Evidence (arcade_roster_determinism, Debug, fixed VBlank pacing, 900
  race ticks each, ten runs in parallel). The live test output is from
  e491763ed (implementer run); the full suite passed 133/133 on b024a1814
  (394 s, arcade_roster_determinism 265 s), and b024a1814 and later
  commits are comment/docs-only. Every run exits 0; about 265 s wall (C
  and I about 264 s each, the other eight about 81 s). A = B
  byte-identical (160583 bytes) and F = G byte-identical (160666 bytes);
  C = A, E = A, I = F, and J = F over all 900 ticks. The I-F and J-F
  offsets equal the C-A and E-A offsets (launch timer +5329 and +37; race
  tick 0 timer and frameTimerConfetti 0). D != A, H != F, and F != A as
  before; F's input digest equals A's at tick 0, differs at ticks 1..899,
  and equals H's at all 900 ticks. F reaches race tick 0 at frameCounter
  760, A at 763 (informational; comparisons are within a profile). No
  further retail bug (no run-time check dialog) appeared in 900 ticks of
  the 1P race, so F-J run through the green light, bot driving, and 1P
  race physics, digested through the rng, rcontrol, and topology-free
  drivers digests (the Physics group itself is not digested, RS-13).

## 7. Risks and open questions

1. Menu-history RNG (section 1) is the core cross-cabinet risk; RS-5/RS-7
   close it only at the seeding point, so anything that consumes retail RNG
   between the seeding hook and the first lockstep tick must be identical
   on both cabinets (runs C and E of the live proof test this on one
   machine for TWO_CAB, and their one-cab counterparts I and J for
   ONE_CAB). Both are single-machine evidence; the cross-cabinet case
   stays untested (risk 6).
2. Boot-relative control counters are in the canonical control domain; two
   cabinets never share a boot history (RS-12). Run E of the live proof
   showed that gGT->timer parity feeds the simulation RNG: with an odd
   timer offset (37), sdata->randomNumber first differed from run A at race
   tick 399 on Crash Cove (drivers, input, and rcontrol stayed equal
   through tick 899). The 2P exhaust emitter picks each human driver's
   frames by timer parity (game/Vehicle/VehEmitter.c,
   `(gGT->timer & 1) == d->driverID`), and an exhaust particle that ends
   underwater draws MixRNG_Scramble (game/Particle.c, the bubble pop), so
   the same draws land one frame earlier or later. An even offset (run C,
   +5382) matched. R-6c pins gGT->timer and gGT->frameTimer_Confetti at the
   setup point (RS-17), and run E now matches A. sdata->frameCounter and
   gGT->frameTimer_VsyncCallback stay boot-relative (LEAVE in the RS-17
   audit; the VBlank counter is also not safe to reset), so the full V1
   control digest still differs between cabinets and Task 8 must compare
   race-relative control (as the proof's rcontrol does) or normalize these
   two counters.
3. The V4 runtime derives a fresh bank from the config
   (game/MAIN/MainCanonicalRuntime.c,
   NativeDeterministicRngBankV1_InitInPlace), while the live bank after
   setup has drawn MATCH_SETUP nine times for TWO_CAB and twelve for
   ONE_CAB (five seeds, one setupRandom per bot). Task 8 must project the
   post-setup bank, MainArcadeRaceSetup_Bank() (non-NULL only when
   VALIDATED), not a fresh one.
4. Shared-RNG presentation consumers (particles, VS quips) draw from
   randomNumber; they are deterministic given the simulation, but any
   future per-cabinet presentation (one viewport per cabinet) that changes
   which particles spawn would desync the simulation RNG. Task 7/8 must
   keep the 2P presentation path or migrate those draws.
5. Closed: the dormant validators were written against source-shaped
   facts, and live facts could have exposed assumptions (for example human
   nav-path values). R-5a's retail audit
   (game/MAIN/MainArcadeRaceSetupFacts.h) found that every
   MainArcadeBotSetup range rule holds for retail values; no validator rule
   was corrected, and the live proof validates.
6. The proof is single-machine; the step 7 gate still needs both cabinet
   PCs.
7. Host timing. Natively a late host frame produces extra VBlanks: VSync
   first emits the overdue catch-up VBlanks (Native_CatchUpDueVBlanks,
   platform/native_platform.c) and then waits for its own, so
   gGT->elapsedTimeMS becomes 48 or 64 instead of 32 for that tick. That
   feeds msInThisLEV, elapsedEventTime, trafficLightsTimer, and physics,
   and the extra VBlanks also move gGT->frameTimer_VsyncCallback. Every
   emitted VBlank while not paused also increments gGT->frameTimer_Confetti
   (game/MAIN/MainDrawCb.c:25), which feeds the particle oscillators and
   through them MixRNG draws; RS-17 pins it only at race start, so a
   mid-race host hitch still moves a simulation input. Only the proof pins
   pacing (RS-18) and only V2 playback cancels it (the recorded VSync
   packets and frame elapsed time), so Task 8 must adopt deterministic
   VBlanks per tick for linked races.
8. Closed by Task 7 RL-13 (docs/RACE_LAUNCH_MILESTONE.md RL-S9). The
   retail pause-menu vibration toggle flips a P*_VIBRATE bit in gameMode1
   mid-race, and gameMode1 is canonical control state, so one cabinet
   toggling rumble would diverge the control digest. A CTR_NATIVE guard in
   the case 4-7 rows of PROCESSINPUTS_MainFreeze_MenuPtrOptions
   (game/MAIN/MainFreeze.c, the write at :530) now skips the toggle while
   MainArcadeRaceSetup_Status() is not IDLE (a linked race, or the
   internal roster proof), pinned by
   main_freeze_vibration_guard_isolation. The confirm sound and the
   analog-controller row stay retail (risk 9); with the setup IDLE the
   toggle is retail.
9. data.rwd (the racing wheel calibration, loaded from the saved options,
   game/RaceConfig.c:16) applies only to NeGcon/JogCon pads, and native
   input produces only digital or analog pads, so it cannot reach a linked
   race today. If native input ever produces those pad types, the
   calibration must be pinned or excluded for linked races.
10. Nav-path fallback. When a bot's chosen nav path has fewer than 2
    points, BOTS_Driver_Init (game/BOTS.c) falls back to a lower path. The
    setup then fails closed: MainArcadeBotSetup_Plan reports NAV_MISMATCH,
    the setup latches FAILED/BOT_SETUP, and the adapter logs each nav
    path's numPoints so that a level-data fallback can be told apart from
    tampering. Crash Cove validates; a track with a short path would refuse
    to race rather than desync; no sweep of every track is recorded.
11. A second armed race in one session. R-5c fixed the stale race order:
    a launch after an earlier race (the attract demo race) reads the
    pre-race roster input and validates (run C). A new race init while the
    setup is VALIDATED latches FAILED/STATE (since RL-S8b, except the init
    of the main-menu level), so the owner must Disarm between races.
    Disarm clears the whole state to IDLE, so Arm can run again. Closed by
    Task 7
    (docs/RACE_LAUNCH_MILESTONE.md RL-9): the live race caller
    (game/MAIN/MainArcadeRaceLaunch.c) Disarms once per race on the first
    idle main-menu frame after it, and the RL-S8b seam change makes the
    return load's init in VALIDATED on the main-menu level a no-op instead
    of FAILED/STATE. The adapter still never calls Disarm itself (the
    isolation test forbids it), and the proof still runs one race per
    process. A rematch is a second armed race in one process, proven live
    by arcade_link_launch (race 2 VALIDATED in the same process on both
    cabinets) and by the decision core's two-races unit tests.
12. Open review nit (R-6d re-review): the checker's failure branches for
    the race tick 0 timer pin and frameTimerConfetti have no offline
    fixture-report test; only the live test runs them, and only on the pass
    path.
13. Resolved (OC-5). The retail 1P rank-icon HUD in game/UI/UI_Rank.c
    (UI_DrawRankedDrivers, whose 1P branch only a 1-human race takes) set
    only pos.x while an icon was transitioning and then read pos.y into
    iconPos. The value was dead (UI_Lerp2D_Angular overwrites it), but
    the MSVC Debug runtime stopped any 1P race at the first rank change
    with "Run-Time Check Failure #3", so the one-cab runs were capped at
    90 race ticks, before the green light. It came from upstream
    c6a2a67ff. a98dccbe8 fixed it in place under the owner's standing
    directive to fix retail bugs in place (render-only, no simulation
    change), and F-J now run 900 race ticks (3.4).
14. ONE_CAB has no lobby or UI flow. The fixture, the arcade-link lobby,
    and match select remain TWO_CAB-only (RS-1); the only path that
    launches a race through the ONE_CAB setup is the internal roster proof
    (`--arcade-roster-proof-profile one-cab`, RS-23). Task 7's networked
    launch arms only the agreed config of the TWO_CAB link, so it does not
    change this. A player-facing single-cabinet lobby and UI flow stays a
    follow-up.
15. Open optional review nits. OC-1 re-review: the bot rules isolation
    test does not pin numDrivers between the else-if chain and the spawn
    loop of MainInit_Drivers, and does not count LOAD_Robots1P calls with
    nested parentheses. OC-2 re-review: MainArcadeRaceSetupPlan.h still
    cites game/MAIN/MainInit.c:431, :481, and :567, now :432 (the
    WARPBALL_HELD clear), :482 (MainInit_Drivers), and :568 (the
    boolDemoMode check); and the facts isolation test's rule 7 regex lacks
    a left word boundary. OC-3b re-review: three optional nits in the
    checker and the proof hook.
