# Roster milestone

Design-and-status record for HANDOFF integration step 3, "Stable
two-human-plus-bot roster and RNG ownership", on branch `arcade`. Read
AGENTS.md and docs/HANDOFF.md first. This document follows the same pattern
as docs/MATCH_SELECT_MILESTONE.md: a prospective plan with a task list,
updated to record status as tasks land.

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
2. Given a validated TWO_CAB NativeMatchConfigV1, one seam configures the
   retail race (track, laps, 2 humans, per-slot characters, the retail 2P
   AI set, difficulty, mode and cheat bits) and requests its load.
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

Status (R-7): functionally complete. R-2 through R-6e (section 6) meet
items 1-6 on one machine, and the live roster proof (3.4) is the evidence
for item 5. Networked launch (Task 7), the in-race lockstep drive (Task 8),
and real two-cabinet evidence (steps 6-7) remain.

## 3. Decided design

### 3.1 Bot rules: native_arcade_bot_rules (pure, platform)

platform/native_arcade_bot_rules.c,
include/platform/native_arcade_bot_rules.h, library
ctr_native_arcade_bot_rules. It links exactly ctr_native_match_config,
ctr_native_sha256, ctr_native_canonical_codec,
ctr_native_deterministic_rng, and ctr_native_match_select_rules (for the
2P AI table); no game tokens, no I/O, no heap.

- NATIVE_ARCADE_BOT_RULES_V1 canonical encoding: ASCII tag "CTRN arcade bot
  rules v1" (no NUL), then fixed little-endian fields: rulesVersion (u32 =
  1); supported profile (TWO_CAB) and its 8 slot roles; retail race shape
  (humans 2, drivers 6, first bot slot 2, bots 4); the difficulty table
  {0x50, 0xA0, 0xF0}; the difficulty, bot-character, and mode policies; the
  seven 2P AI sets (28 bytes, from NativeMatchSelect_AiSetRacer); the RNG
  recipe (derivation version, MATCH_SETUP stream tag, the ordered
  seed-target codes of 3.3, the 16-bit randomNumber mask, the advRng
  all-zero fallback constants); the reserved-stream mask (ITEMS, HAZARDS,
  BOT[0..7] undrawn in v1). The offset/size/field table in
  include/platform/native_arcade_bot_rules.h is the authority for the
  layout; the encoding is 111 bytes
  (NATIVE_ARCADE_BOT_RULES_V1_ENCODED_BYTES).
- NativeArcadeBotRules_DigestV1(digest[32]) = SHA-256 of that encoding. The
  golden digest, frozen in tests/native_arcade_bot_rules_test.c, is
  9022154eab793fb25d0a2d3b0c787d62fdaf9af490b7e3f1d48fbec8d4065eab.
- Helpers: difficulty-table lookup/validation; expected 2P bots for two
  human characters (the LOAD_Robots2P rule); the retail seed mapping and
  derivation of 3.3 on a caller-owned bank; and
  NativeArcadeBotRules_ValidateConfigV1, the config check behind RS-1..RS-4.
- Isolation test: pure (bans game, lease, lockstep, replay tokens), the
  link set is exact, the difficulty table mirrors game/230/D230.c, and the
  advRng fallback constants mirror game/BOTS.c.

### 3.2 Race setup: game/MAIN/MainArcadeRaceSetup

As built: three pure standalone libraries (C17, extensions off, linked
into ctr_native, never unity-included) behind one thin live adapter.

