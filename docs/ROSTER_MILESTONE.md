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
  NativeArcadeLinkFixture_Build) sets difficulty 0 on slots 0-5,
  gameMode1/gameMode2/rules 0, and botRulesDigest = SHA-256("CTRN
  arcade-link fixture bot rules v1"), a placeholder.
- Live canonical evidence today: in internal builds the per-frame V1
  canonical projection (control, retail RNG, input;
  MainCanonicalState_ProjectLive, game/MAIN/MainMain.c) feeds V2 record and
  playback. The V3/V4 runtime (game/MAIN/MainCanonicalRuntime) is dormant;
  full DRIVERS Physics needs a topology snapshot, and topology capture stays
  unwired (the topology lease is retire-only).

### Retail facts this design rests on

Race shape (2P arcade):

- MainInit_Drivers (game/MAIN/MainInit.c:284-402) spawns the humans in
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
| gGT->deadcoed_struct | include/namespace_Main.h:1301; reset to constants every race (game/MAIN/MainInit.c:441-442) before MainInit_Drivers (:466) | particles (MixRNG_Particles, game/Particle.c:1334-1356) | simulation-canonical, reset per race |
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
   validated through MainArcadeRoster / MainArcadeBotSetup
   (MainArcadeSetupV4Context_Init); a mismatch latches a failed status.
5. Evidence: a single-machine proof runs the live game into a configured
   race several times and shows identical canonical digests for the same
   config and inputs, independence from menu history, and divergence for a
   different seed.
6. Everything is dormant unless armed; normal retail boot is unchanged; no
   canonical-state schema or replay format changes.

## 3. Decided design

### 3.1 Bot rules: native_arcade_bot_rules (pure, platform)

platform/native_arcade_bot_rules.c,
include/platform/native_arcade_bot_rules.h, library
ctr_native_arcade_bot_rules (links match_config, sha256, and
match_select_rules for the 2P AI table; no game tokens, no I/O, no heap).

- NATIVE_ARCADE_BOT_RULES_V1 canonical encoding: ASCII tag "CTRN arcade bot
  rules v1" (no NUL), then fixed little-endian fields: rulesVersion (u32 =
  1); supported profile (TWO_CAB) and its 8 slot roles; retail race shape
  (humans 2, drivers 6, bot slots 2..5); the difficulty table {0x50, 0xA0,
  0xF0}; the seven 2P AI sets (28 bytes, from NativeMatchSelect_AiSetRacer);
  the RNG recipe (derivation version, MATCH_SETUP stream tag, the ordered
  seed-target codes of 3.3, the advRng all-zero fallback constants); the
  reserved-stream mask (ITEMS, HAZARDS, BOT[0..7] undrawn in v1).
- NativeArcadeBotRules_DigestV1(digest[32]) = SHA-256 of that encoding. A
  golden digest is frozen in the unit test.
- Helpers: difficulty-table lookup/validation; expected 2P bots for two
  human characters (the LOAD_Robots2P rule); the retail seed derivation of
  3.3 on a caller-owned bank.
- Isolation test: pure (bans game, lease, lockstep, replay tokens), and the
  difficulty table mirrors game/230/D230.c.

### 3.2 Race setup: game/MAIN/MainArcadeRaceSetup

A pure core plus a thin live adapter.

- Pure core: MainArcadeRaceSetup_Plan(config, out) validates a TWO_CAB
  config (see RS-1..RS-4) and produces the retail field values: levelID,
  numLaps, numPlyrNextGame 2, gameMode1 (ARCADE_MODE with the other mode
  bits cleared, as the retail menu does), the gameMode2 bits to clear
  (every cheat and cup bit), arcadeDifficulty, characterIDs[0..5] (6-7
  unchanged, since retail never reads them for a 6-driver race), and the
  expected bots. It fails closed on: a non-TWO_CAB profile; nonzero
  gameMode1/gameMode2/rules in the config; a botRulesDigest different from
  NativeArcadeBotRules_DigestV1; a bot difficulty outside the table or bots
  with unequal difficulty; a nonzero human difficulty; bot characters that
  differ from the LOAD_Robots2P rule; a track or lap count outside the
  match-select tables.
