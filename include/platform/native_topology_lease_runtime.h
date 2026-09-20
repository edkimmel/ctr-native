#ifndef PLATFORM_NATIVE_TOPOLOGY_LEASE_RUNTIME_H
#define PLATFORM_NATIVE_TOPOLOGY_LEASE_RUNTIME_H

/*
 * Internal restore boundary for the game-owned topology lease retirement
 * owner.  This has no checkpoint payload, state export, capture, activation,
 * or network meaning; it merely retires private process-local state before a
 * validated checkpoint starts overwriting resident game memory.
 */
void NativeTopologyLeaseRuntime_BeforeCheckpointRestore(void);

#endif