- Plan (R-4, game/MAIN/MainArcadeRaceSetupPlan.{c,h}):
  MainArcadeRaceSetupPlan_Build(config, out) fails closed unless
  NativeArcadeBotRules_ValidateConfigV1 accepts the config (RS-1..RS-4: a
  TWO_CAB profile; gameMode1, gameMode2, and rules 0; botRulesDigest equal
  to NativeArcadeBotRules_DigestV1; a match-select table track and lap
  count; distinct human base characters at difficulty 0; bots that follow
  the LOAD_Robots2P rule at one table difficulty), the tick rate is exactly
  30/1 (RS-14), and CAB1 and CAB2 are slots 0 and 1. The plan holds
  levelID, numLaps, numPlyrNextGame 2, the gameMode1 and gameMode2 clear
  and set masks (RS-2: every non-transient bit pinned, ARCADE_MODE set, the
  vibration bits 0, RS-15), arcadeDifficulty (the bots' shared value),
  boolDemoMode 0 (RS-16), characterIDs[0..5] (6-7 untouched, since retail
  never reads them for a 6-driver race), and the expected bots.
  MainArcadeRaceSetupPlan_Apply applies it to a pointer-free mirror of the
  retail fields; MainArcadeRaceSetupPlan_Digest is the SHA-256 of its
  126-byte encoding. The header holds the gameMode1/gameMode2 bit audit and
  the adapter contract.
- Facts (R-5a, game/MAIN/MainArcadeRaceSetupFacts.{c,h}):
  MainArcadeRaceSetupFacts_Build turns a pointer-free snapshot of the race
  after MainInit_Drivers and the pre-race roster input into
  MainArcadeRosterNativeFacts and MainArcadeBotSetupSourceFacts, observed
  only, never copied from the config. The header holds the retail audit of
  the spawn, nav path, and acceleration orders; no dormant validator rule
  needed a correction.
- Decision core (R-5c, game/MAIN/MainArcadeRaceSetupCore.{c,h}): every
  decision of the adapter as a pure step over a pointer-free view of the
  live values: the state checks, the Launch preconditions, the load-field
  verification, the exact ordered list of retail writes, the seed order,
  the fact validation (MainArcadeRoster_BuildPlan and
  MainArcadeRoster_ValidateNativeFacts, then MainArcadeBotSetup_Plan on the
  post-seed bank), and the failure codes. The header holds the state
  machine and the RS-17 counter audit.
- Adapter (R-5b, game/MAIN/MainArcadeRaceSetup.{c,h}, CTR_NATIVE only, in
  the unity chain): reads the view from the retail globals, calls the core,
  applies exactly the writes it returns, in order, and logs one line per
  state change or failure ("arcade race setup:"). It uses MainArcadeRoster
  and MainArcadeBotSetup_Plan directly, not MainArcadeSetupV4Context_Init:
  ctr_native does not link ctr_native_arcade_setup_v4, and neither the
  adapter nor the proof hook names the V4 setup, the V4 projector, or
  MainCanonicalRuntime.

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
  (MainArcadeRaceSetupCore.c:214, checked at :421). After a Launch it
  restores the saved bits only on the idle main-menu level; otherwise it
  leaves them as they are and logs that, since gameMode1 must not change
  under a running race. "As they are" means 0 from the pin unless the
  pause-menu toggle (game/MAIN/MainFreeze.c:518, risk 8) flipped one
  mid-race.

Two CTR_NATIVE hooks in MainInit_FinalizeInit (game/MAIN/MainInit.c):

- MainArcadeRaceSetup_OnFinalizeInitBegin, the very first statement block
  (:425), acts only in LAUNCHED: it verifies levelID (LEVEL_MISMATCH) and
  numLaps, numPlyrCurrGame, and characterIDs[0..5] (LOAD_FIELDS_MISMATCH),
  re-applies the mode words, arcadeDifficulty, and boolDemoMode, pins
  gGT->timer and gGT->frameTimer_Confetti (RS-17), seeds the retail RNG
  states (3.3), and reads the seeds and pins back. SEEDED.
- MainArcadeRaceSetup_OnDriversInitialized, immediately after
  MainInit_Drivers (:486), acts only in SEEDED: it checks that the plan's
  mode bits still hold and no cheat bit is set (FACTS), reads the snapshot
  and the roster input (MainCanonicalDrivers_ExtractRosterInputPreRace:
  the race order is not rebuilt before the first race tick), builds the
  facts, and validates them (ROSTER, BOT_SETUP). On a NAV_MISMATCH it logs
  each nav path's numPoints. VALIDATED, else FAILED; it writes no retail
  field.
- A hook in the wrong state (OnFinalizeInitBegin in SEEDED or VALIDATED,
  OnDriversInitialized in LAUNCHED) latches FAILED/STATE; a hook that
  should act without a game tracker latches FAILED/NO_TRACKER. Every other
  hook call is a no-op, so default boot and every load not launched here
  are unchanged.
- The state is a file-scope static in the adapter, outside every
  checkpoint region, never recorded or canonical (RS-11).
- tests/main_arcade_race_setup_isolation_test.cmake enforces the hook
  placement, that Arm, Launch, and Disarm are named only in
  MainArcadeRaceSetup.{c,h} and MainArcadeRosterProof.c, that each core
  write target stores to its one retail field, that nothing writes
  levelID, and the token bans. The plan, facts, and core have their own
  unit and isolation tests.

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
  (MainArcadeRaceSetup_Bank) has drawn MATCH_SETUP nine times (five seeds,
  four bots). That is the bank Task 8 must project.
- ITEMS, HAZARDS, BOT[0..7] are reserved and undrawn in bot rules v1. Any
  migration of a retail call site to them is a new bot-rules version.
- Presentation RNGs (psxRandSeed, audioRNG) are seeded for cabinet parity
  but stay out of canonical state, as today.

### 3.4 Evidence: live roster proof (internal builds)

- An internal-only option, `--arcade-roster-proof <log path>`, with
  `--arcade-roster-proof-seed <u64>` (default 1),
  `--arcade-roster-proof-dwell <ticks>` (0..7200, default 0), and
  `--arcade-roster-proof-ticks <N>` (1..3600, default 900). main.c builds
  the fixture, resolves it through match select with two fixed choices (the
  fixture characters, track, and laps; nonces seed and seed XOR
  0x9E3779B97F4A7C15), and hands the resolved config to the CTR_NATIVE &&
  CTR_INTERNAL game hook game/MAIN/MainArcadeRosterProof. Options, config
  builder, report writer, and exit-code table live in
  platform/native_arcade_roster_proof.{c,h}. The proof is rejected with any
  arcade-link or replay option and with --exit-after-frame; quick states
  are disabled.
- The hook waits for the title's menu-ready frame
  (MainArcadeLinkPolicy_TitleMenuReady, the rule the arcade-link hook
  uses), then `dwell` ticks, then launches (MainArcadeRaceSetup_Arm and
  _Launch) from the first launch window it sees: the title, or the attract
  demo race. It installs scripted pads for both humans for the whole run
  (Platform_InputInstallPadSnapshots: neutral through race tick 0, then
  both hold CROSS and CAB2 also holds RIGHT for ticks 60-89 of every 120).
  Race tick 0 is the first frame after VALIDATED on which the
  topology-free DRIVERS extraction succeeds. From it the hook logs one line
  per tick: tick, the V1 control, rcontrol, RNG, and input domain digests,
  and the SHA-256 of the topology-free DRIVERS candidate
  (MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta
  through the canonical encoders with Physics zeroed, never raw struct
  bytes). The report header carries the result, the launch window, the
  config, race plan, bot setup plan, and bank digests, the "seeded" line
  (the seeds and pins read back, "match 1"), and one line per slot.
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
  report header (format v7) logs those three counters and
  frameTimerConfetti at the launch tick and at race tick 0.
- The checker, tools/arcade-roster-proof-check.ps1, run by the ctest
  arcade_roster_determinism (Windows only, label "live", 900 race ticks per
  run), starts five proofs:
  - A: seed 0x5EED, dwell 0 (launches from the title);
  - B: A again;
  - C: seed 0x5EED, dwell 5400 (launches from inside the attract demo
    race);
  - D: seed 0x5EEE, dwell 0;
  - E: seed 0x5EED, dwell 37 (launches from the title 37 ticks late).

  It runs them in parallel by default (-Sequential runs them one after
  another) and fails unless:
  - every run exits 0, and its report is format v7 with result PASS, the
    launch window it expects, both counter lines, a seeded line ending
    "match 1", eight slot lines, and exactly the requested tick lines,
    numbered from 0, then "end ticks N" (the proof itself reports PASS
    only when tickLineCount == ticksRequested);
  - A and B are byte-identical;
  - C and E equal A in the config, race plan, bot setup plan, and bank
    digests, the seeded line, the slot lines, and at every tick the rng,
    input, drivers, and rcontrol digests;
  - E's timer offset from A at launch is odd (measured at the launch tick,
    before the RS-17 pin; after it every run reads 0);
  - C's and E's race tick 0 timer and frameTimerConfetti equal A's (the
    RS-17 pins held);
  - D differs from A in the config digest, the bank digest, and the tick 0
    rng digest.

  The full control digest is informational only: it still differs in the
  unpinned boot-relative counters frameCounter and frameTimer. The checker
  prints the C-A and E-A offsets of all four counters at launch and at
  race tick 0, with their values mod 8. It skips (77) when
  assets/ctr-u.bin is absent, no display is available, or the build
  rejects the internal option.

