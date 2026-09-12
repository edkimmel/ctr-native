#ifndef MAIN_CANONICAL_DRIVERS_H
#define MAIN_CANONICAL_DRIVERS_H
#include "platform/native_canonical_driver_behavior.h"
#include "platform/native_canonical_drivers_roster.h"
#include "namespace_Vehicle.h"
enum MainCanonicalDriversStatus { MAIN_CANONICAL_DRIVERS_FAILURE=0, MAIN_CANONICAL_DRIVERS_OK=1 };

/* Dormant source projection candidate.  This intentionally carries only the
 * validated roster/prelude and the fixed Race group; it is not a detailed
 * DRIVERS record and must not be published to the scheduler. */
struct MainCanonicalDriversRosterRaceCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
};

/* Further dormant candidate: stable-slot Race plus the 116-byte Dynamics
 * group.  It is an explicit value projection, never a native Driver layout
 * snapshot, and remains outside the live detailed DRIVERS publisher. */
struct MainCanonicalDriversRosterRaceDynamicsCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
	struct NativeCanonicalDriverDynamicsV1 dynamics[8];
};

/* Dormant local-only extension with the selected 24-byte Active value.  It
 * is deliberately not a detailed record publisher or scheduler payload. */
struct MainCanonicalDriversRosterRaceDynamicsActiveCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
	struct NativeCanonicalDriverDynamicsV1 dynamics[8];
	struct NativeCanonicalDriverActiveV1 active[8];
};
struct MainCanonicalDriversRosterRaceDynamicsActivePendingCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
	struct NativeCanonicalDriverDynamicsV1 dynamics[8];
	struct NativeCanonicalDriverActiveV1 active[8];
	struct NativeCanonicalDriverPendingDamageV1 pendingDamage[8];
};
/* Local-only Bot extension. It remains outside detailed publication. */
struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
	struct NativeCanonicalDriverDynamicsV1 dynamics[8];
	struct NativeCanonicalDriverActiveV1 active[8];
	struct NativeCanonicalDriverPendingDamageV1 pendingDamage[8];
	struct NativeCanonicalDriverBotV1 bot[8];
};

/* Narrow dormant Meta foundation for one Driver whose large-stack root has
 * already passed the roster extractor's ownership gate.  This returns only
 * the frozen external/thread presence bits; it is not an aggregate Meta
 * projector and must not publish a detailed DRIVERS record. */
struct MainCanonicalDriversMetaFlags
{
	uint16_t externalPresenceFlags;
	uint16_t driverThreadSimFlags;
};

const struct NativeCanonicalDriverBehaviorRegistry *MainCanonicalDrivers_ProductionRegistry(void);
int MainCanonicalDrivers_ValidateProductionBinding(void);
/* These compare typed callbacks only and emit stable IDs; no pointer escapes. */
int MainCanonicalDrivers_ResolveBehavior(const DriverFunc table[13], uint8_t *behaviorIDOut);
int MainCanonicalDrivers_ResolveThread(void (*thread)(struct Thread *), uint8_t *threadBehaviorIDOut);
/* Dormant candidate builder for a later full driver projection. */
int MainCanonicalDrivers_ProjectPrelude(const struct NativeCanonicalDriversRosterInput *input,
	const DriverFunc tables[8][13], void (*const threads[8])(struct Thread *),
	struct NativeCanonicalDriversRosterCandidate *out);
/* Dormant live-source adapter. It validates only the 64-byte roster prelude
 * and behavior/thread identities into a local candidate; it does not publish
 * a DRIVERS state or inspect the 520-byte slot payloads. */
int MainCanonicalDrivers_ExtractRosterPrelude(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct NativeCanonicalDriversRosterCandidate *out);
/* Extends ExtractRosterPrelude with the explicit 60-byte Race group for each
 * present stable driver slot.  The result is output-atomic and remains a
 * dormant candidate until all 520-byte slot groups are independently mapped. */
int MainCanonicalDrivers_ExtractRosterRace(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct MainCanonicalDriversRosterRaceCandidate *out);
/* Extends the same fully validated roster/root candidate with Dynamics in
 * stable GameTracker.drivers[0..7] order.  Absent slots are exact zero
 * values and output is assigned only after every present field is copied. */
int MainCanonicalDrivers_ExtractRosterRaceDynamics(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct MainCanonicalDriversRosterRaceDynamicsCandidate *out);
/* Fieldwise selected-union projection.  The Driver must already be a valid
 * root when this is used in game-owned code; this routine never copies a
 * pointer or inactive union branch and assigns its output only on success. */
int MainCanonicalDrivers_ExtractDriverActive(const struct Driver *driver,
	uint8_t kind,uint8_t behaviorID,uint8_t kartState,
	struct NativeCanonicalDriverActiveV1 *out);
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActive(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct MainCanonicalDriversRosterRaceDynamicsActiveCandidate *out);
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePending(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingCandidate *out);
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBot(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotCandidate *out);
int MainCanonicalDrivers_ResolveMetaFlags(const struct GameTracker *gGT,
	const struct Driver *driver,uint8_t kind,uint8_t behaviorID,uint8_t kartState,
	struct MainCanonicalDriversMetaFlags *out);
#endif
