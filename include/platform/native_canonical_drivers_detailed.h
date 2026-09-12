#ifndef PLATFORM_NATIVE_CANONICAL_DRIVERS_DETAILED_H
#define PLATFORM_NATIVE_CANONICAL_DRIVERS_DETAILED_H

#include "platform/native_canonical_drivers.h"
#include "platform/native_canonical_driver_behavior.h"

#include <stddef.h>
#include <stdint.h>

/* Pointer-free source values for the fixed 4,224-byte DRIVERS normative
 * stream.  These types deliberately describe the wire contract, not Driver. */
#define NATIVE_CANONICAL_DRIVERS_DETAILED_VERSION 2u
#define NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT UINT8_C(0xff)
#define NATIVE_CANONICAL_DRIVERS_META_BYTES 40u
#define NATIVE_CANONICAL_DRIVERS_RACE_BYTES 60u
#define NATIVE_CANONICAL_DRIVERS_PHYSICS_BYTES 148u
#define NATIVE_CANONICAL_DRIVERS_DYNAMICS_BYTES 116u
#define NATIVE_CANONICAL_DRIVERS_ACTIVE_BYTES 24u
#define NATIVE_CANONICAL_DRIVERS_BOT_BYTES 128u
#define NATIVE_CANONICAL_DRIVERS_TAIL_BYTES 4u

enum NativeCanonicalDriverKind
{
	NATIVE_CANONICAL_DRIVER_KIND_HUMAN = 1,
	NATIVE_CANONICAL_DRIVER_KIND_BOT = 2
};

/* Fixed semantic presence bits. No unassigned bit is canonical. */
enum NativeCanonicalDriverExternalPresenceFlags
{
	NATIVE_CANONICAL_DRIVER_EXTERNAL_RAIN_CLOUD = UINT16_C(0x0001),
	NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT = UINT16_C(0x0002),
	NATIVE_CANONICAL_DRIVER_EXTERNAL_KNOWN_MASK = UINT16_C(0x0003)
};

enum NativeCanonicalDriverThreadSimFlags
{
	NATIVE_CANONICAL_DRIVER_THREAD_SIM_COLLISION_DISABLED = UINT16_C(0x0001),
	NATIVE_CANONICAL_DRIVER_THREAD_SIM_KNOWN_MASK = UINT16_C(0x0001)
};

#define NATIVE_CANONICAL_DRIVER_BEHAVIOR_NONE 0u

enum NativeCanonicalDriverActiveTag
{
	NATIVE_CANONICAL_DRIVER_ACTIVE_NONE = 0,
	NATIVE_CANONICAL_DRIVER_ACTIVE_DRIFT = 1,
	NATIVE_CANONICAL_DRIVER_ACTIVE_SPIN = 2,
	NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE = 3,
	NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB = 4,
	NATIVE_CANONICAL_DRIVER_ACTIVE_PLANT_EATEN = 5,
	NATIVE_CANONICAL_DRIVER_ACTIVE_BLASTED = 6,
	NATIVE_CANONICAL_DRIVER_ACTIVE_WARP = 7
};

