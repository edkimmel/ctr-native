#include "platform/native_canonical_state.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                         \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static void MakeGoldenState(struct NativeCanonicalStateV1 *state)
{
	NativeCanonicalStateV1_Init(state);
	state->frameNumber = UINT32_C(0x01020304);
	state->control.frameTimer = 1;
	state->control.frameCounter = -2;
	state->control.timer = INT32_C(0x10203040);
	state->control.framesInThisLEV = INT32_MIN;
	state->control.elapsedTimeMS = INT32_MAX;
	state->control.msInThisLEV = 0;
	state->control.elapsedEventTime = 7;
	state->control.mainGameState = -8;
	state->control.loadingStage = 9;
	state->control.levelID = -10;
	state->rng.mixRandomNumber = UINT32_C(0x11223344);
	state->rng.deadcoed0 = UINT32_C(0x55667788);
	state->rng.deadcoed1 = UINT32_C(0x99aabbcc);
	state->rng.advRng0 = UINT32_C(0xddeeff00);
	state->rng.advRng1 = UINT32_C(0x01020304);
	state->rng.psxRngSeed = UINT32_C(0x89abcdef);

	for (uint32_t i = 0; i < NATIVE_IDENTITY_DIGEST_BYTES; i++)
	{
		state->identity.build[i] = (uint8_t)i;
		state->identity.content[i] = (uint8_t)(0x80u + i);
	}

	for (uint32_t i = 0; i < NATIVE_CANONICAL_INPUT_PAD_COUNT; i++)
	{
		struct NativeCanonicalInputPadV1 *pad = &state->input.pads[i];

		pad->status = (uint8_t)(0xa0u + i);
		pad->id = (uint8_t)(0xb0u + i);
		pad->buttons[0] = (uint8_t)(0xc0u + i);
		pad->buttons[1] = (uint8_t)(0xd0u + i);
		pad->analog[0] = (uint8_t)(0xe0u + (4u * i));
		pad->analog[1] = (uint8_t)(0xe1u + (4u * i));
		pad->analog[2] = (uint8_t)(0xe2u + (4u * i));
		pad->analog[3] = (uint8_t)(0xe3u + (4u * i));
		pad->connected = (i == 1u) ? 0 : 1;
	}

	(void)NativeCanonicalStateV1_ComputeDigests(state);
}

static int StatesEqual(const struct NativeCanonicalStateV1 *left, const struct NativeCanonicalStateV1 *right)
{
	if ((left->schemaVersion != right->schemaVersion) || (left->replayFormatVersion != right->replayFormatVersion) ||
	    (left->domainCount != right->domainCount) || (left->frameNumber != right->frameNumber) ||
	    (memcmp(left->identity.build, right->identity.build, NATIVE_IDENTITY_DIGEST_BYTES) != 0) ||
	    (memcmp(left->identity.content, right->identity.content, NATIVE_IDENTITY_DIGEST_BYTES) != 0) ||
	    (memcmp(&left->control, &right->control, sizeof(left->control)) != 0) || (memcmp(&left->rng, &right->rng, sizeof(left->rng)) != 0) ||
	    (left->input.padCount != right->input.padCount) || (memcmp(left->input.pads, right->input.pads, sizeof(left->input.pads)) != 0) ||
	    (memcmp(left->domainDigests, right->domainDigests, sizeof(left->domainDigests)) != 0) ||
	    (left->combinedDigest != right->combinedDigest))
	{
		return 0;
	}

	return 1;
}

static int EncodeGolden(uint8_t *bytes, struct NativeCanonicalStateV1 *state)
{
	struct NativeCodecWriter writer;

	MakeGoldenState(state);
	NativeCodecWriter_Init(&writer, bytes, NativeCanonicalStateV1_EncodedSize(), NULL);
	if (!NativeCanonicalStateV1_Encode(&writer, state) || !NativeCodecWriter_Ok(&writer) ||
	    (NativeCodecWriter_Size(&writer) != NativeCanonicalStateV1_EncodedSize()))
	{
		return 0;
	}

	return 1;
}

