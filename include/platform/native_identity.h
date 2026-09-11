#ifndef PLATFORM_NATIVE_IDENTITY_H
#define PLATFORM_NATIVE_IDENTITY_H

#include <stdint.h>

#define NATIVE_IDENTITY_DIGEST_BYTES 32u

/*
 * Build and content identities are SHA-256 digests used only as immutable
 * replay/state compatibility gates.  They are never zero-filled fallbacks.
 */
struct NativeIdentityV1
{
	uint8_t build[NATIVE_IDENTITY_DIGEST_BYTES];
	uint8_t content[NATIVE_IDENTITY_DIGEST_BYTES];
};

/* A false result marks this build as ineligible for deterministic modes. */
int NativeIdentity_BuildKnown(void);

/*
 * Returns a complete identity only after the build manifest is known and the
 * retained raw disc-image handle has been initialized and hashed.  On failure
 * it leaves the output identity unchanged.
 */
int NativeIdentity_Get(struct NativeIdentityV1 *identity);

#endif
