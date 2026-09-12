#ifndef MAIN_CANONICAL_DRIVERS_H
#define MAIN_CANONICAL_DRIVERS_H
#include "platform/native_canonical_driver_behavior.h"
#include "platform/native_canonical_drivers_roster.h"
#include "namespace_Vehicle.h"
enum MainCanonicalDriversStatus { MAIN_CANONICAL_DRIVERS_FAILURE=0, MAIN_CANONICAL_DRIVERS_OK=1 };
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
#endif
