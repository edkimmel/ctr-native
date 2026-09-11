#ifndef PLATFORM_NATIVE_SHA256_H
#define PLATFORM_NATIVE_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define NATIVE_SHA256_DIGEST_BYTES 32u

struct NativeSha256
{
	uint32_t state[8];
	uint64_t bitCount;
	uint8_t block[64];
	size_t blockSize;
};

void NativeSha256_Init(struct NativeSha256 *sha);
void NativeSha256_Update(struct NativeSha256 *sha, const void *bytes, size_t size);
void NativeSha256_Final(struct NativeSha256 *sha, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES]);

#endif
