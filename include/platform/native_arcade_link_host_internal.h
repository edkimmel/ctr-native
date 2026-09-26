#ifndef PLATFORM_NATIVE_ARCADE_LINK_HOST_INTERNAL_H
#define PLATFORM_NATIVE_ARCADE_LINK_HOST_INTERNAL_H

#include <stdint.h>

/*
 * Host-side read-back for the arcade-link host glue's unit test
 * (tests/native_arcade_link_host_test.c). Not game-facing: game code uses
 * only include/platform/native_arcade_link_host.h, and no game source may
 * include this header (tests/native_arcade_link_host_isolation_test.cmake).
 */

/* The select entropy the host handed the link at its latest
 * initialization (NativeArcadeLinkHost_MixSelectEntropy of the options'
 * selectEntropy and the host epoch); 0 unless the mode is LINK. */
uint64_t NativeArcadeLinkHost_InternalSelectEntropy(void);

/* The link's local race-failure latch (RL-11) as it stands: 1 from an
 * accepted local race failure until the next Tick consumes it; 0 unless the
 * mode is LINK. */
uint8_t NativeArcadeLinkHost_InternalLocalRaceFailure(void);

/* Local drive failures the host reported to the link since the last
 * Configure or Shutdown (at most one per race, LR-S9). */
uint32_t NativeArcadeLinkHost_InternalDriveFailureReports(void);

/* The link's in-race stall count as it stands: the stall reports the race's
 * outcome tracker has counted in a row toward the stall timeout (UX-9; any
 * other take result handed to it resets the count, and each race's start
 * clears it); 0 unless the mode is LINK. */
uint32_t NativeArcadeLinkHost_InternalConsecutiveStalls(void);

/* The link's launch linger count as it stands: ticks (and held tick
 * periods) counted since the launch agreement committed (RL-4, LR-69); 0
 * unless the mode is LINK. */
uint32_t NativeArcadeLinkHost_InternalLaunchTicksSinceCommit(void);

/* The race tick limit the host's race tick limit setter stored (0: the
 * default), in any mode (LR-60). */
uint32_t NativeArcadeLinkHost_InternalRaceTickLimit(void);

/* The race tick limit in force in the race drive: the begun drive's (18000
 * for a default begin), 0 for a drive not begun; 0 unless the mode is LINK. */
uint32_t NativeArcadeLinkHost_InternalDriveRaceTickLimit(void);

/* Forward-declared only, as in the public header. */
struct NativeMatchConfigV1;

/* Solo (docs/SOLO_CAB_MILESTONE.md SOLO-11): the unit tests' switch for the
 * solo gate, read by the next LINK Configure (and kept by AbortToTitle).
 * Production never calls it, so solo stays off there until SOLO-S4 changes
 * the host's own default. Any nonzero value means 1. Kept across Shutdown. */
void NativeArcadeLinkHost_InternalSetSoloEnabled(uint8_t enabled);

/* The solo query's fail-closed check (SOLO-6), on any candidate: copies
 * *candidate into *out and returns 1 only when it is an ARCADE_ONE_CAB config
 * that passes NativeArcadeBotRules_ValidateConfigV1; otherwise (a NULL
 * argument, another profile, or a failed check) returns 0 with *out
 * untouched. NativeArcadeLinkHost_GetSoloConfig returns exactly what this
 * returns for the link's solo race config. */
int NativeArcadeLinkHost_InternalCopyValidSoloConfig(const struct NativeMatchConfigV1 *candidate,
	struct NativeMatchConfigV1 *out);

#endif
