#ifndef MAIN_ARCADE_RACE_SETUP_PLAN_H
#define MAIN_ARCADE_RACE_SETUP_PLAN_H

#include "platform/native_arcade_bot_rules.h"
#include "platform/native_match_config.h"
#include "platform/native_sha256.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Race setup pure core (docs/ROSTER_MILESTONE.md section 3.2, task R-4; both
 * profiles since OC-2). Turns a validated ARCADE_TWO_CAB or ARCADE_ONE_CAB
 * NativeMatchConfigV1 into the values of the retail race-defining fields and
 * applies them to a pointer-free mirror of those fields. Not in
 * game/game_unity.h; its one live caller is the race setup
 * decision core (game/MAIN/MainArcadeRaceSetupCore.c, R-5c), on behalf of the
 * R-5b adapter (game/MAIN/MainArcadeRaceSetup.c), which copies the mirror
 * from the retail globals, applies the core's writes, and requests the load
 * (levelID goes through
 * MainRaceTrack_RequestLoad, which is what writes the retail levelID in
 * LOAD_LevelFile, game/LOAD/LOAD_Level.c:42-43).
 *
 * Pure: reads no game global, does no I/O, uses no heap, keeps no hidden
 * state. Every int function returns 1 on success, or 0 with every output
 * untouched on NULL arguments or invalid input.
 *
 * The retail headers need the retail common.h, so the GameMode1, GameMode2,
 * and ADVENTURE_BOSS values used here are mirrored below;
 * tests/main_arcade_race_setup_plan_isolation_test.cmake checks every mirror
 * against include/namespace_Main.h, and the field widths against
 * include/namespace_Main.h and include/regionsEXE.h.
 *
 * ---------------------------------------------------------------------------
 * Audit (R-4): every gameMode1 and gameMode2 bit on the retail Arcade ->
 * Single Race -> 2 players -> difficulty -> characters -> track -> laps path.
 * The audit is profile-independent: the retail 1-player path runs the same
 * menu writes and differs only in numPlyrNextGame (row + 1 = 1, :345), so the
 * mode masks below serve ARCADE_ONE_CAB unchanged.
 *
 * The retail menu path writes, in order: gameMode1 &= ~(BATTLE_MODE |
 * ADVENTURE_MODE | TIME_TRIAL | ADVENTURE_ARENA | ARCADE_MODE | ADVENTURE_CUP),
 * gameMode2 &= ~CUP_ANY_KIND, numLaps = 3 (1 with CHEAT_ONELAP)
 * (game/230/MM_MenuFlow.c:152-162, :218-221), gameMode1 |= ARCADE_MODE (:224);
 * gameMode2 &= ~CUP_ANY_KIND for Single Race (:569); numPlyrNextGame = row + 1
 * (:345); arcadeDifficulty = cupDifficulty speed[row] (:532); the character
 * grid picks into characterIDs[0..3] (game/230/MM_Characters.c:1391, overlap
 * fix :701); currLEV and numLaps from the lap row, 1 with CHEAT_ONELAP
 * (game/230/MM_TrackSelect.c:862, :773, :787); then QueueLoadTrack_MenuProc clears POINT_LIMIT | LIFE_LIMIT |
 * TIME_LIMIT, resets originalEventTime, and requests currLEV
 * (game/QueueLoadTrack.c:25-31). It clears no cheat bit (only the Adventure
 * and Time Trial rows clear five of them, MM_MenuFlow.c:184-188, :205-209).
 *
 * Classes: MODE = the plan sets or clears it; CHEAT/CUP = the plan clears it;
 * HOST-LOCAL = a per-cabinet setting, pinned by the plan to a fixed value;
 * TRANSIENT = owned by the load or race path, left alone by the plan and
 * recomputed before the race reads it. "sim" = a race-simulation reader.
 *
 * gameMode1 (include/namespace_Main.h:4-40, ADVENTURE_BOSS :40)
 * bit        name               class      evidence
 * 0x00000001 PAUSE_1            MODE clear set only by MainFreeze.c:1211 (which returns
 *                                          first at MAIN_MENU_LEVEL and in demo,
 *                                          :1181-1194); cleared only by unpause
 *                                          (MainFrame.c:397, MainFreeze.c:811, :952,
 *                                          :987, :1012, :1032, :1049, :1068); no load
 *                                          or race path resets it
 * 0x00000002 PAUSE_2            MODE clear no writer in game/ (MainFrame.c:466 tests
 *                                          gameModeEnd, not gameMode1)
 * 0x00000004 PAUSE_3            MODE clear no named reader or writer in game/
 * 0x00000008 PAUSE_4            MODE clear no named reader or writer in game/
 *                                          PAUSE_1..4: no load or race path resets
 *                                          them, and the simulation reads all four
 *                                          through PAUSE_ALL (sim MainFrame.c:134,
 *                                          MainMain.c:343, CAM.c:1814, GAMEPAD.c:705)
 * 0x00000010 DEBUG_MENU         MODE clear no writer in game/; sim Particle.c:521,
 *                                          MainFrame.c:213, :296
 * 0x00000020 BATTLE_MODE        MODE clear retail clears MM_MenuFlow.c:152; sim BOTS.c:388,
 *                                          RB_Player.c:7, MainGameEnd.c:222
 * 0x00000040 START_OF_RACE      TRANSIENT  set or cleared every race init
 *                                          MainGameStart.c:14-34; CAM.c:1641 clears
 * 0x00000080 (unnamed)          MODE clear no named reader or writer
 * 0x00000100 P1_VIBRATE         HOST-LOCAL options (RaceConfig.c:19, memory card) and the
 * 0x00000200 P2_VIBRATE         HOST-LOCAL pause menu (MainFreeze.c:518); a set bit
 * 0x00000400 P3_VIBRATE         HOST-LOCAL disables rumble (GAMEPAD.c:1002, :1033, :1064,
 * 0x00000800 P4_VIBRATE         HOST-LOCAL output only); pinned to 0, the retail default
 * 0x00001000 WARPBALL_HELD      TRANSIENT  cleared at race init MainInit.c:431; sim
 *                                          VehPhysGeneral.c:1919, :1927
 * 0x00002000 MAIN_MENU          TRANSIENT  LOAD_TenStages.c:128 clears, :144 sets for
 *                                          menu levels only; sim BOTS.c:320
 * 0x00004000 POINT_LIMIT        MODE clear retail clears QueueLoadTrack.c:28; set
 * 0x00008000 LIFE_LIMIT         MODE clear MM_Battle.c:544-553; sim VehPickState.c:324,
 * 0x00010000 TIME_LIMIT         MODE clear :343, RB_Player.c:22, :40, :67, MainGameEnd.c:556
 * 0x00020000 TIME_TRIAL         MODE clear retail clears MM_MenuFlow.c:152; sim BOTS.c:341,
 *                                          MainInit.c:390, MainGameEnd.c:43
 * 0x00040000 BETA_UNLIMITED     MODE clear no reader or writer in game/
 * 0x00080000 ADVENTURE_MODE     MODE clear retail clears MM_MenuFlow.c:152; sim BOTS.c:357,
 *                                          MainInit.c:331
 * 0x00100000 ADVENTURE_ARENA    MODE clear retail clears MM_MenuFlow.c:152 (and
 *                                          LOAD_TenStages.c:128); sim MainInit.c:184
 * 0x00200000 END_OF_RACE        TRANSIENT  cleared LOAD_TenStages.c:128 and
 *                                          MainGameStart.c:34; set MainGameEnd.c:546;
 *                                          sim VehPhysProc.c:775
 * 0x00400000 ARCADE_MODE        MODE set   retail MM_MenuFlow.c:224; sim BOTS.c:219,
 *                                          MainInit.c:331, PlayLevel.c:433-437
 * 0x00800000 ROLLING_ITEM       MODE clear set in race RB_Crate.c:203, cleared only at race
 *                                          end or by the HUD (MainGameEnd.c:541,
 *                                          UI_RenderFrame.c:879, :1058), so a quit
 *                                          race can leave it; sim RB_Crate.c:200
 * 0x01000000 AKU_SONG           MODE clear set on mask pickup VehPickupItem.c:287, :308,
 * 0x02000000 UKA_SONG           MODE clear cleared by audio HOWL_AudioState.c:190; a
 *                                          quit race can leave them
 * 0x04000000 RELIC_RACE         MODE clear set AH_WarpPad.c:158, :597; the menu and the
 *                                          pause quit (MainFreeze.c:802-808) do not
 *                                          clear it; sim RB_Teeth.c:8, BOTS.c:341, :1210
 * 0x08000000 CRYSTAL_CHALLENGE  MODE clear set AH_WarpPad.c:612; sim RB_GenericMine.c:18,
 *                                          VehPhysGeneral.c:1709, BOTS.c:345
 * 0x10000000 ADVENTURE_CUP      MODE clear retail clears MM_MenuFlow.c:152; sim BOTS.c:230
 * 0x20000000 GAME_CUTSCENE      TRANSIENT  LOAD_TenStages.c:128 clears, :134-165 sets for
 *                                          cutscene levels only; sim BOTS.c:320
 * 0x40000000 LOADING            TRANSIENT  MainMain.c:222 sets, :258, :327 clear
 * 0x80000000 ADVENTURE_BOSS     MODE clear set AH_Garage.c:341; not cleared by the menu or
 *                                          the pause quit; sim BOTS.c:208, :260, :349,
 *                                          RB_MinePool.c:20, RB_Plant.c:274
 *
 * gameMode2 (include/namespace_Main.h:185-221)
 * 0x00000001 SPAWN_AT_BOSS      MODE clear 222.c:582, MainFreeze.c:1081; sim VehBirth.c:160,
 * 0x00000002 SPAWN_RETAINED_UNK MODE clear :352; cleared by VehBirth.c:334, :377
 * 0x00000004 VEH_FREEZE_PODIUM  MODE clear set CS_Podium.c:683; sim VehPhysProc.c:690
 * 0x00000008 TOKEN_RACE         MODE clear set AH_WarpPad.c:153; sim INSTANCE.c:379
 * 0x00000010 CUP_ANY_KIND       CHEAT/CUP  retail clears MM_MenuFlow.c:155, :569; sim
 *                                          BOTS.c:303, :330, :396, :417, :1226
 * 0x00000020 LEV_SWAP           TRANSIENT  LOAD_TenStages.c:129 clears, :139, :159 set
 * 0x00000040 (unnamed)          MODE clear no named reader or writer
 * 0x00000080 CREDITS            TRANSIENT  LOAD_TenStages.c:129 clears, :166 sets
 * 0x00000100 NO_LEV_INSTANCE    TRANSIENT  LOAD_TenStages.c:129 clears, :578 sets
 * 0x00000200 CHEAT_WUMPA        CHEAT/CUP  set MM_CheatCodes.c:5; sim VehBirth.c:498
 * 0x00000400 CHEAT_MASK         CHEAT/CUP  set MM_CheatCodes.c:65; sim VehBirth.c:507
 * 0x00000800 CHEAT_TURBO        CHEAT/CUP  set MM_CheatCodes.c:71; sim VehBirth.c:512
 * 0x00001000 CUP_NEW_WIN        CHEAT/CUP  cleared CS_Camera.c:257; read UI_CupStandings.c:704
 * 0x00002000 CUP_NEW_BATTLE     CHEAT/CUP  cleared CS_Camera.c:257; read UI_CupStandings.c:731
 * 0x00004000 VEH_FREEZE_DOOR    MODE clear cleared VehBirth.c:63, AH_MaskHint.c:477; set
 *                                          VehBirth.c:169; sim VehPhysProc.c:690
 * 0x00008000 CHEAT_INVISIBLE    CHEAT/CUP  set MM_CheatCodes.c:77; sim VehPhysProc.c:651
 * 0x00010000 CHEAT_ENGINE       CHEAT/CUP  set MM_CheatCodes.c:83; sim VehPhysProc.c:356
 * 0x00020000 GARAGE_OSK         MODE clear set by cutscene script R233.c:2573; cleared
 *                                          CS_Garage.c:65; garage UI only
 * 0x00040000 CHEAT_ADV          CHEAT/CUP  set MM_CheatCodes.c:95; sim BOTS.c:240
 * 0x00080000 CHEAT_ICY          CHEAT/CUP  set MM_CheatCodes.c:107; sim COLL.c:1476
 * 0x00100000 CHEAT_TURBOPAD     CHEAT/CUP  set MM_CheatCodes.c:113; sim VehPhysForce.c:1012
 * 0x00200000 CHEAT_SUPERHARD    CHEAT/CUP  set MM_CheatCodes.c:101; sim BOTS.c:223
 * 0x00400000 CHEAT_BOMBS        CHEAT/CUP  set MM_CheatCodes.c:89; sim VehBirth.c:517
 * 0x00800000 CHEAT_ONELAP       CHEAT/CUP  set MM_CheatCodes.c:119; the menu turns it into
 *                                          numLaps 1 (MM_TrackSelect.c:787), which the
 *                                          plan overwrites
 * 0x01000000 INC_RELIC          MODE clear set CS_Podium.c:587, cleared :479; read
 * 0x02000000 INC_KEY            MODE clear UI_DrawNum.c:78, :95, :112
 * 0x04000000 INC_TROPHY         MODE clear
 * 0x08000000 CHEAT_TURBOCOUNT   CHEAT/CUP  set MM_CheatCodes.c:125; read UI_RenderFrame.c:567
 * 0x10000000 LNG_CHANGE         MODE clear no reader or writer in game/
 * 0x20000000 (unnamed)          MODE clear no named reader or writer (cutscene opcodes
 * 0x40000000 (unnamed)          MODE clear can OR any bits, CS_Thread.c:956, :964; the
 * 0x80000000 (unnamed)          MODE clear only one in the tree is GARAGE_OSK)
 *
 * So the plan owns every bit of both words except the TRANSIENT ones:
 * gameMode1 = (gameMode1 & TRANSIENT) | ARCADE_MODE, and
 * gameMode2 = gameMode2 & TRANSIENT. Both words are in the canonical control
 * domain (game/MAIN/MainMain.c:75-76), so pinning every non-transient bit
 * also makes them equal across cabinets. TRANSIENT bits are recomputed by
 * the load (LOAD_TenStages.c:128-129) and race init (MainGameStart.c:14-34,
 * MainInit.c:431) before the race reads them, or are never set here.
 *
 * Other race-defining fields on this path:
 * - levelID, numLaps, numPlyrNextGame, arcadeDifficulty, and the
 *   characterIDs of the profile's characterWriteMask: owned.
 *   ARCADE_TWO_CAB owns characterIDs[0..5]; [2..5] are rewritten by
 *   LOAD_Robots2P (game/LOAD/LOAD_Assets.c:21-59) with the same values
 *   (RS-4), and [6..7] are untouched (a 6-driver race never reads them).
 *   ARCADE_ONE_CAB owns all eight: [0] is the human, and [1..7] are exactly
 *   what LOAD_Robots1P (LOAD_Assets.c:61-76) writes for that human
 *   (NativeArcadeBotRules_ExpectedBots1P, RS-20). On the 1P arcade path the
 *   load always runs it: for a track level, load stage 0 clears MAIN_MENU,
 *   GAME_CUTSCENE, ADVENTURE_ARENA (gameMode1) and CREDITS (gameMode2)
 *   (game/LOAD/LOAD_TenStages.c:128-129) and sets none of them back (the
 *   sets at :131-166 apply only to the ndi, ending, intro, screen, garage,
 *   hub, and credit level names), and with TIME_TRIAL and RELIC_RACE clear and
 *   numPlyrCurrGame 1 (:102) it picks levelLOD 1P (:184-187), all before
 *   load stage 4 calls LOAD_DriverMPK (:316). There the call
 *   LOAD_Robots1P(characterIDs[0]) (LOAD_Assets.c:150-153) is skipped only
 *   when gameMode1 masked with TIME_TRIAL | MAIN_MENU is exactly MAIN_MENU,
 *   which stage 0 has ruled out. The other 1P-only branches before it are
 *   unreachable: TIME_TRIAL (:105-108), ADVENTURE_BOSS (:124-127), and the
 *   ADVENTURE_CUP purple gem cup (:129-148) test mode bits the plan clears;
 *   the cutscene/arena/credits/garage branch (:110-122) tests the bits stage
 *   0 cleared and levelID ADVENTURE_GARAGE, which a match-select table track
 *   never is. No unowned race-defining field differs between the 1P and 2P
 *   arcade paths: the menu writes are the same (the audit above), and what
 *   differs downstream (numPlyrCurrGame, levelLOD, numDrivers, the bots)
 *   the load and race init derive from the owned numPlyrNextGame and
 *   characterIDs. MainInit_Drivers (game/MAIN/MainInit.c:288-406) then gives
 *   numPlyrCurrGame 1 in arcade numDrivers = 8 (:352-355) and calls
 *   BOTS_Driver_Init(i) for i = 1..7 (:363-366), which read characterIDs[1..7].
 * - boolDemoMode (char, include/namespace_Main.h:571): added and pinned to 0.
 *   The demo path sets it (MM_Title.c:167) and the race reads it
 *   (MainInit.c:567 converts every human driver to a bot; BOTS.c:1022,
 *   GAMEPAD.c:705); the arcade-link screens also run over a demo race
 *   (MainArcadeLink.c:239-245).
 * - Not owned: currLEV (only carries the menu's track to the load request,
 *   QueueLoadTrack.c:31); originalEventTime (read only by the battle and
 *   crystal limit clock, UI_Clock.c:453); the cup fields (read only under
 *   ADVENTURE_CUP or CUP_ANY_KIND, BOTS.c:230-253, :330, :396-438,
 *   LOAD_Assets.c:134, which the plan clears); numPlyrCurrGame and
 *   numBotsNextGame (set by the load and bot init, LOAD_TenStages.c:102,
 *   BOTS.c:328).
 *
 * Owner-accepted defaults: RS-14, the plan requires the retail 30 Hz
 * tick rate (tickRateNumerator/tickRateDenominator exactly 30/1); vibration
 * pinned to 0 (rumble enabled for every pad, the retail default without a
 * memory card), so a cabinet's saved rumble preference does not apply to a
 * linked race; boolDemoMode pinned to 0.
 * ---------------------------------------------------------------------------
 * Owner-accepted defaults (OC-2; docs/ROSTER_MILESTONE.md section 4
 * takes them over):
 *
 * RS-21: race setup plan encoding v2, tag "CTRN arcade race setup plan v2"
 * (30 bytes, the same length as v1), for BOTH profiles. v2 adds the uint32
 * profile and the uint8 firstBotSlot and botCount, and widens expectedBots to
 * NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT (7) entries, the unused tail 0.
 * ARCADE_TWO_CAB field values are unchanged; its plan digest changes only
 * because the encoding version (tag and layout) changes. The plan digest is
 * proof evidence only: it is not canonical state, not replayed, and never
 * sent on the network.
 *
 * RS-22: the per-profile plan shape.
 * - ARCADE_TWO_CAB: numPlyrNextGame 2, characterWriteMask 0x3F (slots 0..5),
 *   firstBotSlot 2, botCount 4, aiSetIndex the retail 2P AI set of the bots
 *   (below AI_SET_COUNT), characterIDs[6..7] 0, expectedBots[4..6] 0.
 * - ARCADE_ONE_CAB: numPlyrNextGame 1, characterWriteMask 0xFF (all eight
 *   characterIDs: slot 0 the human, 1..7 the LOAD_Robots1P bots),
 *   firstBotSlot 1, botCount 7, aiSetIndex AI_SET_NONE (0xFF: 1P uses no AI
 *   set).
 * The post-setup bank of an ARCADE_ONE_CAB race has drawn MATCH_SETUP 12
 * times (the 5 retail seeds, then one setupRandom per bot, 7 bots), versus 9
 * for ARCADE_TWO_CAB.
 * ---------------------------------------------------------------------------
 * Contract for the R-5 live adapter (game/MAIN/MainArcadeRaceSetup):
 * - Launch runs the first Apply on a snapshot of the live fields before
 *   MainRaceTrack_RequestLoad and writes back every owned field except
 *   levelID: the adapter never writes levelID into gGT while the old level
 *   runs; the load request carries it and LOAD_LevelFile writes it
 *   (game/LOAD/LOAD_Level.c:42-43).
 * - ONE pre-drivers hook, at the very start of MainInit_FinalizeInit
 *   (game/MAIN/MainInit.c, before the WARPBALL_HELD clear at :431 and so
 *   before MainInit_Drivers at :481):
 *   - VERIFIES the fields the load consumed and fails closed on a mismatch:
 *     levelID and numLaps equal the plan, numPlyrCurrGame equals the plan's
 *     numPlyrNextGame (2 for ARCADE_TWO_CAB, 1 for ARCADE_ONE_CAB; the load
 *     copies numPlyrNextGame, LOAD_TenStages.c:102), and the characterIDs of
 *     the plan's characterWriteMask slots equal the plan, which holds the
 *     bots exactly as the load wrote them: LOAD_Robots2P for ARCADE_TWO_CAB
 *     (LOAD_Assets.c:21-59, RS-4, slots 0..5), LOAD_Robots1P for
 *     ARCADE_ONE_CAB (LOAD_Assets.c:61-76, RS-20, slots 0..7);
 *   - RE-APPLIES only the mode words, arcadeDifficulty, and boolDemoMode. The
 *     demo race behind the link screens keeps running until LOADING and can
 *     set ROLLING_ITEM and AKU_SONG/UKA_SONG, and the pending OnBegin mode
 *     bits (Loading.OnBegin.AddBitsConfig0/8, RemBitsConfig0/8) are ORed into
 *     and masked out of both words when the load starts
 *     (game/MAIN/MainMain.c:270-290), so only this re-apply fixes the words
 *     the race reads;
 *   - SEEDS the retail RNG states from the bank
 *     (NativeArcadeBotRules_DeriveRetailSeedsV1).
 * - A post-drivers hook, right after MainInit_Drivers, validates the facts
 *   (the R-5a facts builder, then the roster and bot setup validators).
 * - The adapter saves the cabinet's vibration bits (P1..P4_VIBRATE) at Arm
 *   and restores them at Disarm; RaceConfig_LoadGameOptions ORs the saved
 *   bits in once (RaceConfig.c:19).
 * - Launch preconditions: sdata->Loading.stage is LOAD_IDLE; all four pending
 *   OnBegin mode words are 0; sdata->boolHasLoadedOptions != 0 (so the
 *   one-time options load cannot OR vibration bits in after Apply); and
 *   (gameMode1 & PAUSE_ALL) == 0. Launch never clears pause itself: the
 *   unpause path (MainFrame.c:397-406) has side effects (menu input clear,
 *   pause audio, the adventure pause cleanup, ElimBG, the menu hide, the
 *   unpause cooldown).
 * - Recorded as Task 8 / R-7 risks, not handled here: the pause-menu
 *   vibration toggle flips gameMode1 mid-race (MainFreeze.c:518); data.rwd
 *   (racing wheel calibration, loaded from the options by RaceConfig.c:16)
 *   only affects NeGcon/JogCon pads, and native input produces only digital
 *   or analog pads.
 * ---------------------------------------------------------------------------
 */