## 4. Defaults for owner review

1. RS-1: Live scope is the TWO_CAB profile only (retail 2P arcade single
   race, 6 drivers). ONE_CAB is rejected by the setup until needed.
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
   through the seam.
3. RS-3: Difficulty stays global, as in retail. Every bot slot carries the
   retail speed value (0x50 easy, 0xA0 medium, 0xF0 hard), all equal; human
   slots carry 0. The arcade-link fixture uses medium (0xA0).
4. RS-4: Bot characters follow the LOAD_Robots2P rule (as SEL-4); retail
   still writes them live, and the fact validation checks agreement.
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
   Bot rules v1 takes the content-identity side; the decision stays with
   the owner.
10. RS-10: A fact-validation failure latches FAILED and logs; step 3 does
    not abort the race itself. Task 7 decides the player-facing response.
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
    (game/MAIN/MainArcadeRaceSetupPlan.c:46-47, against
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
    (MainArcadeRaceSetupCore.c:214, checked at :421); after a Launch it
    restores them only on the idle main-menu level
    (MainArcadeRaceSetupCore.c:421-434), otherwise it leaves them as they
    are and logs that: 0 from the pin, unless the pause-menu toggle
    (game/MAIN/MainFreeze.c:518) flipped one mid-race.
16. RS-16 (R-4): boolDemoMode is pinned to 0
    (MainArcadeRaceSetupPlan.c:66), written at Launch and re-applied by the
    pre-drivers hook. Demo mode would turn every human driver into a bot
    (game/MAIN/MainInit.c:568-573) and run the demo exit.
17. RS-17 (R-6c): At race init, after the load-field verification and
    before the seeds, the setup pins the boot-relative counters that feed
    the race simulation or its RNG: gGT->timer = 0 (exhaust, terrain, warp
    dust, bubble, and flame particles pick frames by it, and those particles
    draw MixRNG) and gGT->frameTimer_Confetti = 0 (particle oscillators).
    The adapter reads both back and the proof checks them (the "seeded"
    line; PIN_MISMATCH); the checker also requires C and E to start race
    tick 0 with A's timer and frameTimerConfetti. sdata->frameCounter and
    gGT->frameTimer_VsyncCallback feed only presentation and the platform
    (the VBlank counter is also not safe to reset: the load queue compares
    it with a stored timestamp), so they stay boot-relative and the full V1
    control digest stays informational. The audit of every counter, its
    readers, and its verdict is in game/MAIN/MainArcadeRaceSetupCore.h.
18. RS-18 (R-6b): Fixed VBlank pacing (Platform_SetFixedVBlankPacing,
    include/platform.h; the pure decision NativeVBlankPacing_Plan,
    platform/native_vblank_pacing.c) is proof-only: main.c turns it on only
    for the roster proof, and every other run keeps the retail-faithful
    catch-up pacing. It is not the linked-race answer: Task 8 must adopt
    deterministic VBlanks per tick (risk 7).

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

## 7. Risks and open questions

1. Menu-history RNG (section 1) is the core cross-cabinet risk; RS-5/RS-7
   close it only at the seeding point, so anything that consumes retail RNG
   between the seeding hook and the first lockstep tick must be identical
   on both cabinets (runs C and E of the live proof test this on one
   machine).
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
   setup has drawn MATCH_SETUP nine times (five seeds, one setupRandom per
   bot). Task 8 must project the post-setup bank,
   MainArcadeRaceSetup_Bank() (non-NULL only when VALIDATED), not a fresh
   one.
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
8. The pause-menu vibration toggle flips a P*_VIBRATE bit in gameMode1
   mid-race (game/MAIN/MainFreeze.c:518), and gameMode1 is canonical
   control state, so one cabinet toggling rumble would diverge the control
   digest. Tasks 7/8 must disable that row in linked races.
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
    setup is VALIDATED latches FAILED/STATE, so the owner must Disarm
    between races. Disarm clears the whole state to IDLE, so Arm can run
    again, but no live path does that yet: nothing calls Disarm (the
    isolation test forbids the adapter from calling it itself), the proof
    runs one race per process, and no test runs two armed races in one
    process. Task 7 owns that sequence.
12. Open review nit (R-6d re-review): the checker's failure branches for
    the race tick 0 timer pin and frameTimerConfetti have no offline
    fixture-report test; only the live test runs them, and only on the pass
    path.
