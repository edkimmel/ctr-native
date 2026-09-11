#include "platform/native_sha256.h"

#include <string.h>

#define NATIVE_SHA256_ROTATE_RIGHT(value, amount) (((value) >> (amount)) | ((value) << (32u - (amount))))

static const uint32_t s_nativeSha256Constants[64] = {
	UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
	UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
	UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
	UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
	UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
	UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
	UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
	UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
	UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
	UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
	UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
};

static uint32_t NativeSha256_ReadBE32(const uint8_t *bytes)
{
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void NativeSha256_WriteBE32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)(value >> 24);
	bytes[1] = (uint8_t)(value >> 16);
	bytes[2] = (uint8_t)(value >> 8);
	bytes[3] = (uint8_t)value;
}

static void NativeSha256_Transform(struct NativeSha256 *sha, const uint8_t block[64])
{
	uint32_t words[64];
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint32_t e;
	uint32_t f;
	uint32_t g;
	uint32_t h;

	for (uint32_t i = 0; i < 16; i++)
	{
		words[i] = NativeSha256_ReadBE32(&block[i * 4u]);
	}
	for (uint32_t i = 16; i < 64; i++)
	{
		const uint32_t sigma0 = NATIVE_SHA256_ROTATE_RIGHT(words[i - 15u], 7u) ^ NATIVE_SHA256_ROTATE_RIGHT(words[i - 15u], 18u) ^
		                        (words[i - 15u] >> 3);
		const uint32_t sigma1 = NATIVE_SHA256_ROTATE_RIGHT(words[i - 2u], 17u) ^ NATIVE_SHA256_ROTATE_RIGHT(words[i - 2u], 19u) ^
		                        (words[i - 2u] >> 10);
		words[i] = words[i - 16u] + sigma0 + words[i - 7u] + sigma1;
	}

	a = sha->state[0];
	b = sha->state[1];
	c = sha->state[2];
	d = sha->state[3];
	e = sha->state[4];
	f = sha->state[5];
	g = sha->state[6];
	h = sha->state[7];
	for (uint32_t i = 0; i < 64; i++)
	{
		const uint32_t sum1 = NATIVE_SHA256_ROTATE_RIGHT(e, 6u) ^ NATIVE_SHA256_ROTATE_RIGHT(e, 11u) ^ NATIVE_SHA256_ROTATE_RIGHT(e, 25u);
		const uint32_t choice = (e & f) ^ ((~e) & g);
		const uint32_t temp1 = h + sum1 + choice + s_nativeSha256Constants[i] + words[i];
		const uint32_t sum0 = NATIVE_SHA256_ROTATE_RIGHT(a, 2u) ^ NATIVE_SHA256_ROTATE_RIGHT(a, 13u) ^ NATIVE_SHA256_ROTATE_RIGHT(a, 22u);
		const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		const uint32_t temp2 = sum0 + majority;

		h = g;
		g = f;
		f = e;
		e = d + temp1;
		d = c;
		c = b;
		b = a;
		a = temp1 + temp2;
	}

	sha->state[0] += a;
	sha->state[1] += b;
	sha->state[2] += c;
	sha->state[3] += d;
	sha->state[4] += e;
	sha->state[5] += f;
	sha->state[6] += g;
	sha->state[7] += h;
}

void NativeSha256_Init(struct NativeSha256 *sha)
{
	if (sha == NULL)
	{
		return;
	}

	sha->state[0] = UINT32_C(0x6a09e667);
	sha->state[1] = UINT32_C(0xbb67ae85);
	sha->state[2] = UINT32_C(0x3c6ef372);
	sha->state[3] = UINT32_C(0xa54ff53a);
	sha->state[4] = UINT32_C(0x510e527f);
	sha->state[5] = UINT32_C(0x9b05688c);
	sha->state[6] = UINT32_C(0x1f83d9ab);
	sha->state[7] = UINT32_C(0x5be0cd19);
	sha->bitCount = 0;
	sha->blockSize = 0;
}

void NativeSha256_Update(struct NativeSha256 *sha, const void *bytes, size_t size)
{
	const uint8_t *input = (const uint8_t *)bytes;

	if ((sha == NULL) || ((input == NULL) && (size != 0)))
	{
		return;
	}

	sha->bitCount += (uint64_t)size * 8u;
	while (size != 0)
	{
		size_t copySize = sizeof(sha->block) - sha->blockSize;

		if (copySize > size)
		{
			copySize = size;
		}
		memcpy(&sha->block[sha->blockSize], input, copySize);
		sha->blockSize += copySize;
		input += copySize;
		size -= copySize;

		if (sha->blockSize == sizeof(sha->block))
		{
			NativeSha256_Transform(sha, sha->block);
			sha->blockSize = 0;
		}
	}
}

void NativeSha256_Final(struct NativeSha256 *sha, uint8_t digest[NATIVE_SHA256_DIGEST_BYTES])
{
	uint8_t lengthBytes[8];
	const uint64_t bitCount = sha != NULL ? sha->bitCount : 0;

	if ((sha == NULL) || (digest == NULL))
	{
		return;
	}

	sha->block[sha->blockSize++] = 0x80;
	if (sha->blockSize > 56)
	{
		memset(&sha->block[sha->blockSize], 0, sizeof(sha->block) - sha->blockSize);
		NativeSha256_Transform(sha, sha->block);
		sha->blockSize = 0;
	}
	memset(&sha->block[sha->blockSize], 0, 56u - sha->blockSize);
	for (uint32_t i = 0; i < 8; i++)
	{
		lengthBytes[7u - i] = (uint8_t)(bitCount >> (i * 8u));
	}
	memcpy(&sha->block[56], lengthBytes, sizeof(lengthBytes));
	NativeSha256_Transform(sha, sha->block);
	sha->blockSize = 0;

	for (uint32_t i = 0; i < 8; i++)
	{
		NativeSha256_WriteBE32(&digest[i * 4u], sha->state[i]);
	}
}