/* gameMode1 bits: mirrors of include/namespace_Main.h enum GameMode1 and ADVENTURE_BOSS. */
#define MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_1 UINT32_C(0x1)
#define MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_2 UINT32_C(0x2)
#define MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_3 UINT32_C(0x4)
#define MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_4 UINT32_C(0x8)
#define MAIN_ARCADE_RACE_SETUP_GM1_PAUSE_ALL UINT32_C(0xF)
#define MAIN_ARCADE_RACE_SETUP_GM1_DEBUG_MENU UINT32_C(0x10)
#define MAIN_ARCADE_RACE_SETUP_GM1_BATTLE_MODE UINT32_C(0x20)
#define MAIN_ARCADE_RACE_SETUP_GM1_START_OF_RACE UINT32_C(0x40)
#define MAIN_ARCADE_RACE_SETUP_GM1_P1_VIBRATE UINT32_C(0x100)
#define MAIN_ARCADE_RACE_SETUP_GM1_P2_VIBRATE UINT32_C(0x200)
#define MAIN_ARCADE_RACE_SETUP_GM1_P3_VIBRATE UINT32_C(0x400)
#define MAIN_ARCADE_RACE_SETUP_GM1_P4_VIBRATE UINT32_C(0x800)
#define MAIN_ARCADE_RACE_SETUP_GM1_WARPBALL_HELD UINT32_C(0x1000)
#define MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU UINT32_C(0x2000)
#define MAIN_ARCADE_RACE_SETUP_GM1_POINT_LIMIT UINT32_C(0x4000)
#define MAIN_ARCADE_RACE_SETUP_GM1_LIFE_LIMIT UINT32_C(0x8000)
#define MAIN_ARCADE_RACE_SETUP_GM1_TIME_LIMIT UINT32_C(0x10000)
#define MAIN_ARCADE_RACE_SETUP_GM1_TIME_TRIAL UINT32_C(0x20000)
#define MAIN_ARCADE_RACE_SETUP_GM1_BETA_UNLIMITED UINT32_C(0x40000)
#define MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_MODE UINT32_C(0x80000)
#define MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_ARENA UINT32_C(0x100000)
#define MAIN_ARCADE_RACE_SETUP_GM1_END_OF_RACE UINT32_C(0x200000)
#define MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE UINT32_C(0x400000)
#define MAIN_ARCADE_RACE_SETUP_GM1_ROLLING_ITEM UINT32_C(0x800000)
#define MAIN_ARCADE_RACE_SETUP_GM1_AKU_SONG UINT32_C(0x1000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_UKA_SONG UINT32_C(0x2000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_RELIC_RACE UINT32_C(0x4000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_CRYSTAL_CHALLENGE UINT32_C(0x8000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_CUP UINT32_C(0x10000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_GAME_CUTSCENE UINT32_C(0x20000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_LOADING UINT32_C(0x40000000)
#define MAIN_ARCADE_RACE_SETUP_GM1_ADVENTURE_BOSS UINT32_C(0x80000000)

