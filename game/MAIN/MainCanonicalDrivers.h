#ifndef MAIN_CANONICAL_DRIVERS_H
#define MAIN_CANONICAL_DRIVERS_H
#include "platform/native_canonical_driver_behavior.h"
#include "platform/native_canonical_drivers_roster.h"
#include "MainCanonicalTopology.h"
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
/* Local-only complete-within-scope Meta extension.  This remains a candidate,
 * not a detailed-stream publisher or scheduler payload. */
struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
	struct NativeCanonicalDriverDynamicsV1 dynamics[8];
	struct NativeCanonicalDriverActiveV1 active[8];
	struct NativeCanonicalDriverPendingDamageV1 pendingDamage[8];
	struct NativeCanonicalDriverBotV1 bot[8];
	struct NativeCanonicalDriverMetaV1 meta[8];
};
/* Local-only extension with the explicit 148-byte Physics group.  The
 * topology lifetime context is an input guard only; this remains neither a
 * detailed-stream publisher nor a scheduler payload. */
struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate
{
	struct NativeCanonicalDriversRosterCandidate roster;
	struct NativeCanonicalDriverRaceV1 race[8];
	struct NativeCanonicalDriverDynamicsV1 dynamics[8];
	struct NativeCanonicalDriverActiveV1 active[8];
	struct NativeCanonicalDriverPendingDamageV1 pendingDamage[8];
	struct NativeCanonicalDriverBotV1 bot[8];
	struct NativeCanonicalDriverMetaV1 meta[8];
	struct NativeCanonicalDriverPhysicsV1 physics[8];
};
/* Complete but dormant DRIVERS value.  It is an assembly result only: no
 * scheduler, replay, native state, or live publisher retains this object. */
struct MainCanonicalDriversDetailedAssembly
{
	struct NativeCanonicalDriversDetailedV1 detailed;
	struct NativeCanonicalDriversV1 summary;
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
/* The source-shaped roster input ExtractRosterPrelude normalizes: built from
 * gGT/sdata with the same ownership gates, every present slot's behavior and
 * thread IDs resolved exactly as ProjectPrelude resolves them, and returned
 * only if NativeCanonicalDriversRoster_Normalize accepts it.  Output-atomic. */
int MainCanonicalDrivers_ExtractRosterInput(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct NativeCanonicalDriversRosterInput *out);
/* Pre-race variant for the live race setup adapter, which reads the roster
 * right after MainInit_Drivers, before the first race tick rebuilds
 * driversInRaceOrder (PlayLevel_UpdateLapStats): identical to
 * ExtractRosterInput except that the race order and winner lists are recorded
 * as not yet observed (raceOrderCount 0 and winnerCount 0, both lists at their
 * empty encodings), and driversInRaceOrder, numWinners, and winnerIndex are
 * never read, so a stale order left by the previous race cannot refuse it.
 * humanPlayerPositions (the input's ranks) are stale before the first race
 * tick as well: PlayLevel_UpdateLapStats writes them only at PlayLevel.c:371
 * and :413, so they still hold the previous race's (or the demo race's)
 * positions. This variant still reads them and range-checks them (0..7 per
 * human, as ExtractRosterInput does); nothing downstream in the race setup
 * (facts, roster, bot setup) depends on their values.
 * Output-atomic. */
int MainCanonicalDrivers_ExtractRosterInputPreRace(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct NativeCanonicalDriversRosterInput *out);
/* Dormant live-source adapter. It validates only the 64-byte roster prelude
 * and behavior/thread identities into a local candidate; it does not publish
 * a DRIVERS state or inspect the 520-byte slot payloads.  Equivalent to
 * ExtractRosterInput followed by NativeCanonicalDriversRoster_Normalize. */
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
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta(const struct GameTracker *gGT,
	const struct sData *sdata,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate *out);
/* Every present root requires a current, explicitly supplied topology
 * snapshot even if all three QuadBlock references are null.  Rootless menus
 * remain topology-uninspected and yield exact-zero Physics slots. */
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMetaPhysics(
	const struct GameTracker *gGT,const struct sData *sdata,
	const struct MainCanonicalTopologyContext *topologyContext,
	const struct MainCanonicalTopologySnapshot *topologySnapshot,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate *out);
/* Workspace-only continuation after ExtractRosterPrelude has populated
 * out->roster.  Remaining groups are written directly into caller-owned
 * staging memory, avoiding the nested complete-candidate copies used by the
 * public transactional wrappers.  Failure may leave staging modified. */
int MainCanonicalDrivers_ExtractCompleteFromPreludeInPlace(
	const struct GameTracker *gGT,const struct sData *sdata,
	const struct MainCanonicalTopologyContext *topologyContext,
	const struct MainCanonicalTopologySnapshot *topologySnapshot,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate *staging);
/* Assembles an already successful Meta+Physics candidate into the frozen
 * detailed stream and its digest summary.  The output is transactional. */
int MainCanonicalDrivers_AssembleDetailed(
	const struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate *candidate,
	struct MainCanonicalDriversDetailedAssembly *out);
/* Workspace-oriented in-place variant: no 4KiB normative stream or assembly
 * is placed on the caller stack.  The caller must treat `out` as staging
 * until this returns success. */
int MainCanonicalDrivers_AssembleDetailedWithScratch(
	const struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate *candidate,
	uint8_t *scratch,size_t scratchSize,struct MainCanonicalDriversDetailedAssembly *out);
int MainCanonicalDrivers_ResolveMetaFlags(const struct GameTracker *gGT,
	const struct Driver *driver,uint8_t kind,uint8_t behaviorID,uint8_t kartState,
	struct MainCanonicalDriversMetaFlags *out);
#endif