static int TestGoldenWireAndRoundTrip(void)
{
	static const uint8_t expected[] = {
		0x4e, 0x43, 0x56, 0x31, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01, 0x00, 0x01, 0x02, 0x03,
		0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
		0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
		0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
		0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91,
		0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d,
		0x9e, 0x9f, 0x01, 0x00, 0x00, 0x00,
		0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff,
		0x40, 0x30, 0x20, 0x10, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0x7f,
		0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff,
		0x09, 0x00, 0x00, 0x00, 0xf6, 0xff, 0xff, 0xff, 0x85, 0x60, 0xb1, 0xa8,
		0x47, 0x44, 0x60, 0xf5, 0x02, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
		0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0xcc, 0xbb, 0xaa, 0x99,
		0x00, 0xff, 0xee, 0xdd, 0x04, 0x03, 0x02, 0x01, 0xef, 0xcd, 0xab, 0x89,
		0xbd, 0x05, 0xe8, 0x09, 0xb5, 0x81, 0x46, 0xb8, 0x03, 0x00, 0x00, 0x00,
		0x28, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xa0, 0xb0, 0xc0, 0xd0,
		0xe0, 0xe1, 0xe2, 0xe3, 0x01, 0xa1, 0xb1, 0xc1, 0xd1, 0xe4, 0xe5, 0xe6,
		0xe7, 0x00, 0xa2, 0xb2, 0xc2, 0xd2, 0xe8, 0xe9, 0xea, 0xeb, 0x01, 0xa3,
		0xb3, 0xc3, 0xd3, 0xec, 0xed, 0xee, 0xef, 0x01, 0x9e, 0x58, 0xc4, 0x2a,
		0x6c, 0x58, 0x75, 0x3d, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x25, 0x23, 0x22, 0x84, 0xe4, 0x9c, 0xf2, 0xcb, 0x05, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x25, 0x23, 0x22, 0x84, 0xe4, 0x9c, 0xf2, 0xcb,
		0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x25, 0x23, 0x22, 0x84,
		0xe4, 0x9c, 0xf2, 0xcb, 0x20, 0x7a, 0x05, 0xf3, 0x16, 0x28, 0x69, 0xd6,
	};
	uint8_t bytes[sizeof(expected)] = {0};
	struct NativeCanonicalStateV1 expectedState;
	struct NativeCanonicalStateV1 decoded;
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;
	struct NativeCodecReader reader;

	CHECK(NativeCanonicalStateV1_EncodedSize() == sizeof(expected));
	MakeGoldenState(&expectedState);
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(NativeCanonicalStateV1_Encode(&writer, &expectedState));
	CHECK(NativeCodecWriter_Size(&writer) == sizeof(expected));
	CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
	CHECK(digest.value == UINT64_C(0x5ab93fc7b1588807));
	CHECK(expectedState.domainDigests[NATIVE_CANONICAL_DOMAIN_CONTROL - 1u] == UINT64_C(0xf5604447a8b16085));
	CHECK(expectedState.domainDigests[NATIVE_CANONICAL_DOMAIN_RNG - 1u] == UINT64_C(0xb84681b509e805bd));
	CHECK(expectedState.domainDigests[NATIVE_CANONICAL_DOMAIN_INPUT - 1u] == UINT64_C(0x3d75586c2ac4589e));
	CHECK(expectedState.domainDigests[NATIVE_CANONICAL_DOMAIN_DRIVERS - 1u] == UINT64_C(0xcbf29ce484222325));
	CHECK(expectedState.domainDigests[NATIVE_CANONICAL_DOMAIN_WORLD - 1u] == UINT64_C(0xcbf29ce484222325));
	CHECK(expectedState.domainDigests[NATIVE_CANONICAL_DOMAIN_TOPOLOGY - 1u] == UINT64_C(0xcbf29ce484222325));
	CHECK(expectedState.combinedDigest == UINT64_C(0xd6692816f3057a20));

	NativeCanonicalStateV1_Init(&decoded);
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(NativeCanonicalStateV1_Decode(&reader, &expectedState.identity, &decoded));
	CHECK(NativeCodecReader_Remaining(&reader) == 0);
	CHECK(StatesEqual(&decoded, &expectedState));
	return 0;
}

static int TestDomainPerturbationIsolation(void)
{
	struct NativeCanonicalStateV1 base;
	struct NativeCanonicalStateV1 changed;

	MakeGoldenState(&base);
	changed = base;
	changed.control.timer++;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&changed));
	CHECK(changed.domainDigests[0] != base.domainDigests[0]);
	CHECK(memcmp(&changed.domainDigests[1], &base.domainDigests[1], sizeof(uint64_t) * 5u) == 0);
	CHECK(changed.combinedDigest != base.combinedDigest);

	changed = base;
	changed.rng.advRng1++;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&changed));
	CHECK(changed.domainDigests[1] != base.domainDigests[1]);
	CHECK(changed.domainDigests[0] == base.domainDigests[0]);
	CHECK(memcmp(&changed.domainDigests[2], &base.domainDigests[2], sizeof(uint64_t) * 4u) == 0);
	CHECK(changed.combinedDigest != base.combinedDigest);

	changed = base;
	changed.input.pads[2].analog[3]++;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&changed));
	CHECK(changed.domainDigests[2] != base.domainDigests[2]);
	CHECK(memcmp(changed.domainDigests, base.domainDigests, sizeof(uint64_t) * 2u) == 0);
	CHECK(memcmp(&changed.domainDigests[3], &base.domainDigests[3], sizeof(uint64_t) * 3u) == 0);
	CHECK(changed.combinedDigest != base.combinedDigest);

	changed = base;
	changed.identity.build[0] ^= 1;
	changed.identity.content[0] ^= 1;
	CHECK(NativeCanonicalStateV1_ComputeDigests(&changed));
	CHECK(memcmp(changed.domainDigests, base.domainDigests, sizeof(base.domainDigests)) == 0);
	CHECK(changed.combinedDigest == base.combinedDigest);
	return 0;
}

