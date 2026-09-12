#ifndef MAIN_CANONICAL_DRIVERS_H
#define MAIN_CANONICAL_DRIVERS_H
#include "platform/native_canonical_driver_behavior.h"
enum MainCanonicalDriversStatus { MAIN_CANONICAL_DRIVERS_OK=1, MAIN_CANONICAL_DRIVERS_NOT_CONFIGURED=0 };
const struct NativeCanonicalDriverBehaviorRegistry *MainCanonicalDrivers_ProductionRegistry(void);
int MainCanonicalDrivers_ResolveBehavior(const void *const table[13], uint8_t *behaviorIDOut);
#endif