- Seam for Task 7 (and the proof): MainArcadeRaceSetup_Arm(config) runs the
  plan and derives the bank (NativeDeterministicRngBankV1_Init from
  masterSeed); MainArcadeRaceSetup_Launch() writes the retail fields and
  calls MainRaceTrack_RequestLoad; MainArcadeRaceSetup_Status() reports
  IDLE / ARMED / SEEDED / VALIDATED / FAILED; MainArcadeRaceSetup_Disarm()
  returns to IDLE. State is a game-owned static, excluded from checkpoints.
- Two CTR_NATIVE hooks in MainInit_FinalizeInit
  (game/MAIN/MainInit.c:404-466), no-ops unless ARMED:
  - before MainInit_Drivers (after the retail deadcoed reset): seed the
    retail RNG states (3.3) -> SEEDED;
  - after MainInit_Drivers: read the pointer-free facts
    (MainArcadeRosterNativeFacts, MainArcadeBotSetupSourceFacts) from
    gGT/sdata/data and run MainArcadeSetupV4Context_Init with the post-seed
    bank -> VALIDATED, else FAILED (logged). The locked setup plan digest
    and bank digest are readable.
- Nothing else in game/ may arm the setup; an isolation test enforces the
  hook placement, the single arming caller set, and the token bans.

### 3.3 RNG ownership

- Retail RNGs remain the in-race simulation RNG: no retail call site is
  migrated to the bank (RS-5). Identical call order on both cabinets comes
  from lockstep running the same simulation on the same inputs, and is
  proven by the two-run evidence (3.4).
- At the SEEDED hook the setup draws from the bank's MATCH_SETUP stream
  (global slot), in this fixed order: randomNumber = draw & 0xFFFF;
  advRng.state0 = draw; advRng.state1 = draw (both zero -> the retail
  constants 0x30215400 / 0x493583fe); psxRandSeed = draw; audioRNG = draw.
  deadcoed stays retail (reset per race just before the hook).
- MainArcadeBotSetup's per-bot setupRandom draws follow on the same stream
  after the seeds, in ascending stable slot, so the bank handed to V4 is
  the post-setup bank.
- ITEMS, HAZARDS, BOT[0..7] are reserved and undrawn in bot rules v1. Any
  migration of a retail call site to them is a new bot-rules version.
- Presentation RNGs (psxRandSeed, audioRNG) are seeded for cabinet parity
  but stay out of canonical state, as today.

### 3.4 Evidence: live roster proof (internal builds)

- An internal-only option, `--arcade-roster-proof <log path>`, with
  `--arcade-roster-proof-seed <u64>` and
  `--arcade-roster-proof-dwell <ticks>`. main.c builds the fixture,
  resolves it through match select with two fixed choices (the fixture
  characters, track, and laps; nonces derived from the proof seed), and
  hands the resolved config to a thin CTR_NATIVE && CTR_INTERNAL game hook.
  Rejected with any arcade-link or replay option; quick states disabled.
- The hook waits for the menu-ready frame plus `dwell` ticks, arms and
  launches, installs scripted pads for both humans for the whole run
  (Platform_InputInstallPadSnapshots; nothing before the first race tick,
  then both hold accelerate), and from the first race tick after VALIDATED
  logs one line per tick for a fixed tick count: tick, V1 control, RNG, and
  input domain digests, and the digest of the topology-free DRIVERS
  candidate
  (MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta
  through the canonical encoders, never raw struct bytes); plus the setup
  plan and bank digests once. It exits 0 after the last tick, nonzero on
  FAILED.
- A ctest runs the exe: A and B (same seed, dwell 0) must produce
  byte-identical logs; C (same seed, a different dwell) must match A in
  every RNG, input, DRIVERS, plan, and bank digest (control may differ only
  in boot-relative counters, which the check reports); D (another seed)
  must differ from A in the RNG digest at tick 0 and in the bank digest. It
  skips when assets/ctr-u.bin is absent, no display is available, or the
  build rejects the internal option.

## 4. Defaults for owner review

1. RS-1: Live scope is the TWO_CAB profile only (retail 2P arcade single
   race, 6 drivers). ONE_CAB is rejected by the setup until needed.
2. RS-2: The config's gameMode1, gameMode2, and rules stay 0 and mean
   "retail arcade single race, no cheats, no cup"; the setup writes the
   retail mode bits and clears every cheat and cup bit. Nonzero values are
   reserved for future modes and rejected. This also closes the Task 7
   cheat-reset item (GAME_LOOP_UI risk 8) once Task 7 launches through the
   seam.
