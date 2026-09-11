#include "platform/native_identity.h"

#include "platform/native_disc_image.h"

#include <string.h>

#ifndef CTR_NATIVE_BUILD_IDENTITY_KNOWN
#define CTR_NATIVE_BUILD_IDENTITY_KNOWN 0
#endif

#ifndef CTR_NATIVE_BUILD_IDENTITY_HEX
#define CTR_NATIVE_BUILD_IDENTITY_HEX ""
#endif

static int NativeIdentity_HexDigit(char byte, uint8_t *value)
{
	if ((byte >= '0') && (byte <= '9'))
	{
		*value = (uint8_t)(byte - '0');
		return 1;
	}
	if ((byte >= 'a') && (byte <= 'f'))
	{
		*value = (uint8_t)(byte - 'a' + 10);
		return 1;
	}
	if ((byte >= 'A') && (byte <= 'F'))
	{
		*value = (uint8_t)(byte - 'A' + 10);
		return 1;
	}

	return 0;
}

static int NativeIdentity_GetBuild(uint8_t build[NATIVE_IDENTITY_DIGEST_BYTES])
{
#if CTR_NATIVE_BUILD_IDENTITY_KNOWN == 0
	(void)build;
	return 0;
#else
	static const char manifestDigest[] = CTR_NATIVE_BUILD_IDENTITY_HEX;
	uint8_t candidate[NATIVE_IDENTITY_DIGEST_BYTES];

	if (strlen(manifestDigest) != NATIVE_IDENTITY_DIGEST_BYTES * 2u)
	{
		return 0;
	}

	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		uint8_t high;
		uint8_t low;

		if (!NativeIdentity_HexDigit(manifestDigest[i * 2u], &high) || !NativeIdentity_HexDigit(manifestDigest[i * 2u + 1u], &low))
		{
			return 0;
		}
		candidate[i] = (uint8_t)((high << 4) | low);
	}
	memcpy(build, candidate, sizeof(candidate));
	return 1;
#endif
}

int NativeIdentity_BuildKnown(void)
{
	uint8_t ignored[NATIVE_IDENTITY_DIGEST_BYTES];

	return NativeIdentity_GetBuild(ignored);
}

int NativeIdentity_Get(struct NativeIdentityV1 *identity)
{
	struct NativeIdentityV1 candidate;

	if ((identity == NULL) || !NativeIdentity_GetBuild(candidate.build) ||
	    !NativeDiscImage_GetContentIdentity(candidate.content))
	{
		return 0;
	}

	*identity = candidate;
	return 1;
}
