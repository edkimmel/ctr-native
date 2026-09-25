#include "platform/native_arcade_race_drive.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * native_arcade_race_drive_unit (docs/LOCKSTEP_RACE_MILESTONE.md LR-S7): the
 * pad normalization of LR-4 and LR-5 (LR-40) over every status and id byte,
 * each with a disconnected and three connected bytes, START pressed and
 * released, every other button bit and analog byte preserved, NULL
 * arguments, and the neutral pad.
 */

static int s_failures;

#define CHECK(expression)                                                                   \
	do                                                                                      \
	{                                                                                       \
		if (!(expression))                                                                  \
		{                                                                                   \
			fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression);       \
			s_failures++;                                                                   \
			return;                                                                         \
		}                                                                                   \
	} while (0)

static struct NativeCanonicalInputPadV1 MakePad(uint8_t connected, uint8_t status, uint8_t id, uint16_t buttons, uint8_t a0, uint8_t a1, uint8_t a2, uint8_t a3)
{
	struct NativeCanonicalInputPadV1 pad;

	memset(&pad, 0, sizeof(pad));
	pad.connected = connected;
	pad.status = status;
	pad.id = id;
	pad.buttons[0] = (uint8_t)(buttons & 0xffu);
	pad.buttons[1] = (uint8_t)(buttons >> 8);
	pad.analog[0] = a0;
	pad.analog[1] = a1;
	pad.analog[2] = a2;
	pad.analog[3] = a3;
	return pad;
}

static uint16_t Buttons(const struct NativeCanonicalInputPadV1 *pad)
{
	return (uint16_t)(pad->buttons[0] | (pad->buttons[1] << 8));
}

static int IsNeutral(const struct NativeCanonicalInputPadV1 *pad)
{
	return (pad->connected == 1u) && (pad->status == 0u) && (pad->id == 0x41u) && (pad->buttons[0] == 0xffu) && (pad->buttons[1] == 0xffu) &&
	       (pad->analog[0] == 0x80u) && (pad->analog[1] == 0x80u) && (pad->analog[2] == 0x80u) && (pad->analog[3] == 0x80u);
}

static void TestConstants(void)
{
	CHECK(NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL == 0x41u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG == 0x73u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_START_MASK == 0x0008u);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS == 0xffffu);
	CHECK(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG == 0x80u);
	CHECK(sizeof(struct NativeCanonicalInputPadV1) == 9u);
}

static void TestNeutralPad(void)
{
	struct NativeCanonicalInputPadV1 pad;

	memset(&pad, 0x5a, sizeof(pad));
	NativeArcadeRaceDrive_NeutralPad(&pad);
	CHECK(IsNeutral(&pad));

	/* NULL is a no-op (it must not crash). */
	NativeArcadeRaceDrive_NeutralPad(NULL);
}

/* Every status byte and every id byte, each with connected 0, 1, 2, and
 * 0xff: connected 0 is the neutral pad whatever the other bytes; any other
 * connected byte keeps the buttons (START released) and analog bytes, and
 * gets connected 1, status 0, and id 0x73 only for 0x73. */
static void TestEveryStatusAndIdByte(void)
{
	static const uint8_t connectedBytes[4] = {0u, 1u, 2u, 0xffu};

	for (unsigned field = 0; field < 2u; field++)
	{
		for (unsigned value = 0; value < 256u; value++)
		{
			for (unsigned c = 0; c < 4u; c++)
			{
				uint8_t status = field == 0u ? (uint8_t)value : (uint8_t)0x5au;
				uint8_t id = field == 1u ? (uint8_t)value : (uint8_t)0x73u;
				struct NativeCanonicalInputPadV1 in = MakePad(connectedBytes[c], status, id, 0xbfb7u, 0x01u, 0x7fu, 0xfeu, 0x42u);
				struct NativeCanonicalInputPadV1 before = in;
				struct NativeCanonicalInputPadV1 out;

				memset(&out, 0xa5, sizeof(out));
				NativeArcadeRaceDrive_NormalizePad(&in, &out);
				CHECK(memcmp(&in, &before, sizeof(in)) == 0);
				if (connectedBytes[c] == 0u)
				{
					CHECK(IsNeutral(&out));
					continue;
				}
				CHECK(out.connected == 1u);
				CHECK(out.status == 0u);
				CHECK(out.id == (id == 0x73u ? 0x73u : 0x41u));
				/* 0xbfb7: CROSS (0x4000) and START (0x0008) pressed; START is released. */
				CHECK(Buttons(&out) == 0xbfbfu);
				CHECK(out.analog[0] == 0x01u && out.analog[1] == 0x7fu && out.analog[2] == 0xfeu && out.analog[3] == 0x42u);
			}
		}
	}
}

/* START is released whether it came pressed or released; every other bit of
 * the word passes through, one bit pressed at a time and all at once. */