/* gameMode2 bits: mirrors of include/namespace_Main.h enum GameMode2. */
#define MAIN_ARCADE_RACE_SETUP_GM2_SPAWN_AT_BOSS UINT32_C(0x1)
#define MAIN_ARCADE_RACE_SETUP_GM2_GAME_MODE2_SPAWN_RETAINED_UNKNOWN UINT32_C(0x2)
#define MAIN_ARCADE_RACE_SETUP_GM2_VEH_FREEZE_PODIUM UINT32_C(0x4)
#define MAIN_ARCADE_RACE_SETUP_GM2_TOKEN_RACE UINT32_C(0x8)
#define MAIN_ARCADE_RACE_SETUP_GM2_CUP_ANY_KIND UINT32_C(0x10)
#define MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP UINT32_C(0x20)
#define MAIN_ARCADE_RACE_SETUP_GM2_CREDITS UINT32_C(0x80)
#define MAIN_ARCADE_RACE_SETUP_GM2_NO_LEV_INSTANCE UINT32_C(0x100)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_WUMPA UINT32_C(0x200)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_MASK UINT32_C(0x400)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBO UINT32_C(0x800)
#define MAIN_ARCADE_RACE_SETUP_GM2_CUP_NEW_WIN UINT32_C(0x1000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CUP_NEW_BATTLE UINT32_C(0x2000)
#define MAIN_ARCADE_RACE_SETUP_GM2_VEH_FREEZE_DOOR UINT32_C(0x4000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_INVISIBLE UINT32_C(0x8000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ENGINE UINT32_C(0x10000)
#define MAIN_ARCADE_RACE_SETUP_GM2_GARAGE_OSK UINT32_C(0x20000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ADV UINT32_C(0x40000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ICY UINT32_C(0x80000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBOPAD UINT32_C(0x100000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_SUPERHARD UINT32_C(0x200000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_BOMBS UINT32_C(0x400000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ONELAP UINT32_C(0x800000)
#define MAIN_ARCADE_RACE_SETUP_GM2_INC_RELIC UINT32_C(0x1000000)
#define MAIN_ARCADE_RACE_SETUP_GM2_INC_KEY UINT32_C(0x2000000)
#define MAIN_ARCADE_RACE_SETUP_GM2_INC_TROPHY UINT32_C(0x4000000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_TURBOCOUNT UINT32_C(0x8000000)
#define MAIN_ARCADE_RACE_SETUP_GM2_LNG_CHANGE UINT32_C(0x10000000)
#define MAIN_ARCADE_RACE_SETUP_GM2_CHEAT_ALL UINT32_C(0x8FD8E00)

