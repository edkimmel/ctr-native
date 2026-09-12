#ifndef PLATFORM_NATIVE_CANONICAL_DRIVERS_ROSTER_H
#define PLATFORM_NATIVE_CANONICAL_DRIVERS_ROSTER_H

#include "platform/native_canonical_drivers_detailed.h"

#define NATIVE_CANONICAL_DRIVERS_HANDLE_NONE UINT8_C(0xff)
struct NativeCanonicalDriversRosterSlot { uint8_t present, driverID, kind, behaviorID, threadBehaviorID; };
struct NativeCanonicalDriversRosterInput {
	struct NativeCanonicalDriversRosterSlot slots[8]; uint8_t raceOrderCount, raceOrder[8], winnerCount, winners[4], playerCount, ranks[8], navCount[3], navOrder[3][8];
};
struct NativeCanonicalDriversRosterCandidate { struct NativeCanonicalDriversPreludeV1 prelude; uint8_t behaviorID[8], threadBehaviorID[8], kind[8]; };
int NativeCanonicalDriversRoster_Normalize(const struct NativeCanonicalDriversRosterInput *input, struct NativeCanonicalDriversRosterCandidate *out);
#endif