static void TestButtons(void)
{
	struct NativeCanonicalInputPadV1 out;
	struct NativeCanonicalInputPadV1 in;

	in = MakePad(1u, 0u, 0x41u, 0xfff7u, 0x80u, 0x80u, 0x80u, 0x80u);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(Buttons(&out) == 0xffffu);

	in = MakePad(1u, 0u, 0x41u, 0xffffu, 0x80u, 0x80u, 0x80u, 0x80u);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(Buttons(&out) == 0xffffu);

	in = MakePad(1u, 0u, 0x41u, 0x0000u, 0x80u, 0x80u, 0x80u, 0x80u);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(Buttons(&out) == 0x0008u);

	for (unsigned bit = 0; bit < 16u; bit++)
	{
		uint16_t pressed = (uint16_t)(0xffffu & ~(1u << bit));
		uint16_t expected = (uint16_t)(pressed | 0x0008u);

		in = MakePad(1u, 0u, 0x73u, pressed, 0x80u, 0x80u, 0x80u, 0x80u);
		NativeArcadeRaceDrive_NormalizePad(&in, &out);
		CHECK(Buttons(&out) == expected);
		CHECK(out.buttons[0] == (uint8_t)(expected & 0xffu) && out.buttons[1] == (uint8_t)(expected >> 8));
	}

	/* Every button word: only bit 0x0008 changes. */
	for (unsigned word = 0; word < 0x10000u; word++)
	{
		in = MakePad(2u, 0x12u, 0x41u, (uint16_t)word, 0x80u, 0x80u, 0x80u, 0x80u);
		NativeArcadeRaceDrive_NormalizePad(&in, &out);
		CHECK(Buttons(&out) == (uint16_t)(word | 0x0008u));
	}
}

static void TestAnalogBytes(void)
{
	struct NativeCanonicalInputPadV1 in;
	struct NativeCanonicalInputPadV1 out;

	for (unsigned axis = 0; axis < 4u; axis++)
	{
		for (unsigned value = 0; value < 256u; value++)
		{
			in = MakePad(1u, 0xffu, 0xffu, 0xffffu, 0x80u, 0x80u, 0x80u, 0x80u);
			in.analog[axis] = (uint8_t)value;
			NativeArcadeRaceDrive_NormalizePad(&in, &out);
			for (unsigned other = 0; other < 4u; other++)
			{
				CHECK(out.analog[other] == (other == axis ? (uint8_t)value : 0x80u));
			}
			CHECK(out.id == 0x41u && out.status == 0u && out.connected == 1u);
		}
	}

	/* A disconnected pad's analog bytes do not survive. */
	in = MakePad(0u, 0u, 0x73u, 0x0000u, 0x00u, 0xffu, 0x00u, 0xffu);
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(IsNeutral(&out));
}

/* The status byte of a connected pad never decides disconnection: 0xff with
 * id 0xff (the pad bus's disconnected packet) is still connected. */
static void TestStatusDoesNotDisconnect(void)
{
	struct NativeCanonicalInputPadV1 in = MakePad(1u, 0xffu, 0xffu, 0xbfffu, 0x10u, 0x20u, 0x30u, 0x40u);
	struct NativeCanonicalInputPadV1 out;

	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(out.connected == 1u && out.status == 0u && out.id == 0x41u);
	CHECK(Buttons(&out) == 0xbfffu);
	CHECK(out.analog[0] == 0x10u && out.analog[3] == 0x40u);
}

static void TestInPlace(void)
{
	struct NativeCanonicalInputPadV1 pad = MakePad(0xffu, 0x33u, 0x73u, 0x7ff7u, 0x00u, 0x11u, 0x22u, 0x33u);

	NativeArcadeRaceDrive_NormalizePad(&pad, &pad);
	CHECK(pad.connected == 1u && pad.status == 0u && pad.id == 0x73u);
	CHECK(Buttons(&pad) == 0x7fffu);
	CHECK(pad.analog[0] == 0x00u && pad.analog[1] == 0x11u && pad.analog[2] == 0x22u && pad.analog[3] == 0x33u);

	pad = MakePad(0u, 0x33u, 0x73u, 0x0000u, 0x00u, 0x11u, 0x22u, 0x33u);
	NativeArcadeRaceDrive_NormalizePad(&pad, &pad);
	CHECK(IsNeutral(&pad));
}

/* The all-zero pad (LR-5's frames 0 to D - 1) is disconnected: neutral. */
static void TestZeroPad(void)
{
	struct NativeCanonicalInputPadV1 in;
	struct NativeCanonicalInputPadV1 out;

	memset(&in, 0, sizeof(in));
	NativeArcadeRaceDrive_NormalizePad(&in, &out);
	CHECK(IsNeutral(&out));
}

static void TestNullArguments(void)
{
	struct NativeCanonicalInputPadV1 in = MakePad(1u, 0u, 0x73u, 0x0000u, 0u, 0u, 0u, 0u);
	struct NativeCanonicalInputPadV1 out;
	struct NativeCanonicalInputPadV1 sentinel;

	memset(&out, 0xa5, sizeof(out));
	sentinel = out;
	NativeArcadeRaceDrive_NormalizePad(NULL, &out);
	CHECK(memcmp(&out, &sentinel, sizeof(out)) == 0);
	NativeArcadeRaceDrive_NormalizePad(&in, NULL);
	NativeArcadeRaceDrive_NormalizePad(NULL, NULL);
	CHECK(in.connected == 1u && in.id == 0x73u && Buttons(&in) == 0x0000u);
}

int main(void)
{
	TestConstants();
	TestNeutralPad();
	TestEveryStatusAndIdByte();
	TestButtons();
	TestAnalogBytes();
	TestStatusDoesNotDisconnect();
	TestInPlace();
	TestZeroPad();
	TestNullArguments();

	if (s_failures != 0)
	{
		fprintf(stderr, "native_arcade_race_drive_unit: %d failure(s)\n", s_failures);
		return 1;
	}
	printf("native_arcade_race_drive_unit: ok\n");
	return 0;
}