/* The plan's policy (the audit above). */
#define MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK                                                                 \
	(MAIN_ARCADE_RACE_SETUP_GM1_START_OF_RACE | MAIN_ARCADE_RACE_SETUP_GM1_WARPBALL_HELD |                        \
	 MAIN_ARCADE_RACE_SETUP_GM1_MAIN_MENU | MAIN_ARCADE_RACE_SETUP_GM1_END_OF_RACE |                              \
	 MAIN_ARCADE_RACE_SETUP_GM1_GAME_CUTSCENE | MAIN_ARCADE_RACE_SETUP_GM1_LOADING)
#define MAIN_ARCADE_RACE_SETUP_GM1_HOST_LOCAL_MASK                                                                \
	(MAIN_ARCADE_RACE_SETUP_GM1_P1_VIBRATE | MAIN_ARCADE_RACE_SETUP_GM1_P2_VIBRATE |                              \
	 MAIN_ARCADE_RACE_SETUP_GM1_P3_VIBRATE | MAIN_ARCADE_RACE_SETUP_GM1_P4_VIBRATE)
#define MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK MAIN_ARCADE_RACE_SETUP_GM1_ARCADE_MODE
/* Every other bit, the PAUSE and HOST-LOCAL vibration bits included (pinned to 0). */
#define MAIN_ARCADE_RACE_SETUP_GM1_CLEAR_MASK \
	(UINT32_C(0xFFFFFFFF) & ~(MAIN_ARCADE_RACE_SETUP_GM1_TRANSIENT_MASK | MAIN_ARCADE_RACE_SETUP_GM1_SET_MASK))
