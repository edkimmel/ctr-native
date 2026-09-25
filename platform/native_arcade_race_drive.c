#include "platform/native_arcade_race_drive.h"

#include "platform/native_canonical_state.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Linked-race drive core (docs/LOCKSTEP_RACE_MILESTONE.md LR-1). LR-S7: pad
 * normalization only. LR-S8 adds the per-tick drive.
 */

void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out)
{
	if (out == NULL)
	{
		return;
	}

	memset(out, 0, sizeof(*out));
	out->status = 0u;
	out->id = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL;
	out->buttons[0] = (uint8_t)(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS & 0xffu);
	out->buttons[1] = (uint8_t)(NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS >> 8);
	out->analog[0] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[1] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[2] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->analog[3] = (uint8_t)NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG;
	out->connected = 1u;
}

void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out)
{
	struct NativeCanonicalInputPadV1 pad;

	if ((in == NULL) || (out == NULL))
	{
		return;
	}

	/* LR-40: only the connected byte decides disconnection. */
	if (in->connected == 0u)
	{
		NativeArcadeRaceDrive_NeutralPad(&pad);
	}
	else
	{
		pad = *in;
		pad.connected = 1u;
		pad.status = 0u;
		pad.id = (uint8_t)((in->id == NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG) ? NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG
		                                                                     : NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL);
		/* Active-low: setting the bits releases START. */
		pad.buttons[0] = (uint8_t)(pad.buttons[0] | (NATIVE_ARCADE_RACE_DRIVE_START_MASK & 0xffu));
		pad.buttons[1] = (uint8_t)(pad.buttons[1] | (NATIVE_ARCADE_RACE_DRIVE_START_MASK >> 8));
	}

	*out = pad;
}
