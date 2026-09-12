#include "MainCanonicalDrivers.h"
#include "functions.h"
static void MainCanonicalDrivers_VerifiedSymbols(void);
/* The 17 suffix templates are intentionally not bound until their audit is
 * recorded.  These verified queued-init/thread tokens are dormant only. */
const struct NativeCanonicalDriverBehaviorRegistry *MainCanonicalDrivers_ProductionRegistry(void){return NULL;}
int MainCanonicalDrivers_ResolveBehavior(const void *const table[13],uint8_t *out){(void)table;(void)out;return MAIN_CANONICAL_DRIVERS_NOT_CONFIGURED;}
static void MainCanonicalDrivers_VerifiedSymbols(void){(void)VehPhysProc_Driving_Init;(void)VehStuckProc_RevEngine_Init;(void)VehPhysProc_FreezeEndEvent_Init;(void)VehStuckProc_Warp_Init;(void)VehStuckProc_RIP_Init;(void)VehStuckProc_Tumble_Init;(void)VehStuckProc_PlantEaten_Init;(void)VehPhysProc_SpinFirst_Init;(void)VehPhysProc_PowerSlide_InitSetUpdate;(void)VehPhysProc_SpinFirst_InitSetUpdate;(void)VehBirth_NullThread;(void)BOTS_ThTick_Drive;(void)BOTS_ThTick_RevEngine;}