#define MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK \
	(MAIN_ARCADE_RACE_SETUP_GM2_LEV_SWAP | MAIN_ARCADE_RACE_SETUP_GM2_CREDITS | MAIN_ARCADE_RACE_SETUP_GM2_NO_LEV_INSTANCE)
#define MAIN_ARCADE_RACE_SETUP_GM2_SET_MASK UINT32_C(0)
#define MAIN_ARCADE_RACE_SETUP_GM2_CLEAR_MASK (UINT32_C(0xFFFFFFFF) & ~MAIN_ARCADE_RACE_SETUP_GM2_TRANSIENT_MASK)

#define MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT 8u /* retail characterIDs[8] */

/* The per-profile plan shape (RS-22). The bot slots and counts are the bot
 * rules' own (NATIVE_ARCADE_BOT_RULES_FIRST_BOT_SLOT/_BOT_COUNT and
 * _1P_FIRST_BOT_SLOT/_1P_BOT_COUNT); the plan source static-asserts that each
 * write mask covers exactly the humans and the bots. */
#define MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_TWO_CAB 2u
#define MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_TWO_CAB 0x3Fu /* slots 0..5; 6 and 7 untouched */
#define MAIN_ARCADE_RACE_SETUP_NUM_PLAYERS_ONE_CAB 1u
#define MAIN_ARCADE_RACE_SETUP_CHARACTER_WRITE_MASK_ONE_CAB 0xFFu /* slots 0..7 */
/* aiSetIndex: a retail 2P AI set index is below AI_SET_COUNT (mirrors
 * NATIVE_MATCH_SELECT_AI_SET_COUNT, the seven sets of game/zGlobal_DATA.c);
 * ARCADE_ONE_CAB uses no AI set and carries AI_SET_NONE (mirrors
 * NATIVE_MATCH_SELECT_AI_SET_NONE). The plan unit test checks both mirrors. */