3. RS-3: Difficulty stays global, as in retail. Every bot slot carries the
   retail speed value (0x50 easy, 0xA0 medium, 0xF0 hard), all equal; human
   slots carry 0. The arcade-link fixture uses medium (0xA0).
4. RS-4: Bot characters follow the LOAD_Robots2P rule (as SEL-4); retail
   still writes them live, and the fact validation checks agreement.
5. RS-5: Retail RNGs stay the in-race simulation RNG; nothing migrates to
   the bank in v1.
6. RS-6: The bank's MATCH_SETUP stream owns the retail seeds, then the
   per-bot setupRandom draws. ITEMS, HAZARDS, and BOT[0..7] are reserved.
7. RS-7: Seed mapping and order as in 3.3; deadcoed stays retail.
8. RS-8: Presentation RNGs (psxRandSeed, audioRNG) are seeded for cabinet
   parity but stay non-canonical.
9. RS-9: The bot-rules digest covers native rule choices only. Retail
   per-track difficulty tables and nav data are covered by build and
   content identity, not re-hashed (UNVERIFIED: described as the M3
   package's deferred "normalized nav semantics" decision; no record of
   that decision was found in the tree or its history).
10. RS-10: A fact-validation failure latches FAILED and logs; step 3 does
    not abort the race itself. Task 7 decides the player-facing response.
11. RS-11: Setup state is game-owned static, never checkpointed, replayed,
    or canonical; quick states are disabled in proof mode.
12. RS-12: Boot-relative control counters are not altered by the setup in
    v1. If the dwell run shows they feed the simulation, resetting them at
    the setup point becomes its own reviewed task.
13. RS-13: The proof digests V1 control/RNG/input plus the topology-free
    DRIVERS candidate; Physics, WORLD, TOPOLOGY, and live V4 projection
    stay with Task 8.

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
269afbd5b).

### R-1 -- this document

Status: in progress.

### R-2 -- bot rules (native_arcade_bot_rules)

Module, unit test with a golden digest, isolation test (3.1). Review.

### R-3 -- fixture uses the real bot rules

NativeArcadeLinkFixture_Build: botRulesDigest from
NativeArcadeBotRules_DigestV1; bot slots at RS-3 medium. Update the fixture
tests and isolation test. Review.

### R-4 -- race setup pure core

MainArcadeRaceSetup_Plan and the seed derivation onto a pointer-free RNG
value; unit test; isolation test. Review.

### R-5 -- race setup live adapter

Arm / Launch / Status / Disarm, the two MainInit_FinalizeInit hooks, fact
extraction, fail-closed validation; unit and isolation tests. Review.

### R-6 -- live roster proof

The internal option, hook, scripted pads, digest log, and ctest (3.4).
Review.

### R-7 -- docs close-out

This document's statuses; docs/HANDOFF.md step 3 and Deterministic
simulation; the docs/GAME_LOOP_UI_MILESTONE.md botRulesDigest notes.

## 7. Risks and open questions

1. Menu-history RNG (section 1) is the core cross-cabinet risk; RS-5/RS-7
   close it only at the seeding point, so anything that consumes retail RNG
   between the seeding hook and the first lockstep tick must be identical
   on both cabinets (the dwell run tests this on one machine).
2. Boot-relative control counters (gGT->timer, sdata->frameCounter and
   similar) are in the canonical control domain; two cabinets never share a
   boot history, so Task 8 must either compare race-relative control or
   normalize these counters (RS-12).
3. The V4 runtime derives a fresh bank from the config
   (game/MAIN/MainCanonicalRuntime.c,
   NativeDeterministicRngBankV1_InitInPlace), while the live bank after
   setup has drawn MATCH_SETUP values. Task 8 must project the post-setup
   bank (MainArcadeSetupV4 already carries it).
4. Shared-RNG presentation consumers (particles, VS quips) draw from
   randomNumber; they are deterministic given the simulation, but any
   future per-cabinet presentation (one viewport per cabinet) that changes
   which particles spawn would desync the simulation RNG. Task 7/8 must
   keep the 2P presentation path or migrate those draws.
5. The dormant validators were written against source-shaped facts; live
   facts may expose assumptions (for example human nav-path values) that
   need a reviewed correction in R-5.
6. The proof is single-machine; the step 7 gate still needs both cabinet
   PCs.
