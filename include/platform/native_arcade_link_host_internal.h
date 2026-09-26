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

/* 1 while the race drive is in its local mode (a solo race's drive,
 * docs/SOLO_CAB_MILESTONE.md SOLO-7), else 0; 0 unless the mode is LINK. */
uint8_t NativeArcadeLinkHost_InternalDriveLocal(void);

/* Discovery (docs/DISCOVERY_MILESTONE.md DISC-12): 1 while the host's
 * discovery socket is open (a discovery-mode LINK Configure until Shutdown),
 * else 0; and its bound port, 0 while closed. */
uint8_t NativeArcadeLinkHost_InternalDiscoveryOpen(void);
uint16_t NativeArcadeLinkHost_InternalDiscoveryPort(void);

/* Discovery: the pairings (or none) the host handed the link since the last
 * Configure or Shutdown; always 0 in static mode, where it hands none. */
uint32_t NativeArcadeLinkHost_InternalPairingsHanded(void);

/* Discovery: when the mode is LINK and the link's pending pairing slot
 * holds a pairing, writes its peer address and elected role (1 cabinet 1, 2
 * cabinet 2) and returns 1; otherwise returns 0 with the outputs untouched
 * (a NULL output included). */
int NativeArcadeLinkHost_InternalPendingPairing(uint32_t *peerIpv4, uint16_t *peerPort, uint8_t *localRole);

/* Discovery (risks 6 and 10): while the discovery socket is open, writes its
 * service ticks since Configure, its interface-list reads (the one at
 * Configure included; none with explicit targets), and 1 while a refresh
 * held by a race waits, and returns 1; otherwise returns 0 with the outputs
 * untouched (a NULL output included). */
int NativeArcadeLinkHost_InternalDiscoveryStatus(uint32_t *ticks, uint32_t *refreshes, uint8_t *refreshPending);

/* 1 while a race runs, the state that holds the discovery refresh (risk 10):
 * the flow on RACING, the race pacing on (RaceBegin to RaceEnd), a begun
 * drive, or a drive in its finish linger; 0 otherwise and outside LINK. */
uint8_t NativeArcadeLinkHost_InternalRaceRunning(void);

/* Forward-declared only, as in the public header. */
struct NativeMatchConfigV1;

/* Solo (docs/SOLO_CAB_MILESTONE.md SOLO-11): the unit tests' switch for the
 * solo gate, read by the next LINK Configure (and kept by AbortToTitle).
 * Production never calls it, so production runs on the host's own default,
 * on since SOLO-S4. Any nonzero value means 1. Kept across Shutdown. */
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