enum NativeCanonicalDriverDynamicsField
{
	NATIVE_CANONICAL_DRIVER_DYN_AMP_TURN_STATE,
	NATIVE_CANONICAL_DRIVER_DYN_BUTTON_USED_TO_START_DRIFT,
	NATIVE_CANONICAL_DRIVER_DYN_WALL_RUB_SPEED_LIMIT,
	NATIVE_CANONICAL_DRIVER_DYN_WHEEL_ROTATION,
	NATIVE_CANONICAL_DRIVER_DYN_SPEED,
	NATIVE_CANONICAL_DRIVER_DYN_SPEED_APPROX,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_HEIGHT_CURR,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_HEIGHT_PREV,
	NATIVE_CANONICAL_DRIVER_DYN_AXIS_ROTATION_Y,
	NATIVE_CANONICAL_DRIVER_DYN_AXIS_ROTATION_X,
	NATIVE_CANONICAL_DRIVER_DYN_ANGLE,
	NATIVE_CANONICAL_DRIVER_DYN_BASE_SPEED,
	NATIVE_CANONICAL_DRIVER_DYN_FIRE_SPEED,
	NATIVE_CANONICAL_DRIVER_DYN_FORWARD_ACCEL_IMPULSE,
	NATIVE_CANONICAL_DRIVER_DYN_ROTATION_SPIN_RATE,
	NATIVE_CANONICAL_DRIVER_DYN_ACCEL_TAP_WINDOW_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_ACCEL_TAP_COUNT,
	NATIVE_CANONICAL_DRIVER_DYN_TERRAIN_SCALED_BASE_SPEED,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_CURR,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_PREV,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_LERP_TARGET,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_LERP_VEL,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_ANGLE,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_VELOCITY,
	NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_MULT_DRIFT,
	NATIVE_CANONICAL_DRIVER_DYN_TURBO_METER_ROOM_LEFT,
	NATIVE_CANONICAL_DRIVER_DYN_TURBO_OUTSIDE_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_RESERVES,
	NATIVE_CANONICAL_DRIVER_DYN_FIRE_SPEED_CAP,
	NATIVE_CANONICAL_DRIVER_DYN_NUM_FRAMES_SPENT_STEERING,
	NATIVE_CANONICAL_DRIVER_DYN_FORWARD_DIR,
	NATIVE_CANONICAL_DRIVER_DYN_PREVIOUS_FRAME_MULT_DRIFT,
	NATIVE_CANONICAL_DRIVER_DYN_TIME_UNTIL_DRIFT_SPINOUT,
	NATIVE_CANONICAL_DRIVER_DYN_DISTANCE_FROM_GROUND,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_TEN_BUFFER,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_COOLDOWN_MS,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_COYOTE_TIMER_MS,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_FORCED_MS,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_INITIAL_VEL_Y,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_HIGH_JUMP_TIMER_MS,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_LANDING_BOOST,
	NATIVE_CANONICAL_DRIVER_DYN_WALL_RUB_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_NO_INPUT_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_BURN_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_SQUISH_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_START_GUARD_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_WINDOW_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_COUNT,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_SQUISH_STRETCH,
	NATIVE_CANONICAL_DRIVER_DYN_JUMP_SQUISH_STRETCH2,
	NATIVE_CANONICAL_DRIVER_DYN_TERRAIN_FRICTION_TIMER,
	NATIVE_CANONICAL_DRIVER_DYN_COUNT
};

struct NativeCanonicalDriversPreludeV1
{
	uint32_t slotCount, presenceMask;
	uint8_t raceOrder[8], playerCount, activeBotCount;
	int8_t numLaps;
	uint8_t winnerCount, winnerSlots[4], humanPlayerPositions[8], navListCount[3], navListOrder[3][8], raceOrderCount;
	uint32_t detailedVersion;
};

struct NativeCanonicalDriverMetaV1
{
	uint8_t present, slotIndex, driverID, characterID, driverKind, behaviorID, threadBehaviorID, kartState;
	uint32_t actionsFlagSet, actionsFlagSetPrevFrame;
	uint8_t heldItemID, numHeldItems;
	int8_t numWumpas, numCrystals, numTimeCrates, accelConst, turnConst, turboConst;
	uint8_t lapIndex;
	int8_t simpTurnState;
	uint8_t currentTerrain, forcedJumpType;
	int8_t normalVecID;
	uint8_t boolFirstFrameSinceRevEngine, clockSend;
	int8_t revEngineState;
	uint16_t externalPresenceFlags, driverThreadSimFlags;
	int16_t collisionFlags, rainCloudEffect;
};

struct NativeCanonicalDriverRaceV1
{
	int16_t clockReceive, hazardTimer, superEngineTimer, itemRollTimer, noItemTimer, jumpMeter, jumpMeterTimer, numTurbos;
	int32_t invincibleTimer, invisibleTimer, lapTime, timeElapsedInRace;
	int16_t driverRank;
	uint8_t checkpointBranchChoiceIndex, checkpointCurrentIndex;
	uint32_t distanceToFinishCurr, distanceToFinishCheckpoint, distanceDrivenBackwards;
	int32_t battleNumLives, battleTeamID, pickupLetterCount;
};

struct NativeCanonicalDriverPhysicsV1
{
	uint32_t currQuadIndex, underDriverQuadIndex, lastValidQuadIndex;
	uint8_t terrainMeta1Index, terrainMeta2Index;
	uint16_t reserved0;
	uint32_t stepFlagSet;
	int32_t quadBlockHeight, velocity[3], originToCenter[3], posCurr[3], posPrev[3];
	int16_t normalVecUP[3], spsHitPos[3], spsNormalVec[3], axisAngle1[3], axisAngle2[3], axisAngle3[3], axisAngle4[3],
		rotCurr[4], rotPrev[4], posWallColl[3], forwardAccelVector[3], accel[3];
};

