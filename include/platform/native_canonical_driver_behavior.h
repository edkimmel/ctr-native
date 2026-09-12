#ifndef PLATFORM_NATIVE_CANONICAL_DRIVER_BEHAVIOR_H
#define PLATFORM_NATIVE_CANONICAL_DRIVER_BEHAVIOR_H

#include <stdint.h>

#define NATIVE_CANONICAL_DRIVER_BEHAVIOR_INIT_COUNT 11u
#define NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT 17u
#define NATIVE_CANONICAL_DRIVER_BEHAVIOR_TABLE_FIELDS 13u
#define NATIVE_CANONICAL_DRIVER_BEHAVIOR_MAX 186u

enum NativeCanonicalDriverThreadBehaviorID
{
	NATIVE_CANONICAL_DRIVER_THREAD_NULL = 0,
	NATIVE_CANONICAL_DRIVER_THREAD_VEH_BIRTH_NULL = 1,
	NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE = 2,
	NATIVE_CANONICAL_DRIVER_THREAD_BOTS_REV_ENGINE = 3
};

struct NativeCanonicalDriverBehaviorRegistry
{
	const void *initTokens[NATIVE_CANONICAL_DRIVER_BEHAVIOR_INIT_COUNT];
	const void *suffixTemplates[NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT][12];
};

typedef const void *(*NativeCanonicalDriverBehaviorTokenCallback)(void *context, uint8_t fieldIndex);
typedef int (*NativeCanonicalDriverBehaviorKindCallback)(void *context, uint8_t slotIndex, uint8_t *kindOut);

int NativeCanonicalDriverBehaviorRegistry_Validate(const struct NativeCanonicalDriverBehaviorRegistry *registry);
int NativeCanonicalDriverBehavior_Resolve(const struct NativeCanonicalDriverBehaviorRegistry *registry,
	const void *const table[NATIVE_CANONICAL_DRIVER_BEHAVIOR_TABLE_FIELDS], uint8_t *behaviorIDOut);
int NativeCanonicalDriverBehavior_ResolveCallback(const struct NativeCanonicalDriverBehaviorRegistry *registry,
	NativeCanonicalDriverBehaviorTokenCallback callback, void *context, uint8_t *behaviorIDOut);
int NativeCanonicalDriverBehavior_ValidateKind(uint8_t kind, uint8_t behaviorID, uint8_t threadBehaviorID);
/* Pointer-free state/tag gate.  Active tags use the stable detailed enum
 * values (NONE through WARP); kartState is the documented PSX state value. */
int NativeCanonicalDriverBehavior_ValidateState(uint8_t kind, uint8_t behaviorID, uint8_t kartState, uint32_t activeTag);
int NativeCanonicalDriverBehavior_ValidateKindCallback(NativeCanonicalDriverBehaviorKindCallback callback, void *context,
	uint8_t slotIndex, uint8_t behaviorID, uint8_t threadBehaviorID);

#endif