#define MAIN_ARCADE_RACE_SETUP_AI_SET_COUNT 7u
#define MAIN_ARCADE_RACE_SETUP_AI_SET_NONE 0xFFu

#define MAIN_ARCADE_RACE_SETUP_TICK_RATE_NUMERATOR 30u
#define MAIN_ARCADE_RACE_SETUP_TICK_RATE_DENOMINATOR 1u
#define MAIN_ARCADE_RACE_SETUP_PLAN_V2_TAG "CTRN arcade race setup plan v2"
#define MAIN_ARCADE_RACE_SETUP_PLAN_V2_ENCODED_BYTES 135u

/*
 * Pointer-free mirror of the retail fields the plan owns, at the retail
 * widths (the mode words are int in retail and carried here as their 32-bit
 * pattern). Compare it field by field: it has padding.
 */
struct MainArcadeRaceSetupRetailFields
{
	int32_t levelID;          /* GameTracker levelID (int) */
	uint32_t gameMode1;       /* GameTracker gameMode1 (int) */
	uint32_t gameMode2;       /* GameTracker gameMode2 (int) */
	int32_t arcadeDifficulty; /* GameTracker arcadeDifficulty (int) */
	int16_t characterIDs[MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT]; /* characterIDs (s16[8]) */
	int8_t numLaps;           /* GameTracker numLaps (s8) */
	uint8_t numPlyrNextGame;  /* GameTracker numPlyrNextGame (u8) */
	uint8_t boolDemoMode;     /* GameTracker boolDemoMode (char) */
};

