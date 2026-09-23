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

#endif