struct NativeCanonicalDriverDynamicsV1 { int16_t field[NATIVE_CANONICAL_DRIVER_DYN_COUNT]; int32_t xSpeed, ySpeed, zSpeed; };
struct NativeCanonicalDriverActiveV1 { uint32_t unionTag; uint8_t branchBytes[20]; };
/* Explicit values for bytes 388..515. This is a wire-value type, never a
 * native BotData snapshot; every field is emitted LE below. */
struct NativeCanonicalDriverBotV1 {
	int16_t botPath; uint16_t botNavFrameIndex;
	int32_t navProgressRemainder, reserved5ac; uint32_t botFlags; int32_t botAccel; int16_t aiDamageState;
	int16_t rotXZ, driftTarget, mulDrift, simpTurnState, turboMeter, fireLevel;
	int32_t squishCooldown, reserved5cc, speedY, speedLinear, accel[3], velocity[3], positionBackup[3];
	int16_t aiRot[3]; int32_t aiProgressCooldown; int16_t aiRotY; uint8_t aiQuadblockCheckpointIndex;
	int16_t estimatePos[3]; uint8_t estimateRot[4]; int16_t estimateDistXYZ, estimateDistXZ, estimateFlags, estimatePathChangeOpcode;
	uint8_t estimateGoBackCount, estimateSpecialBits, maskObjPresent; int16_t weaponCooldown; uint8_t blastBounceCount, desiredPathBossOnly; int32_t reserved628;
};
enum NativeCanonicalDriverBotFlags {
	NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE = UINT32_C(0x0002),
	NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_SUPPRESS = UINT32_C(0x0004),
	NATIVE_CANONICAL_DRIVER_BOT_FLAGS_KNOWN_MASK = UINT32_C(0x03ff)
};
enum NativeCanonicalDriverKartState {
	NATIVE_CANONICAL_DRIVER_KART_STATE_MASK_GRABBED = 5
};
struct NativeCanonicalDriverPendingDamageV1 { uint8_t type, attackerSlotPlusOne, reason, reservedZero; };
_Static_assert(sizeof(struct NativeCanonicalDriverPendingDamageV1)==NATIVE_CANONICAL_DRIVERS_TAIL_BYTES,
	"pending-damage tail must remain four explicit bytes");
struct NativeCanonicalDriverSlotV1
{
	struct NativeCanonicalDriverMetaV1 meta;
	struct NativeCanonicalDriverRaceV1 race;
	struct NativeCanonicalDriverPhysicsV1 physics;
	struct NativeCanonicalDriverDynamicsV1 dynamics;
	struct NativeCanonicalDriverActiveV1 active;
	struct NativeCanonicalDriverBotV1 bot;
	struct NativeCanonicalDriverPendingDamageV1 pendingDamage;
};
struct NativeCanonicalDriversDetailedV1 { struct NativeCanonicalDriversPreludeV1 prelude; struct NativeCanonicalDriverSlotV1 slots[8]; };

/* Validates the portable 148-byte Physics group without consulting native
 * pointers.  Quad references are either UINT32_MAX (null) or a nonnegative
 * signed-32-bit index; the two terrain values are fixed data[] indices. */
int NativeCanonicalDriverPhysicsV1_Validate(const struct NativeCanonicalDriverPhysicsV1 *value);
void NativeCanonicalDriversDetailedV1_Init(struct NativeCanonicalDriversDetailedV1 *value);
int NativeCanonicalDriversDetailedV1_Validate(const struct NativeCanonicalDriversDetailedV1 *value);
size_t NativeCanonicalDriversDetailedV1_EncodedSize(void);
int NativeCanonicalDriversDetailedV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalDriversDetailedV1 *value);
/* In-place summary builder for game-owned static workspaces. `scratch` must
 * be exactly the 4,224-byte normative stream and is never retained. */
int NativeCanonicalDriversDetailedV1_BuildSummaryWithScratch(const struct NativeCanonicalDriversDetailedV1 *value,
	uint8_t *scratch, size_t scratchSize, struct NativeCanonicalDriversV1 *summary);
int NativeCanonicalDriversDetailedV1_BuildSummary(const struct NativeCanonicalDriversDetailedV1 *value,
	struct NativeCanonicalDriversV1 *summary);

#endif