struct MainArcadeRaceSetupPlan
{
	uint8_t locked;           /* 1 once built */
	uint8_t numPlyrNextGame;  /* NUM_PLAYERS_TWO_CAB (2) or NUM_PLAYERS_ONE_CAB (1) */
	int8_t numLaps;           /* config lapCount */
	uint8_t boolDemoMode;     /* 0 */
	uint32_t profile;         /* config profile: ARCADE_TWO_CAB or ARCADE_ONE_CAB */
	int32_t levelID;          /* config trackID */
	uint32_t gameMode1ClearMask;
	uint32_t gameMode1SetMask;
	uint32_t gameMode2ClearMask;
	uint32_t gameMode2SetMask;
	int32_t arcadeDifficulty; /* the bots' shared difficulty */
	uint8_t characterWriteMask; /* CHARACTER_WRITE_MASK_TWO_CAB (0x3F) or _ONE_CAB (0xFF) */
	uint8_t aiSetIndex;       /* TWO_CAB: the retail 2P AI set of the bots; ONE_CAB: AI_SET_NONE */
	uint8_t firstBotSlot;     /* the profile's first bot slot: 2 (TWO_CAB) or 1 (ONE_CAB) */
	uint8_t botCount;         /* the profile's bot count: 4 (TWO_CAB) or 7 (ONE_CAB) */
	uint8_t reserved[2];      /* 0 */
	/* slot i's character where bit i of characterWriteMask is set, else 0 */
	int16_t characterIDs[MAIN_ARCADE_RACE_SETUP_CHARACTER_COUNT];
	/* expectedBots[i] is slot firstBotSlot + i's bot for i < botCount, else 0:
	 * NativeArcadeBotRules_ExpectedBots2P (TWO_CAB) or _ExpectedBots1P (ONE_CAB) */
	uint8_t expectedBots[NATIVE_ARCADE_BOT_RULES_MAX_BOT_COUNT];
	uint64_t masterSeed;      /* for the R-5 bank */
	uint32_t rngDerivationVersion;
	uint8_t configDigest[NATIVE_SHA256_DIGEST_BYTES]; /* NativeMatchConfigV1_Digest */
};