static int DecodeMustFailWithoutMutation(uint8_t *bytes, size_t size)
{
	struct NativeCanonicalStateV1 unchanged;
	struct NativeCanonicalStateV1 output;
	struct NativeCodecReader reader;

	MakeGoldenState(&unchanged);
	output = unchanged;
	NativeCodecReader_Init(&reader, bytes, size);
	CHECK(!NativeCanonicalStateV1_Decode(&reader, &unchanged.identity, &output));
	CHECK(reader.offset == 0);
	CHECK(NativeCodecReader_Ok(&reader));
	CHECK(StatesEqual(&output, &unchanged));
	return 0;
}

static int TestDecodeGatesAndTransactions(void)
{
	uint8_t bytes[292];
	struct NativeCanonicalStateV1 state;

	CHECK(EncodeGolden(bytes, &state));
	bytes[0] ^= 1;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[4] = 2;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[8] = 3;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[12] = 5;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[84] = 2;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[88] = 39;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[188] = 3;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[132] ^= 1;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	bytes[284] ^= 1;
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes)) == 0);
	CHECK(EncodeGolden(bytes, &state));
	CHECK(DecodeMustFailWithoutMutation(bytes, sizeof(bytes) - 1u) == 0);
	return 0;
}

static int TestIdentityGates(void)
{
	uint8_t bytes[292];
	struct NativeCanonicalStateV1 state;
	struct NativeCanonicalStateV1 output;
	struct NativeIdentityV1 expectedIdentity;
	struct NativeCodecReader reader;

	CHECK(EncodeGolden(bytes, &state));
	expectedIdentity = state.identity;
	expectedIdentity.build[0] ^= 1;
	output = state;
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(!NativeCanonicalStateV1_Decode(&reader, &expectedIdentity, &output));
	CHECK(reader.offset == 0);
	CHECK(NativeCodecReader_Ok(&reader));
	CHECK(StatesEqual(&output, &state));

	expectedIdentity = state.identity;
	expectedIdentity.content[NATIVE_IDENTITY_DIGEST_BYTES - 1u] ^= 1;
	output = state;
	NativeCodecReader_Init(&reader, bytes, sizeof(bytes));
	CHECK(!NativeCanonicalStateV1_Decode(&reader, &expectedIdentity, &output));
	CHECK(reader.offset == 0);
	CHECK(NativeCodecReader_Ok(&reader));
	CHECK(StatesEqual(&output, &state));
	return 0;
}

static int TestEncodeTransactions(void)
{
	uint8_t bytes[292];
	uint8_t before[292];
	struct NativeCanonicalStateV1 state;
	struct NativeCodecDigest64 digest;
	struct NativeCodecWriter writer;

	memset(bytes, 0xcc, sizeof(bytes));
	memcpy(before, bytes, sizeof(bytes));
	MakeGoldenState(&state);
	state.schemaVersion++;
	NativeCodecDigest64_Init(&digest);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), &digest);
	CHECK(!NativeCanonicalStateV1_Encode(&writer, &state));
	CHECK(writer.offset == 0);
	CHECK(NativeCodecWriter_Ok(&writer));
	CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
	CHECK(digest.value == UINT64_C(0xcbf29ce484222325));

	MakeGoldenState(&state);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes) - 1u, NULL);
	CHECK(!NativeCanonicalStateV1_Encode(&writer, &state));
	CHECK(writer.offset == 0);
	CHECK(NativeCodecWriter_Ok(&writer));
	CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);

	MakeGoldenState(&state);
	state.control.timer++;
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(!NativeCanonicalStateV1_Encode(&writer, &state));
	CHECK(writer.offset == 0);
	CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);

	MakeGoldenState(&state);
	state.combinedDigest ^= UINT64_C(1);
	NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
	CHECK(!NativeCanonicalStateV1_Encode(&writer, &state));
	CHECK(writer.offset == 0);
	CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
	return 0;
}

int main(void)
{
	if ((TestGoldenWireAndRoundTrip() != 0) || (TestDomainPerturbationIsolation() != 0) ||
	    (TestDecodeGatesAndTransactions() != 0) || (TestIdentityGates() != 0) || (TestEncodeTransactions() != 0))
	{
		return 1;
	}

	puts("native_canonical_state_test: passed");
	return 0;
}