/*
 * Builds the plan. Fails unless NativeArcadeBotRules_ValidateConfigV1(config)
 * passes (an ARCADE_TWO_CAB or ARCADE_ONE_CAB config; any other profile
 * fails), the tick rate is exactly TICK_RATE_NUMERATOR/TICK_RATE_DENOMINATOR
 * (RS-14), and the humans are retail players 0 (and 1):
 * - ARCADE_TWO_CAB: the CAB1_HUMAN and CAB2_HUMAN roles are slots 0 and 1;
 *   the bots, slots 2..5, are retail drivers 2..5 (ExpectedBots2P, and
 *   aiSetIndex its set);
 * - ARCADE_ONE_CAB: the CAB1_HUMAN role is slot 0 and there is no CAB2_HUMAN
 *   slot; the bots, slots 1..7, are retail drivers 1..7 (ExpectedBots1P, and
 *   aiSetIndex AI_SET_NONE).
 * arcadeDifficulty is the first bot slot's difficulty (ValidateConfigV1: every
 * bot slot carries the same table value). The result is well-formed (below).
 */
int MainArcadeRaceSetupPlan_Build(const struct NativeMatchConfigV1 *config, struct MainArcadeRaceSetupPlan *out);

/*
 * *after = *before with the plan applied: levelID, numLaps, numPlyrNextGame,
 * arcadeDifficulty, and boolDemoMode replaced; each mode word cleared by its
 * clear mask and ORed with its set mask; characterIDs[i] replaced where bit i
 * of characterWriteMask is set. before and after may alias.
 *
 * Fails on a plan that is not well-formed: not locked; boolDemoMode or a mode
 * mask off the policy above; reserved not 0; arcadeDifficulty not a bot-rules
 * table value (NativeArcadeBotRules_IsDifficulty); profile neither
 * ARCADE_TWO_CAB nor ARCADE_ONE_CAB; numPlyrNextGame, characterWriteMask,
 * firstBotSlot, or botCount not exactly the profile's (RS-22, so a plan
 * mixing the two profiles' fields fails); a human characterID
 * (characterIDs[0], and [1] for TWO_CAB) outside 0..255; bots that are not
 * the humans' bots: for TWO_CAB, NativeArcadeBotRules_ExpectedBots2P(
 * characterIDs[0], characterIDs[1]) fails or its bots differ from
 * expectedBots[0..3] or its set from aiSetIndex; for ONE_CAB,
 * NativeArcadeBotRules_ExpectedBots1P(characterIDs[0]) fails or its bots
 * differ from expectedBots[0..6], or aiSetIndex is not AI_SET_NONE; a
 * characterIDs slot outside characterWriteMask not 0; an unused expectedBots
 * entry (i >= botCount) not 0; or characterIDs[firstBotSlot + i] !=
 * expectedBots[i] for i < botCount. Every plan Build returns is well-formed.
 */
int MainArcadeRaceSetupPlan_Apply(const struct MainArcadeRaceSetupPlan *plan,
	const struct MainArcadeRaceSetupRetailFields *before, struct MainArcadeRaceSetupRetailFields *after);

/*
 * SHA-256 of the V2 encoding (RS-21): every plan field in declaration order,
 * little-endian through NativeCodecWriter, signed fields as their
 * two's-complement pattern, PLAN_V2_ENCODED_BYTES bytes (the plan source
 * static-asserts the sum). Fails like Apply on a plan that is not
 * well-formed.
 *
 *   offset size field
 *   0      30   tag "CTRN arcade race setup plan v2" (ASCII, no NUL)
 *   30     1    locked 1
 *   31     1    numPlyrNextGame (2 TWO_CAB, 1 ONE_CAB)
 *   32     1    numLaps (s8)
 *   33     1    boolDemoMode 0
 *   34     4    profile (NATIVE_MATCH_CONFIG_V1_PROFILE_ARCADE_*)
 *   38     4    levelID (s32)
 *   42     4    gameMode1ClearMask
 *   46     4    gameMode1SetMask
 *   50     4    gameMode2ClearMask
 *   54     4    gameMode2SetMask
 *   58     4    arcadeDifficulty (s32)
 *   62     1    characterWriteMask (0x3F TWO_CAB, 0xFF ONE_CAB)
 *   63     1    aiSetIndex (0..6 TWO_CAB, 0xFF ONE_CAB)
 *   64     1    firstBotSlot (2 TWO_CAB, 1 ONE_CAB)
 *   65     1    botCount (4 TWO_CAB, 7 ONE_CAB)
 *   66     2    reserved 0, 0
 *   68     16   characterIDs[0..7], s16 each
 *   84     7    expectedBots[0..6] (unused tail 0)
 *   91     8    masterSeed
 *   99     4    rngDerivationVersion
 *   103    32   configDigest
 *   135         end
 */
int MainArcadeRaceSetupPlan_Digest(const struct MainArcadeRaceSetupPlan *plan, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

#endif
